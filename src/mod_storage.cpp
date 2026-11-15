#include "mod_storage.hpp"
#include "file_ops.hpp"
#include <filesystem>

namespace neko {
namespace {
namespace fs = std::filesystem;
fs::path nativePath(const QString &path) {
#ifdef Q_OS_WIN
    return fs::path(path.toStdWString());
#else
    return fs::path(QFile::encodeName(path).constData());
#endif
}
bool sameFile(const QString &first, const QString &second) {
    std::error_code error;
    return fs::equivalent(nativePath(first), nativePath(second), error) && !error;
}
quint64 links(const QString &path) {
    std::error_code error;
    const auto count = fs::hard_link_count(nativePath(path), error);
    return error ? 0 : count;
}
QString algorithmName(QCryptographicHash::Algorithm algorithm) {
    switch (algorithm) {
    case QCryptographicHash::Sha1:
        return "sha1";
    case QCryptographicHash::Sha256:
        return "sha256";
    case QCryptographicHash::Sha512:
        return "sha512";
    default:
        return {};
    }
}
} // namespace
ModStorage::ModStorage(QString path, Checkpoint callback, Progress report, LinkCreator link)
    : root(QDir(path).absolutePath()), checkpoint(std::move(callback)), progress(std::move(report)),
      createLink(std::move(link)) {
    if (!createLink)
        createLink = [](const QString &source, const QString &target) {
            std::error_code error;
            fs::create_hard_link(nativePath(source), nativePath(target), error);
            return !error;
        };
}
void ModStorage::check() const {
    if (checkpoint)
        checkpoint();
}
QString ModStorage::checkedModPath(const QString &path) const {
    const auto relative = QDir(root).relativeFilePath(QFileInfo(path).absoluteFilePath());
    const auto parts = relative.split('/');
    require(parts.size() >= 4 && parts[0] == "instances" && !parts[1].isEmpty() &&
                (parts[2] == "mods" || parts[2].startsWith(".mods-")) &&
                (path.endsWith(".jar", Qt::CaseInsensitive) ||
                 path.endsWith(".jar.disabled", Qt::CaseInsensitive)),
            "Shared storage accepts instance mod JARs only");
    return safePath(root, relative);
}
bool ModStorage::privateFile(const QString &path) const {
    const auto id = QDir(root).relativeFilePath(path).split('/').value(1);
    const auto metadata = safePath(root, "instances/" + id + "/instance.json");
    return QFile::exists(metadata) && readJson(metadata)["privateMods"].toBool();
}
QString ModStorage::blobPath(const QString &hash) const {
    require(QRegularExpression("^[a-f0-9]{64}$").match(hash).hasMatch(), "Invalid shared object ID");
    return safePath(root, "shared/mods/sha256/" + hash.left(2) + "/" + hash + ".jar");
}
QString ModStorage::indexPath(const QString &hash, QCryptographicHash::Algorithm algorithm) const {
    const auto name = algorithmName(algorithm);
    if (name.isEmpty() ||
        !QRegularExpression(QString("^[a-fA-F0-9]{%1}$").arg(QCryptographicHash::hashLength(algorithm) * 2))
             .match(hash)
             .hasMatch())
        return {};
    return safePath(root, "shared/mods/index/" + name + "/" + hash.toLower() + ".json");
}
QString ModStorage::digest(const QString &path, QCryptographicHash::Algorithm algorithm) const {
    QFile input(path);
    require(input.open(QIODevice::ReadOnly), "Cannot read shared mod: " + QFileInfo(path).fileName());
    QCryptographicHash hash(algorithm);
    while (!input.atEnd()) {
        check();
        const auto bytes = input.read(1024 * 1024);
        require(!bytes.isEmpty(), "Cannot read mod data");
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}
void ModStorage::copyAtomic(const QString &source, const QString &target, bool writable) const {
    QFile input(source);
    require(input.open(QIODevice::ReadOnly), "Cannot read mod for copying");
    require(QDir().mkpath(QFileInfo(target).absolutePath()), "Cannot create mod directory");
    const auto temp = target + ".copy-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto cleanup = qScopeGuard([&] { QFile::remove(temp); });
    QSaveFile output(temp);
    require(output.open(QIODevice::WriteOnly), "Cannot create private mod copy");
    while (!input.atEnd()) {
        check();
        const auto bytes = input.read(1024 * 1024);
        require(!bytes.isEmpty() && output.write(bytes) == bytes.size(), "Cannot copy mod data");
    }
    check();
    input.close();
    require(output.commit(), "Cannot commit mod copy");
    publishFile(temp, target);
    if (writable)
        require(QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::WriteOwner),
                "Cannot make private mod writable");
}
bool ModStorage::replaceWithLink(const QString &source, const QString &target) const {
    if (sameFile(source, target))
        return true;
    const auto temp = target + ".link-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    require(QDir().mkpath(QFileInfo(target).absolutePath()), "Cannot create mod directory");
    if (!createLink(source, temp))
        return false;
    auto cleanup = qScopeGuard([&] { QFile::remove(temp); });
    check();
    publishFile(temp, target);
    return true;
}
void ModStorage::remember(const QString &path, const QString &sha256) {
    for (const auto algorithm : {QCryptographicHash::Sha1, QCryptographicHash::Sha512}) {
        const auto key = indexPath(digest(path, algorithm), algorithm);
        if (QFile::exists(key)) {
            try {
                if (readJson(key)["sha256"].toString() == sha256)
                    continue;
            } catch (const std::exception &) {
            }
        }
        writeJson(key, {{"sha256", sha256}});
    }
}
bool ModStorage::shareFile(const QString &address) {
    check();
    const auto path = checkedModPath(address);
    require(QFileInfo(path).isFile(), "Mod file is missing");
    if (privateFile(path))
        return false;
    require(QDir().mkpath(safePath(root, "shared/mods")), "Cannot create shared mod storage");
    QLockFile lock(safePath(root, "shared/mods/storage.lock"));
    require(lock.tryLock(5000), "Shared mod storage is busy");
    const auto sha256 = digest(path, QCryptographicHash::Sha256);
    const auto blob = blobPath(sha256);
    if (sameFile(path, blob))
        return true;
    bool created = false, keep = false;
    auto discard = qScopeGuard([&] {
        if (created && !keep)
            QFile::remove(blob);
    });
    if (!QFile::exists(blob) || digest(blob, QCryptographicHash::Sha256) != sha256) {
        require(QDir().mkpath(QFileInfo(blob).absolutePath()), "Cannot create shared object directory");
        if (QFile::exists(blob) || !createLink(path, blob))
            copyAtomic(path, blob, false);
        created = true;
        require(digest(blob, QCryptographicHash::Sha256) == sha256,
                "Mod changed while preparing shared storage");
    }
    // All instance references point to immutable binaries. Neko updates by replacement.
    require(
        QFile::setPermissions(blob, QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther),
        "Cannot protect shared mod file");
    require(digest(path, QCryptographicHash::Sha256) == sha256,
            "Mod changed during optimization; retry with Minecraft closed");
    if (!replaceWithLink(blob, path)) {
        if (created && links(blob) == 1)
            QFile::remove(blob);
        return false;
    }
    remember(blob, sha256);
    keep = true;
    return true;
}
void ModStorage::cloneFile(const QString &source, const QString &destination) {
    const auto from = checkedModPath(source), to = checkedModPath(destination);
    require(!QFileInfo::exists(to), "A mod file already exists at the destination");
    if (shareFile(from) && !privateFile(to) && replaceWithLink(from, to))
        return;
    copyAtomic(from, to);
}
void ModStorage::download(Http &http, const QString &url, const QString &address, const QString &hash,
                          QCryptographicHash::Algorithm algorithm) {
    const auto path = checkedModPath(address);
    const auto key = indexPath(hash, algorithm);
    if (!key.isEmpty() && QFile::exists(key)) {
        require(QDir().mkpath(safePath(root, "shared/mods")), "Cannot open shared storage");
        QLockFile lock(safePath(root, "shared/mods/storage.lock"));
        require(lock.tryLock(5000), "Shared mod storage is busy");
        QString blob, sha256;
        try {
            sha256 = readJson(key)["sha256"].toString();
            blob = blobPath(sha256);
        } catch (const std::exception &) {
        }
        if (!blob.isEmpty() && QFileInfo(blob).isFile() &&
            digest(blob, QCryptographicHash::Sha256) == sha256 &&
            digest(blob, algorithm).compare(hash, Qt::CaseInsensitive) == 0) {
            check();
            require(QFile::setPermissions(blob, QFileDevice::ReadOwner | QFileDevice::ReadGroup |
                                                    QFileDevice::ReadOther),
                    "Cannot protect shared mod file");
            if (privateFile(path) || !replaceWithLink(blob, path))
                copyAtomic(blob, path);
            if (progress)
                progress("Reused shared mod: " + QFileInfo(path).fileName());
            return;
        }
    }
    http.download(url, path, hash, algorithm);
    shareFile(path);
}
QStringList ModStorage::instanceFiles(const QString &id) const {
    require(!id.isEmpty() && !id.contains('/') && !id.contains('\\'), "Invalid instance ID");
    const auto folder = safePath(root, "instances/" + id + "/mods");
    QStringList result;
    QDirIterator it(folder, {"*.jar", "*.jar.disabled"}, QDir::Files | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        result << checkedModPath(it.next());
    return result;
}
void ModStorage::optimizeInstance(const QString &id) {
    const auto metadata = safePath(root, "instances/" + id + "/instance.json");
    if (QFile::exists(metadata) && readJson(metadata)["privateMods"].toBool())
        return;
    for (const auto &file : instanceFiles(id)) {
        check();
        if (progress)
            progress("Sharing mod: " + QFileInfo(file).fileName());
        shareFile(file);
    }
}
Json ModStorage::optimizeAll() {
    QDir instances(safePath(root, "instances"));
    for (const auto &id : instances.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks))
        optimizeInstance(id);
    return usage();
}
Json ModStorage::usage() {
    qint64 logical = 0, saved = 0, stored = 0, unused = 0;
    int files = 0, shared = 0, objects = 0;
    QMap<QString, int> references;
    QMap<QString, qint64> sizes;
    QDir instances(safePath(root, "instances"));
    for (const auto &id : instances.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        for (const auto &file : instanceFiles(id)) {
            check();
            ++files;
            const auto size = QFileInfo(file).size();
            logical += size;
            if (links(file) < 2)
                continue;
            const auto hash = digest(file, QCryptographicHash::Sha256), blob = blobPath(hash);
            if (sameFile(file, blob)) {
                ++shared;
                ++references[hash];
                sizes[hash] = size;
            }
        }
    }
    for (auto it = references.begin(); it != references.end(); ++it)
        saved += qMax(0, it.value() - 1) * sizes[it.key()];
    const auto folder = safePath(root, "shared/mods/sha256");
    QDirIterator blobs(folder, {"*.jar"}, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (blobs.hasNext()) {
        check();
        const auto file = blobs.next();
        safePath(root, QDir(root).relativeFilePath(file));
        ++objects;
        stored += QFileInfo(file).size();
        if (links(file) == 1)
            unused += QFileInfo(file).size();
    }
    return {{"files", files},
            {"sharedFiles", shared},
            {"objects", objects},
            {"logicalBytes", double(logical)},
            {"savedBytes", double(saved)},
            {"storeBytes", double(stored)},
            {"unusedBytes", double(unused)}};
}
qint64 ModStorage::cleanUnused() {
    check();
    require(QDir().mkpath(safePath(root, "shared/mods")), "Cannot open shared storage");
    QLockFile lock(safePath(root, "shared/mods/storage.lock"));
    require(lock.tryLock(5000), "Shared mod storage is busy");
    qint64 reclaimed = 0;
    const auto folder = safePath(root, "shared/mods/sha256");
    QDirIterator files(folder, {"*.jar"}, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        check();
        const auto file = files.next();
        safePath(root, QDir(root).relativeFilePath(file));
        if (links(file) != 1)
            continue;
        const auto size = QFileInfo(file).size();
        if (QFile::remove(file))
            reclaimed += size;
    }
    return reclaimed;
}
void ModStorage::makePrivate(const QString &id) {
    const auto files = instanceFiles(id);
    const auto metadata = safePath(root, "instances/" + id + "/instance.json");
    auto instance = readJson(metadata);
    // Keep future imports private even if conversion is interrupted and retried.
    instance["privateMods"] = true;
    writeJson(metadata, instance);
    for (const auto &file : files) {
        check();
        copyAtomic(file, file);
    }
}
} // namespace neko
