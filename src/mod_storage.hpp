#pragma once
#include "core.hpp"

namespace neko {
// Only immutable mod JARs belong here. Worlds, configs and account data stay private.
class ModStorage {
  public:
    using Checkpoint = std::function<void()>;
    using LinkCreator = std::function<bool(const QString &, const QString &)>;
    explicit ModStorage(QString root, Checkpoint checkpoint = {}, Progress progress = {},
                        LinkCreator link = {});
    void cloneFile(const QString &source, const QString &destination);
    bool shareFile(const QString &path);
    void download(Http &http, const QString &url, const QString &path, const QString &hash,
                  QCryptographicHash::Algorithm algorithm = QCryptographicHash::Sha1);
    void optimizeInstance(const QString &id);
    Json optimizeAll();
    Json usage();
    qint64 cleanUnused();
    void makePrivate(const QString &id);

  private:
    QString root;
    Checkpoint checkpoint;
    Progress progress;
    LinkCreator createLink;
    void check() const;
    QString checkedModPath(const QString &path) const;
    bool privateFile(const QString &path) const;
    QString blobPath(const QString &sha256) const;
    QString indexPath(const QString &hash, QCryptographicHash::Algorithm algorithm) const;
    QString digest(const QString &path, QCryptographicHash::Algorithm algorithm) const;
    void copyAtomic(const QString &source, const QString &target, bool writable = true) const;
    bool replaceWithLink(const QString &source, const QString &target) const;
    void remember(const QString &path, const QString &sha256);
    QStringList instanceFiles(const QString &id) const;
};
} // namespace neko
