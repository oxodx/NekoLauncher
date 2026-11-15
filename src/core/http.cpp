#include "core.hpp"
#include "file_ops.hpp"

namespace neko {
void Http::checkCancelled() const {
    require(!cancellation || !cancellation->load(), "Operation cancelled");
}
QByteArray Http::request(const QString &url, const QByteArray &body, const QByteArray &type,
                         const QMap<QByteArray, QByteArray> &headers, bool allowError) {
    checkCancelled();
    QUrl u(url);
    require(u.scheme() == "https" && !u.host().isEmpty() && u.userInfo().isEmpty(),
            "Only HTTPS downloads are allowed");
    QNetworkRequest req(u);
    req.setTransferTimeout(60000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "NekoLauncher/0.1.0 (desktop C++ launcher)");
    for (auto i = headers.begin(); i != headers.end(); ++i)
        req.setRawHeader(i.key(), i.value());
    if (!type.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, type);
    QNetworkReply *reply = type.isEmpty() ? manager.get(req) : manager.post(req, body);
    auto cleanup = qScopeGuard([&] { delete reply; });
    QByteArray data;
    bool oversized = false;
    QObject::connect(reply, &QIODevice::readyRead, reply, [&] {
        data.append(reply->readAll());
        if (data.size() > 512 * 1024 * 1024) {
            oversized = true;
            reply->abort();
        }
    });
    QTimer cancelTimer;
    QObject::connect(&cancelTimer, &QTimer::timeout, reply, [&] {
        if (cancellation && cancellation->load())
            reply->abort();
    });
    cancelTimer.start(100);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    checkCancelled();
    data.append(reply->readAll());
    require(!oversized, "Download exceeds 512 MiB limit");
    auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    require((allowError && status >= 400) ||
                (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300),
            QString("Request failed (%1): %2 — %3").arg(status).arg(u.host(), reply->errorString()));
    require(data.size() <= 512 * 1024 * 1024, "Download exceeds memory limit");
    return data;
}
Json Http::json(const QString &url, const Json &body, const QMap<QByteArray, QByteArray> &headers) {
    auto bytes =
        request(url, body.isEmpty() ? QByteArray{} : QJsonDocument(body).toJson(QJsonDocument::Compact),
                body.isEmpty() ? QByteArray{} : QByteArray("application/json"), headers);
    QJsonParseError e;
    auto d = QJsonDocument::fromJson(bytes, &e);
    require(e.error == QJsonParseError::NoError && d.isObject(),
            "Invalid JSON response from " + QUrl(url).host());
    return d.object();
}
QJsonArray Http::array(const QString &url) {
    QJsonParseError e;
    auto d = QJsonDocument::fromJson(request(url), &e);
    require(e.error == QJsonParseError::NoError && d.isArray(), "Invalid JSON array response");
    return d.array();
}
void Http::download(const QString &url, const QString &path, const QString &hash,
                    QCryptographicHash::Algorithm algorithm) {
    checkCancelled();
    auto valid = [&](const QByteArray &bytes) {
        return hash.isEmpty() || QString::fromLatin1(QCryptographicHash::hash(bytes, algorithm).toHex())
                                         .compare(hash, Qt::CaseInsensitive) == 0;
    };
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.size() > 0 && valid(existing.readAll()))
        return;
    existing.close();
    auto bytes = request(url);
    require(valid(bytes), "Integrity check failed for " + QFileInfo(path).fileName());
    require(QDir().mkpath(QFileInfo(path).absolutePath()), "Cannot create download directory");
    const auto temp = path + ".download-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto cleanup = qScopeGuard([&] { QFile::remove(temp); });
    QSaveFile f(temp);
    require(f.open(QIODevice::WriteOnly), "Cannot write download");
    require(f.write(bytes) == bytes.size() && f.commit(), "Cannot save download");
    checkCancelled();
    publishFile(temp, path);
}
} // namespace neko
