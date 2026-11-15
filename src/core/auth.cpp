#include "core.hpp"
#include <chrono>
#include <thread>

namespace neko {
QString microsoftAuthError(const Json &response) {
    const auto error = response["error"].toString("unknown_error");
    QStringList codes;
    for (const auto &value : response["error_codes"].toArray())
        codes << "AADSTS" + QString::number(value.toInt());
    QString message = "Microsoft sign-in failed: " + error +
                      (codes.isEmpty() ? QString{} : " (" + codes.join(", ") + ")") + ".";
    if (codes.contains("AADSTS70002") || codes.contains("AADSTS7000218"))
        return message +
               " This app must be a public/mobile client. In Microsoft Entra > App registrations > your app "
               "> Authentication, enable Allow public client flows and save. If needed, add the Mobile and "
               "desktop applications platform. No client secret belongs in this launcher.";
    if (codes.contains("AADSTS700016"))
        return message + " Microsoft could not find this application. Check the Application (client) ID and "
                         "that the registration supports personal Microsoft accounts.";
    if (error == "authorization_declined" || error == "access_denied")
        return message + " Sign-in was declined. Start again when ready.";
    if (error == "expired_token")
        return message + " The device code expired. Start sign-in again.";
    auto detail = response["error_description"].toString().section("Trace ID:", 0, 0).trimmed();
    if (!detail.isEmpty())
        message += " " + detail.left(600);
    return message;
}
Account Engine::authenticate(const QString &clientId, std::function<void(QString, QString)> code) {
    require(!clientId.trimmed().isEmpty(), "Set your Microsoft public client application ID in Settings. The "
                                           "app must be approved for Minecraft services.");
    auto form = [&](const QString &path, const QMap<QString, QString> &values, bool allowError = false) {
        QUrlQuery q;
        for (auto it = values.begin(); it != values.end(); ++it)
            q.addQueryItem(it.key(), it.value());
        auto raw = http.request("https://login.microsoftonline.com/consumers/oauth2/v2.0/" + path,
                                q.toString(QUrl::FullyEncoded).replace("+", "%2B").toUtf8(),
                                "application/x-www-form-urlencoded", {}, true);
        auto doc = QJsonDocument::fromJson(raw);
        require(doc.isObject(), "Invalid Microsoft response");
        if (!allowError && doc.object().contains("error"))
            throw Error(microsoftAuthError(doc.object()).toStdString());
        return doc.object();
    };
    auto device = form("devicecode", {{"client_id", clientId}, {"scope", "XboxLive.signin offline_access"}});
    require(!device["device_code"].toString().isEmpty(), "Microsoft did not return a device code");
    code(device["verification_uri"].toString(), device["user_code"].toString());
    int interval = qMax(1, device["interval"].toInt(5));
    auto end = QDateTime::currentSecsSinceEpoch() + device["expires_in"].toInt(900);
    QString access, refreshToken;
    while (QDateTime::currentSecsSinceEpoch() < end) {
        for (int i = 0; i < interval * 10; ++i) {
            http.checkCancelled();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        auto result = form("token",
                           {{"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
                            {"client_id", clientId},
                            {"device_code", device["device_code"].toString()}},
                           true);
        access = result["access_token"].toString();
        if (!access.isEmpty()) {
            refreshToken = result["refresh_token"].toString();
            break;
        }
        auto error = result["error"].toString();
        if (error == "slow_down")
            interval += 5;
        else
            require(error == "authorization_pending", microsoftAuthError(result));
    }
    require(!access.isEmpty(), "Sign-in code expired");
    return minecraftAccount(access, refreshToken, clientId);
}
Account Engine::refresh(const Account &account) {
    require(!account.refreshToken.isEmpty(), "Sign in once to save your Microsoft account securely.");
    QUrlQuery form;
    form.addQueryItem("client_id", account.clientId);
    form.addQueryItem("grant_type", "refresh_token");
    form.addQueryItem("refresh_token", account.refreshToken);
    auto response =
        QJsonDocument::fromJson(http.request("https://login.microsoftonline.com/consumers/oauth2/v2.0/token",
                                             form.toString(QUrl::FullyEncoded).replace("+", "%2B").toUtf8(),
                                             "application/x-www-form-urlencoded", {}, true))
            .object();
    require(!response.contains("error"), microsoftAuthError(response));
    require(!response["access_token"].toString().isEmpty(), "Microsoft refresh returned no access token");
    return minecraftAccount(response["access_token"].toString(),
                            response["refresh_token"].toString(account.refreshToken), account.clientId);
}
Account Engine::minecraftAccount(const QString &access, const QString &refreshToken,
                                 const QString &clientId) {
    report("Authenticating Xbox Live");
    auto xbox = http.json(
        "https://user.auth.xboxlive.com/user/authenticate",
        {{"Properties",
          Json{{"AuthMethod", "RPS"}, {"SiteName", "user.auth.xboxlive.com"}, {"RpsTicket", "d=" + access}}},
         {"RelyingParty", "http://auth.xboxlive.com"},
         {"TokenType", "JWT"}});
    auto xsts = http.json(
        "https://xsts.auth.xboxlive.com/xsts/authorize",
        {{"Properties", Json{{"SandboxId", "RETAIL"}, {"UserTokens", QJsonArray{xbox["Token"].toString()}}}},
         {"RelyingParty", "rp://api.minecraftservices.com/"},
         {"TokenType", "JWT"}});
    auto claims = xsts["DisplayClaims"].toObject()["xui"].toArray();
    require(!claims.isEmpty(), "Xbox account has no Minecraft identity");
    auto uhs = claims.first().toObject()["uhs"].toString();
    auto mc = http.json("https://api.minecraftservices.com/authentication/login_with_xbox",
                        {{"identityToken", "XBL3.0 x=" + uhs + ";" + xsts["Token"].toString()}});
    auto token = mc["access_token"].toString();
    require(!token.isEmpty(), "Minecraft authentication denied");
    QMap<QByteArray, QByteArray> headers{{"Authorization", ("Bearer " + token).toUtf8()}};
    auto ent = http.json("https://api.minecraftservices.com/entitlements/mcstore", {}, headers);
    require(!ent["items"].toArray().isEmpty(), "This account has no Minecraft Java entitlement");
    auto profile = http.json("https://api.minecraftservices.com/minecraft/profile", {}, headers);
    require(!profile["id"].toString().isEmpty(),
            "No Java profile found. Set up your profile in the official launcher first.");
    return {profile["name"].toString(),
            profile["id"].toString(),
            token,
            QDateTime::currentSecsSinceEpoch() + mc["expires_in"].toInt(3600),
            refreshToken,
            clientId};
}
} // namespace neko
