#include "file_ops.hpp"
#include "mod_storage.hpp"
#include <filesystem>
#include <iostream>
using namespace neko;
namespace fs = std::filesystem;
int checks = 0;
fs::path native(const QString &path) {
#ifdef Q_OS_WIN
    return fs::path(path.toStdWString());
#else
    return fs::path(QFile::encodeName(path).constData());
#endif
}
void check(bool value, const char *message) {
    ++checks;
    require(value, message);
}
template <class F> void rejects(F function, const char *message) {
    bool rejected = false;
    try {
        function();
    } catch (const Error &) {
        rejected = true;
    }
    check(rejected, message);
}
void write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    const auto temp = path + ".fixture";
    QSaveFile file(temp);
    require(file.open(QIODevice::WriteOnly), "fixture write");
    require(file.write(bytes) == bytes.size() && file.commit(), "fixture commit");
    publishFile(temp, path);
}
QByteArray read(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "fixture read");
    return file.readAll();
}
bool same(const QString &a, const QString &b) {
    std::error_code error;
    return fs::equivalent(native(a), native(b), error) && !error;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        const auto root = temp.path();
        auto instance = [&](const QString &id) {
            writeJson(root + "/instances/" + id + "/instance.json",
                      {{"id", id}, {"name", id}, {"favorite", true}});
        };
        auto file = [&](const QString &id, const QString &name) {
            return root + "/instances/" + id + "/mods/" + name;
        };
        instance("a");
        instance("b");
        instance("c");
        const QByteArray bytes = "identical immutable mod JAR content";
        const auto a = file("a", "example.jar"), b = file("b", "renamed.jar"), c = file("c", "example.jar");
        write(a, bytes);
        write(b, bytes);
        write(c, "a different loader build with the same filename");
        ModStorage storage(root);
        check(storage.shareFile(a), "First mod should enter shared storage");
        check(storage.shareFile(b), "Duplicate should become shared");
        check(same(a, b), "Identical bytes with different filenames must share one inode");
        check(!QFileInfo(a).isSymLink() && !QFileInfo(b).isSymLink(),
              "Instance mods must remain regular files");
        check(!(QFile::permissions(a) & QFileDevice::WriteOwner), "Shared mod must be read-only");
        check(storage.shareFile(c) && !same(a, c),
              "Different builds with the same filename must stay separate");
        auto usage = storage.usage();
        check(usage["sharedFiles"].toInt() == 3 && qint64(usage["savedBytes"].toDouble()) == bytes.size(),
              "Savings must count only duplicate shared references");
        check(storage.shareFile(a), "Repeated sharing must be idempotent");
        const auto disabled = file("b", "renamed.jar.disabled");
        check(QFile::rename(b, disabled) && same(a, disabled), "Disable should preserve sharing");
        rejects([&] { storage.shareFile(root + "/instances/a/options.txt"); },
                "Mutable settings must not be shared");
        rejects([&] { storage.shareFile(root + "/outside.jar"); }, "Outside files must not enter storage");
        instance("download");
        Http http;
        const auto sha1 =
            QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
        const auto target = file("download", "downloaded.jar");
        storage.download(http, "https://must-not-be-requested.invalid/mod.jar", target, sha1);
        check(same(a, target), "Known checksum should reuse shared storage without requesting the URL");
        const auto sha512 =
            QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha512).toHex());
        storage.download(http, "https://must-not-be-requested.invalid/mod.jar",
                         file("download", "sha512.jar"), sha512, QCryptographicHash::Sha512);
        check(same(a, file("download", "sha512.jar")), "SHA-512 packs should reuse SHA-1-installed content");
        instance("duplicate");
        storage.cloneFile(a, file("duplicate", "clone.jar"));
        check(same(a, file("duplicate", "clone.jar")), "Instance duplication should reuse mod bytes");
        rejects([&] { storage.cloneFile(c, file("duplicate", "clone.jar")); },
                "Clone must preserve existing destination");
        backupInstance(root + "/instances/a", root + "/backup.zip");
        extract(root + "/backup.zip", root + "/restored");
        check(read(root + "/restored/mods/example.jar") == bytes &&
                  !same(a, root + "/restored/mods/example.jar"),
              "Backups must contain independent full bytes");
        write(root + "/override/mods/downloaded.jar", "pack override");
        backupInstance(root + "/override", root + "/override.zip");
        extract(root + "/override.zip", root + "/instances/download");
        check(read(target) == "pack override" && read(a) == bytes,
              "Archive override must replace only one instance reference");
        write(target, "replacement version");
        check(read(a) == bytes && read(target) == "replacement version" && !same(a, target),
              "Atomic updates must detach and leave other instances untouched");
        storage.makePrivate("b");
        check(!same(a, disabled) && read(disabled) == bytes, "Private conversion must detach the instance");
        check(QFile::permissions(disabled) & QFileDevice::WriteOwner, "Private copy must be writable");
        check(readJson(root + "/instances/b/instance.json")["favorite"].toBool() &&
                  readJson(root + "/instances/b/instance.json")["privateMods"].toBool(),
              "Private preference must preserve other settings");
        storage.optimizeInstance("b");
        check(!same(a, disabled), "Optimization must respect private instances");
        storage.download(http, "https://must-not-be-requested.invalid/mod.jar", file("b", "another.jar"),
                         sha1);
        check(read(file("b", "another.jar")) == bytes && !same(a, file("b", "another.jar")),
              "Private instances must copy cached downloads");
        check(storage.cleanUnused() == 0, "Cleanup must retain objects referenced by any instance");
        check(QDir(root + "/instances/a").removeRecursively() &&
                  read(file("duplicate", "clone.jar")) == bytes,
              "Deleting one instance must not break another");
        QDir(root + "/instances/duplicate").removeRecursively();
        QDir(root + "/instances/download").removeRecursively();
        QDir(root + "/instances/c").removeRecursively();
        check(storage.cleanUnused() > 0, "Unreferenced shared objects should be reclaimable");
        check(read(disabled) == bytes && read(root + "/restored/mods/example.jar") == bytes,
              "Cleanup must preserve private copies and restored backups");
        QTemporaryDir fallback;
        const auto fallbackFile = fallback.path() + "/instances/f/mods/f.jar";
        write(fallbackFile, bytes);
        ModStorage unsupported(fallback.path(), {}, {},
                               [](const QString &, const QString &) { return false; });
        check(!unsupported.shareFile(fallbackFile) && read(fallbackFile) == bytes,
              "Unsupported hard links must retain an ordinary copy");
        check(unsupported.usage()["storeBytes"].toDouble() == 0 &&
                  unsupported.usage()["savedBytes"].toDouble() == 0,
              "Copy fallback must not claim savings or retain an unused duplicate");
        QTemporaryDir cancelled;
        const auto cancelledFile = cancelled.path() + "/instances/f/mods/f.jar";
        write(cancelledFile, bytes);
        ModStorage cancelledStorage(cancelled.path(), [] { throw Error("cancelled"); });
        rejects([&] { cancelledStorage.shareFile(cancelledFile); }, "Cancellation must stop sharing");
        check(read(cancelledFile) == bytes, "Cancelled optimization must preserve original data");
#ifndef Q_OS_WIN
        const auto symlinkFile = file("b", "symlink.jar");
        std::error_code linkError;
        fs::create_symlink(native(disabled), native(symlinkFile), linkError);
        check(!linkError, "fixture symlink");
        rejects([&] { storage.shareFile(symlinkFile); }, "External or user symlinks must be rejected");
#endif
        std::cout << checks << " mod storage checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
