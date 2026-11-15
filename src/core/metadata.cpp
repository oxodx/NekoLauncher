#include "core.hpp"

namespace neko {
Json Engine::manifest() {
    auto j = http.json("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json");
    writeJson(root + "/cache/manifest.json", j);
    return j;
}
Json Engine::version(const QString &id) {
    auto path = safePath(root, "versions/" + id + "/" + id + ".json");
    if (QFile::exists(path))
        return readJson(path);
    Json m;
    try {
        m = manifest();
    } catch (...) {
        m = readJson(root + "/cache/manifest.json");
    }
    for (const auto &v : m["versions"].toArray())
        if (v.toObject()["id"].toString() == id) {
            auto obj = v.toObject();
            http.download(obj["url"].toString(), path, obj["sha1"].toString());
            return readJson(path);
        }
    throw Error("Version not found in Mojang catalog");
}
void Engine::artifact(const Json &obj, const QString &target) {
    if (obj["url"].toString().isEmpty() && QFileInfo(target).size() > 0) {
        QFile file(target);
        require(file.open(QIODevice::ReadOnly), "Cannot read installed library");
        const auto hash = obj["sha1"].toString();
        require(hash.isEmpty() ||
                    QString::fromLatin1(
                        QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha1).toHex())
                            .compare(hash, Qt::CaseInsensitive) == 0,
                "Installed library checksum mismatch; reinstall the loader: " + target);
        return;
    }
    require(!obj["url"].toString().isEmpty(), "Artifact URL missing for " + target);
    http.download(obj["url"].toString(), target, obj["sha1"].toString());
}
} // namespace neko
