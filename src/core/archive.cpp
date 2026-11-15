#include "core.hpp"
#include "file_ops.hpp"
#include <algorithm>
#include <archive.h>
#include <archive_entry.h>

namespace neko {
void extract(const QString &file, const QString &destination, const QString &prefix, bool natives) {
    auto *a = archive_read_new();
    require(a, "Archive allocation failed");
    auto guard = qScopeGuard([&] { archive_read_free(a); });
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);
    require(archive_read_open_filename(a, QFile::encodeName(file).constData(), 65536) == ARCHIVE_OK,
            "Cannot open ZIP archive");
    archive_entry *entry = nullptr;
    qint64 total = 0;
    int status;
    while ((status = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        QString name = QString::fromUtf8(archive_entry_pathname(entry));
        safePath(destination, name);
        require(!archive_entry_symlink(entry) && !archive_entry_hardlink(entry),
                "Archive links are not supported");
        if (!prefix.isEmpty()) {
            if (!name.startsWith(prefix + "/")) {
                archive_read_data_skip(a);
                continue;
            }
            name = name.mid(prefix.size() + 1);
            if (name.isEmpty())
                continue;
        }
        if (natives &&
            (name.startsWith("META-INF/") || !(name.endsWith(".so") || name.endsWith(".dll") ||
                                               name.endsWith(".dylib") || name.endsWith(".jnilib")))) {
            archive_read_data_skip(a);
            continue;
        }
        auto target = safePath(destination, name);
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            require(QDir().mkpath(target), "Cannot create archive directory");
            continue;
        }
        require(archive_entry_filetype(entry) == AE_IFREG, "Unsupported archive entry");
        require(archive_entry_size(entry) >= 0 && archive_entry_size(entry) <= qint64(2) * 1024 * 1024 * 1024,
                "Archive entry too large");
        total += archive_entry_size(entry);
        require(total <= qint64(8) * 1024 * 1024 * 1024, "Archive exceeds 8 GiB extraction limit");
        require(QDir().mkpath(QFileInfo(target).absolutePath()), "Cannot create archive directory");
        const auto temp = target + ".extract-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto cleanup = qScopeGuard([&] { QFile::remove(temp); });
        QSaveFile out(temp);
        require(out.open(QIODevice::WriteOnly), "Cannot extract " + name);
        char buffer[65536];
        la_ssize_t n;
        while ((n = archive_read_data(a, buffer, sizeof(buffer))) > 0)
            require(out.write(buffer, n) == n, "Archive write failed");
        require(n == 0 && out.commit(), "Archive is damaged or disk is full");
        publishFile(temp, target);
    }
    require(status == ARCHIVE_EOF, "Invalid ZIP archive");
}
void backupInstance(const QString &source, const QString &target, std::function<void()> check,
                    const QStringList &includedPrefixes) {
    require(!QFileInfo(target).absoluteFilePath().startsWith(QDir(source).absolutePath() + "/"),
            "Save backups outside the instance directory");
    QSaveFile output(target);
    require(output.open(QIODevice::WriteOnly), "Cannot open backup destination");
    auto *writer = archive_write_new();
    require(writer, "Cannot allocate archive writer");
    auto cleanup = qScopeGuard([&] { archive_write_free(writer); });
    require(archive_write_set_format_zip(writer) == ARCHIVE_OK, "Cannot create ZIP writer");
    auto write = [](archive *, void *data, const void *bytes, size_t count) -> la_ssize_t {
        return static_cast<QSaveFile *>(data)->write(static_cast<const char *>(bytes), qint64(count));
    };
    require(archive_write_open(writer, &output, nullptr, write, nullptr) == ARCHIVE_OK,
            "Cannot start backup");
    const auto scanRoot = includedPrefixes.size() == 1 && includedPrefixes.first().endsWith('/')
                              ? safePath(source, includedPrefixes.first().chopped(1))
                              : source;
    QDirIterator it(scanRoot, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (check)
            check();
        auto file = it.next();
        auto relative = QDir(source).relativeFilePath(file);
        if (!includedPrefixes.isEmpty() &&
            std::none_of(includedPrefixes.begin(), includedPrefixes.end(),
                         [&](const QString &prefix) { return relative.startsWith(prefix); }))
            continue;
        if (relative.startsWith("natives/") || relative.startsWith("logs/") ||
            relative.startsWith(".fabric/"))
            continue;
        QFile input(file);
        require(input.open(QIODevice::ReadOnly), "Cannot read backup file: " + relative);
        auto *entry = archive_entry_new();
        auto release = qScopeGuard([&] { archive_entry_free(entry); });
        auto path = relative.toUtf8();
        archive_entry_set_pathname(entry, path.constData());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_entry_set_size(entry, input.size());
        require(archive_write_header(writer, entry) == ARCHIVE_OK, "Cannot write backup header");
        qint64 copied = 0;
        while (!input.atEnd()) {
            auto bytes = input.read(65536);
            require(!bytes.isEmpty(), "Cannot read backup data");
            require(archive_write_data(writer, bytes.constData(), size_t(bytes.size())) == bytes.size(),
                    "Cannot write backup data");
            copied += bytes.size();
        }
        require(copied == archive_entry_size(entry), "File changed during backup. Stop Minecraft and retry.");
    }
    if (check)
        check();
    require(archive_write_close(writer) == ARCHIVE_OK && output.commit(), "Cannot complete backup");
}

} // namespace neko
