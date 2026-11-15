#include "file_ops.hpp"
#include "core.hpp"
#include <filesystem>
namespace neko {
void publishFile(const QString &source, const QString &destination) {
    require(QFileInfo(source).absolutePath() == QFileInfo(destination).absolutePath() &&
                source != destination,
            "Atomic replacement requires separate files in the same directory");
    require(QFileInfo(source).isFile() && !QFileInfo(source).isSymLink() &&
                !QFileInfo(destination).isSymLink(),
            "Cannot publish a symbolic link");
    require(!QFileInfo::exists(destination) || QFileInfo(destination).isFile(),
            "Destination is not a regular file");
    auto native = [](const QString &path) {
#ifdef Q_OS_WIN
        return std::filesystem::path(path.toStdWString());
#else
        return std::filesystem::path(QFile::encodeName(path).constData());
#endif
    };
    std::error_code error;
    std::filesystem::rename(native(source), native(destination), error);
#ifdef Q_OS_WIN
    if (error && QFileInfo::exists(destination)) {
        const auto previous = destination + ".previous-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        require(QFile::rename(destination, previous), "Cannot prepare file replacement");
        if (!QFile::rename(source, destination)) {
            require(QFile::rename(previous, destination),
                    "Replacement failed; original remains at " + previous);
            throw Error("Cannot publish replacement file");
        }
        QFile::remove(previous);
        return;
    }
#endif
    require(!error, "Cannot publish file: " + QString::fromStdString(error.message()));
}
} // namespace neko
