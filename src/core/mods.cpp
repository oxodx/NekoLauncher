#include "mod_install.hpp"
#include "mod_storage.hpp"
#include <archive.h>
#include <archive_entry.h>

namespace neko {
namespace {
QString hashFile(const QString &path, QCryptographicHash::Algorithm algorithm) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read mod file: " + path);
    QCryptographicHash digest(algorithm);
    require(digest.addData(&file), "Cannot verify mod file");
    return QString::fromLatin1(digest.result().toHex());
}
QSet<QString> modIdentifiers(const QString &path) {
    QSet<QString> ids;
    auto *reader = archive_read_new();
    auto cleanup = qScopeGuard([&] { archive_read_free(reader); });
    archive_read_support_format_zip(reader);
    if (archive_read_open_filename(reader, QFile::encodeName(path).constData(), 65536) != ARCHIVE_OK)
        return ids;
    archive_entry *entry = nullptr;
    while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
        const auto name = QString::fromUtf8(archive_entry_pathname(entry));
        if (!QStringList{"fabric.mod.json", "quilt.mod.json", "META-INF/mods.toml",
                         "META-INF/neoforge.mods.toml"}
                 .contains(name)) {
            archive_read_data_skip(reader);
            continue;
        }
        const auto size = archive_entry_size(entry);
        if (size <= 0 || size > 262144) {
            archive_read_data_skip(reader);
            continue;
        }
        QByteArray bytes(int(size), '\0');
        if (archive_read_data(reader, bytes.data(), size) != size)
            continue;
        if (name.endsWith(".json")) {
            const auto metadata = QJsonDocument::fromJson(bytes).object();
            const auto id = name.startsWith("quilt") ? metadata["quilt_loader"].toObject()["id"].toString()
                                                     : metadata["id"].toString();
            if (!id.isEmpty())
                ids.insert(id);
        } else {
            const QRegularExpression pattern(R"(\bmodId\s*=\s*["']([A-Za-z0-9_-]+)["'])");
            bool modSection = false;
            for (const auto &line : QString::fromUtf8(bytes).split('\n')) {
                const auto trimmed = line.trimmed();
                if (trimmed.startsWith('['))
                    modSection = trimmed == "[[mods]]";
                if (!modSection)
                    continue;
                const auto match = pattern.match(trimmed);
                if (match.hasMatch())
                    ids.insert(match.captured(1));
            }
        }
    }
    return ids;
}
void validateFile(const Json &file) {
    const auto name = file["fileName"].toString();
    require(name.endsWith(".jar", Qt::CaseInsensitive) && !name.contains('/') && !name.contains('\\'),
            "Provider returned an invalid mod filename");
    require(QRegularExpression("^[a-fA-F0-9]{40}$").match(file["sha1"].toString()).hasMatch() ||
                QRegularExpression("^[a-fA-F0-9]{32}$").match(file["md5"].toString()).hasMatch(),
            "Mod download has no usable integrity hash");
    if (file["manual"].toBool()) {
        const QUrl page(file["pageUrl"].toString());
        require(page.scheme() == "https" &&
                    QStringList{"curseforge.com", "www.curseforge.com"}.contains(page.host()),
                "Manual mod download needs an official CurseForge page");
    } else {
        QUrl url(file["url"].toString());
        require(url.scheme() == "https" && !url.host().isEmpty() && url.userInfo().isEmpty(),
                "Invalid mod download URL");
    }
}
} // namespace
Json resolveMods(const Json &instance, const Json &project, bool curse, const ModMetadata &fetch) {
    const auto game = instance["version"].toString(), loader = instance["loader"].toString().toLower();
    require(!game.isEmpty() && QStringList{"fabric", "quilt", "forge", "neoforge"}.contains(loader),
            "Choose a Fabric, Quilt, Forge or NeoForge instance before installing mods.");
    const QMap<QString, int> loaderIds{{"forge", 1}, {"fabric", 4}, {"quilt", 5}, {"neoforge", 6}};
    QMap<QString, QString> resolved;
    QJsonArray files;
    std::function<void(QString, QString)> visit = [&](QString id, QString pinned) {
        require(QRegularExpression("^[A-Za-z0-9_-]+$").match(id).hasMatch(), "Invalid mod project ID");
        require(!curse || id.toInt() > 0, "Invalid CurseForge project ID");
        if (resolved.contains(id)) {
            require(pinned.isEmpty() || pinned == resolved[id],
                    "Dependencies require conflicting versions of project " + id);
            return;
        }
        require(resolved.size() < 64, "Mod dependency tree exceeds 64 projects");
        Json release, file;
        if (curse) {
            require(pinned.isEmpty(), "Unsupported pinned CurseForge dependency");
            QUrlQuery q;
            q.addQueryItem("gameVersion", game);
            q.addQueryItem("modLoaderType", QString::number(loaderIds[loader]));
            q.addQueryItem("pageSize", "50");
            const auto available =
                fetch("https://api.curseforge.com/v1/mods/" + id + "/files?" + q.toString(QUrl::FullyEncoded))
                    .toObject()["data"]
                    .toArray();
            for (const auto &value : available) {
                const auto candidate = value.toObject();
                if (candidate["gameVersions"].toArray().contains(game) &&
                    candidate["isAvailable"].toBool(true)) {
                    release = candidate;
                    break;
                }
            }
            require(!release.isEmpty(),
                    "No compatible mod file for project " + id + " on " + game + " / " + loader);
            bool compatibleLoader = false;
            for (const auto &tag : release["gameVersions"].toArray())
                if (tag.toString().compare(loader, Qt::CaseInsensitive) == 0)
                    compatibleLoader = true;
            require(compatibleLoader, "CurseForge returned a file for a different loader");
            const auto metadata =
                fetch("https://api.curseforge.com/v1/mods/" + id).toObject()["data"].toObject();
            require(metadata["classId"].toInt() == 6, "Dependency is not a Minecraft mod");
            file = curseDownloadRequirement(release, id.toInt(), release["id"].toInt(),
                                            metadata["links"].toObject()["websiteUrl"].toString());
            file["url"] = release["downloadUrl"];
            file["manual"] = release["downloadUrl"].toString().isEmpty();
            file["project"] = id;
            file["release"] = QString::number(release["id"].toInt());
            file["title"] = metadata["name"];
        } else {
            if (!pinned.isEmpty()) {
                require(QRegularExpression("^[A-Za-z0-9]+$").match(pinned).hasMatch(),
                        "Invalid pinned mod version");
                release = fetch("https://api.modrinth.com/v2/version/" + pinned).toObject();
            } else {
                QUrlQuery q;
                q.addQueryItem(
                    "game_versions",
                    QString::fromUtf8(QJsonDocument(QJsonArray{game}).toJson(QJsonDocument::Compact)));
                q.addQueryItem(
                    "loaders",
                    QString::fromUtf8(QJsonDocument(QJsonArray{loader}).toJson(QJsonDocument::Compact)));
                const auto available = fetch("https://api.modrinth.com/v2/project/" + id + "/version?" +
                                             q.toString(QUrl::FullyEncoded))
                                           .toArray();
                require(!available.isEmpty(),
                        "No compatible release for project " + id + " on " + game + " / " + loader);
                release = available.first().toObject();
            }
            require(release["project_id"].toString() == id &&
                        release["game_versions"].toArray().contains(game) &&
                        release["loaders"].toArray().contains(loader),
                    "Dependency release does not match the instance");
            const auto metadata = fetch("https://api.modrinth.com/v2/project/" + id).toObject();
            require(metadata["project_type"] == "mod", "Dependency is not a Minecraft mod");
            Json artifact;
            for (const auto &value : release["files"].toArray()) {
                auto candidate = value.toObject();
                if (candidate["filename"].toString().endsWith(".jar", Qt::CaseInsensitive) &&
                    (artifact.isEmpty() || candidate["primary"].toBool()))
                    artifact = candidate;
            }
            file = {{"fileName", artifact["filename"]},
                    {"url", artifact["url"]},
                    {"sha1", artifact["hashes"].toObject()["sha1"]},
                    {"project", id},
                    {"release", release["id"]},
                    {"title", metadata["title"]}};
        }
        validateFile(file);
        resolved[id] = file["release"].toString(); // Mark before traversal to terminate cycles.
        file["provider"] = curse ? "curseforge" : "modrinth";
        files.append(file);
        for (const auto &value : release["dependencies"].toArray()) {
            const auto dependency = value.toObject();
            if (curse ? dependency["relationType"].toInt() != 3 : dependency["dependency_type"] != "required")
                continue;
            auto child =
                curse ? QString::number(dependency["modId"].toInt()) : dependency["project_id"].toString();
            const auto pinnedVersion = curse ? QString{} : dependency["version_id"].toString();
            if (child.isEmpty() && !pinnedVersion.isEmpty())
                child = fetch("https://api.modrinth.com/v2/version/" + pinnedVersion)
                            .toObject()["project_id"]
                            .toString();
            require(!child.isEmpty(),
                    "A required dependency has no downloadable project. Install it manually.");
            visit(child, pinnedVersion);
        }
    };
    visit(curse ? QString::number(project["id"].toInt()) : project["project_id"].toString(), {});
    return {{"instanceId", instance["id"]}, {"version", game}, {"loader", loader}, {"files", files}};
}
Json Engine::resolveModInstall(const Json &instance, const Json &project, bool curse, const QString &key) {
    require(!curse || !key.isEmpty(), "Add a CurseForge API key in Settings first");
    return resolveMods(instance, project, curse, [&](const QString &url) -> QJsonValue {
        http.checkCancelled();
        const auto bytes = http.request(url, {}, {},
                                        curse ? QMap<QByteArray, QByteArray>{{"x-api-key", key.toUtf8()}}
                                              : QMap<QByteArray, QByteArray>{});
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        require(error.error == QJsonParseError::NoError, "Invalid provider metadata");
        return document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object());
    });
}
Json Engine::installModPlan(const Json &snapshot, const Json &plan) {
    return installResolvedMods(
        root, snapshot, plan,
        [&](const Json &file, const QString &target) {
            report("Downloading mod: " + file["fileName"].toString());
            ModStorage(
                root, [&] { http.checkCancelled(); }, progress)
                .download(http, file["url"].toString(), target, file["sha1"].toString());
        },
        [&] { http.checkCancelled(); });
}
Json installResolvedMods(const QString &root, const Json &snapshot, const Json &plan,
                         const ModDownload &download, const std::function<void()> &checkpoint) {
    const auto dir = safePath(root, "instances/" + snapshot["id"].toString());
    auto instance = readJson(dir + "/instance.json");
    require(plan["instanceId"] == instance["id"] && plan["version"] == instance["version"] &&
                plan["loader"].toString() == instance["loader"].toString().toLower(),
            "Instance components changed; search for the mod again");
    const auto files = plan["files"].toArray();
    require(!files.isEmpty() && files.size() <= 64, "Invalid mod installation plan");
    QLockFile lock(dir + "/.mod-install.lock");
    require(lock.tryLock(0), "Another mod installation is running for this instance");
    QTemporaryDir staging(dir + "/.mods-XXXXXX");
    require(staging.isValid(), "Cannot create mod staging directory");
    auto pending = instance["pendingDownloads"].toArray();
    QJsonArray managed;
    for (const auto &value : instance["managedMods"].toArray()) {
        const auto receipt = value.toObject();
        const auto existing = safePath(dir, "mods/" + receipt["fileName"].toString());
        bool waiting = false;
        for (const auto &item : pending)
            if (item.toObject()["fileName"] == receipt["fileName"])
                waiting = true;
        if (QFileInfo::exists(existing) || QFileInfo::exists(existing + ".disabled") || waiting)
            managed.append(receipt);
    }
    QMap<QString, QString> existingIds;
    QDir mods(dir + "/mods");
    for (const auto &name : mods.entryList({"*.jar", "*.jar.disabled"}, QDir::Files | QDir::NoSymLinks)) {
        checkpoint();
        for (const auto &id : modIdentifiers(safePath(mods.path(), name)))
            existingIds[id] = name;
    }
    QSet<QString> names;
    QStringList staged;
    for (const auto &value : files) {
        checkpoint();
        const auto file = value.toObject();
        validateFile(file);
        const auto name = file["fileName"].toString();
        require(!names.contains(name), "Two mods use the same filename: " + name);
        names.insert(name);
        const auto target = safePath(dir, "mods/" + name);
        require(!QFileInfo::exists(target + ".disabled"),
                "A disabled copy exists. Enable or remove it first: " + name);
        for (const auto &saved : managed) {
            const auto previous = saved.toObject();
            if (previous["provider"] == file["provider"] && previous["project"] == file["project"])
                require(
                    previous["release"] == file["release"],
                    "Another version is already managed. Remove it before installing a different release: " +
                        name);
        }
        const bool sha1 = QRegularExpression("^[a-fA-F0-9]{40}$").match(file["sha1"].toString()).hasMatch();
        const auto checksum = file[sha1 ? "sha1" : "md5"].toString();
        if (QFileInfo::exists(target)) {
            require(hashFile(target, sha1 ? QCryptographicHash::Sha1 : QCryptographicHash::Md5)
                            .compare(checksum, Qt::CaseInsensitive) == 0,
                    "An existing mod has the same filename and different content: " + name);
        } else if (file["manual"].toBool()) {
            bool found = false;
            for (const auto &item : pending)
                if (item.toObject()["fileName"] == file["fileName"]) {
                    require(item.toObject()["sha1"] == file["sha1"] && item.toObject()["md5"] == file["md5"],
                            "Conflicting manual download: " + name);
                    found = true;
                }
            if (!found)
                pending.append(file);
        } else {
            const auto stage = safePath(staging.path(), name);
            download(file, stage);
            require(hashFile(stage, sha1 ? QCryptographicHash::Sha1 : QCryptographicHash::Md5)
                            .compare(checksum, Qt::CaseInsensitive) == 0,
                    "Mod checksum mismatch: " + name);
            for (const auto &id : modIdentifiers(stage)) {
                require(!existingIds.contains(id),
                        "Mod ID " + id + " is already present in " + existingIds.value(id) +
                            ". Remove or update that file before installing " + name);
                existingIds[id] = name;
            }
            staged << name;
        }
        bool recorded = false;
        for (const auto &item : managed)
            if (item.toObject()["provider"] == file["provider"] &&
                item.toObject()["project"] == file["project"])
                recorded = true;
        if (!recorded) {
            auto receipt = file;
            receipt.remove("url");
            managed.append(receipt);
        }
    }
    checkpoint();
    require(QDir().mkpath(dir + "/mods"), "Cannot create mods directory");
    QStringList committed;
    bool complete = false;
    auto rollback = qScopeGuard([&] {
        if (!complete)
            for (const auto &name : committed)
                QFile::remove(dir + "/mods/" + name);
    });
    for (const auto &name : staged) {
        checkpoint();
        require(QFile::rename(staging.filePath(name), safePath(dir, "mods/" + name)),
                "Cannot install mod: " + name);
        committed << name;
    }
    instance["managedMods"] = managed;
    if (!pending.isEmpty())
        instance["pendingDownloads"] = pending;
    writeJson(dir + "/instance.json", instance);
    complete = true;
    return instance;
}
} // namespace neko
