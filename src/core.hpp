#pragma once
#include <QtCore>
#include <QtNetwork>
#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>

namespace neko {
using Json = QJsonObject;
// Public application identifier; not a client secret or an account token.
inline constexpr auto DefaultMicrosoftClientId = "543cab1d-f3d7-411d-8e7c-4c2bf25a6cf6";
using Progress = std::function<void(const QString &)>;
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};
void require(bool ok, const QString &message);
QString safePath(const QString &root, const QString &relative);
Json readJson(const QString &path);
void writeJson(const QString &path, const Json &value);
QString microsoftAuthError(const Json &response);
QString offlineUuid(const QString &name);
bool allowed(const Json &object, const QString &os, const QString &arch);
QString platform();
QString architecture();
void backupInstance(const QString &source, const QString &target, std::function<void()> check = {},
                    const QStringList &includedPrefixes = {});
void extract(const QString &archive, const QString &destination, const QString &prefix = {},
             bool natives = false);

class Http {
    QNetworkAccessManager manager;

  public:
    std::shared_ptr<std::atomic_bool> cancellation;
    void checkCancelled() const;
    QByteArray request(const QString &url, const QByteArray &body = {}, const QByteArray &type = {},
                       const QMap<QByteArray, QByteArray> &headers = {}, bool allowError = false);
    Json json(const QString &url, const Json &body = {}, const QMap<QByteArray, QByteArray> &headers = {});
    QJsonArray array(const QString &url);
    void download(const QString &url, const QString &path, const QString &hash = {},
                  QCryptographicHash::Algorithm algorithm = QCryptographicHash::Sha1);
};
Json curseDownloadRequirement(const Json &metadata, int projectId, int fileId, const QString &website);
struct Account {
    QString name, uuid, token;
    qint64 expires = 0;
    QString refreshToken, clientId;
};
QProcessEnvironment
launchEnvironment(const Json &instance,
                  QProcessEnvironment inherited = QProcessEnvironment::systemEnvironment());
struct JavaRuntime {
    QString executable;
    int major = 0;
};
int javaMajor(const QString &output);
JavaRuntime inspectJava(const QString &executable);
JavaRuntime selectJava(const QString &preferred, int required, bool automatic = true,
                       const QStringList &extraHomes = {});
struct Launch {
    QString java, directory;
    QStringList arguments;
    QProcessEnvironment environment;
};
class Engine {
  public:
    explicit Engine(QString root, Progress progress = {});
    QString root;
    Progress progress;
    Http http;
    Json manifest();
    Json version(const QString &id);
    Json loaderProfile(const QString &game, const QString &loader, const QString &loaderVersion = {});
    Launch prepare(const Json &instance, const Account &account, bool offline);
    Json installForge(const Json &instance, const Json &base);
    Json officialArtwork(Json instance, const Json &project, bool curse);
    Account refresh(const Account &account);
    Json completeDownloads(const Json &instance, const QStringList &files);
    Json importPack(const QString &archive, const QString &curseKey);
    QJsonArray search(const QString &query, bool curse, const QString &key);
    Json searchPage(const QString &query, bool curse, const QString &key, const Json &options);
    Json resolveModInstall(const Json &instance, const Json &project, bool curse, const QString &key);
    Json installModPlan(const Json &instance, const Json &plan);
    QString downloadPack(const Json &project, bool curse, const QString &key);
    QString installContent(const Json &instance, const Json &project, const QString &kind, bool curse,
                           const QString &key);
    QString importWorld(const Json &instance, const QString &archive);
    Account authenticate(const QString &clientId, std::function<void(QString, QString)> code);

  private:
    Account minecraftAccount(const QString &access, const QString &refresh, const QString &clientId);
    void report(const QString &message);
    void artifact(const Json &object, const QString &target);
};
} // namespace neko
