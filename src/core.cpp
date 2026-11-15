#include "core.hpp"

namespace neko {
void require(bool ok, const QString &message) {
    if (!ok)
        throw Error(message.toStdString());
}
QString platform() {
#if defined(Q_OS_WIN)
    return "windows";
#elif defined(Q_OS_MACOS)
    return "osx";
#else
    return "linux";
#endif
}
QString architecture() {
    auto a = QSysInfo::currentCpuArchitecture();
    return a == "x86_64" ? "x86_64" : a == "arm64" ? "aarch64" : a;
}
QString safePath(const QString &root, const QString &relative) {
    require(!relative.isEmpty() && !relative.contains('\\') && !relative.contains(':') &&
                !QDir::isAbsolutePath(relative),
            "Unsafe relative path: " + relative);
    auto parts = relative.split('/');
    for (const auto &part : parts)
        require(part != "..", "Path traversal rejected: " + relative);
    auto base = QDir(root).absolutePath();
    auto path = QDir::cleanPath(base + "/" + relative);
    require(path.startsWith(base + "/"), "Path escapes destination");
    QString cursor = base;
    require(!QFileInfo(base).isSymLink(), "Destination is a symlink");
    for (const auto &part : parts) {
        cursor += "/" + part;
        require(!QFileInfo(cursor).isSymLink(), "Symlink path rejected");
    }
    return path;
}
Json readJson(const QString &path) {
    QFile f(path);
    require(f.open(QIODevice::ReadOnly), "Cannot read " + path);
    QJsonParseError e;
    auto doc = QJsonDocument::fromJson(f.readAll(), &e);
    require(e.error == QJsonParseError::NoError && doc.isObject(), "Invalid JSON: " + path);
    return doc.object();
}
void writeJson(const QString &path, const Json &value) {
    require(QDir().mkpath(QFileInfo(path).absolutePath()), "Cannot create directory");
    QSaveFile f(path);
    require(f.open(QIODevice::WriteOnly), "Cannot write " + path);
    auto bytes = QJsonDocument(value).toJson();
    require(f.write(bytes) == bytes.size() && f.commit(), "Cannot save " + path);
}
QString offlineUuid(const QString &name) {
    auto b = QCryptographicHash::hash(("OfflinePlayer:" + name).toUtf8(), QCryptographicHash::Md5);
    b[6] = char((quint8(b[6]) & 0x0f) | 0x30);
    b[8] = char((quint8(b[8]) & 0x3f) | 0x80);
    return QString::fromLatin1(b.toHex());
}
bool allowed(const Json &object, const QString &os, const QString &arch) {
    if (!object.contains("rules"))
        return true;
    bool result = false;
    for (const auto &v : object["rules"].toArray()) {
        auto r = v.toObject(), o = r["os"].toObject();
        bool match = true;
        if (o.contains("name"))
            match &= o["name"].toString() == os;
        if (o.contains("arch"))
            match &= o["arch"].toString() == arch;
        if (o.contains("version"))
            match &= QRegularExpression(o["version"].toString()).match(QSysInfo::kernelVersion()).hasMatch();
        for (const auto &f : r["features"].toObject().keys())
            if (r["features"].toObject()[f].toBool())
                match = false;
        if (match)
            result = r["action"].toString() == "allow";
    }
    return result;
}
Engine::Engine(QString path, Progress callback) : root(std::move(path)), progress(std::move(callback)) {
    QDir().mkpath(root);
}
void Engine::report(const QString &text) {
    if (progress)
        progress(text);
}
} // namespace neko
