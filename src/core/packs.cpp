#include "core.hpp"
#include "file_ops.hpp"
#include "mod_storage.hpp"

namespace neko {
namespace {
void copyTree(const QString &source, const QString &destination, const std::function<void()> &check = {}) {
    require(QDir(source).exists(), "Import source is missing");
    QDirIterator entries(source, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot |
                                     QDir::NoSymLinks,
                         QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        if (check)
            check();
        const auto path = entries.next();
        const auto relative = QDir(source).relativeFilePath(path);
        const auto target = safePath(destination, relative);
        const QFileInfo info(path);
        if (info.isDir()) {
            require(QDir().mkpath(target), "Cannot create imported directory");
            continue;
        }
        require(info.isFile(), "Unsupported file in import");
        require(QDir().mkpath(QFileInfo(target).absolutePath()), "Cannot create import directory");
        QFile input(path);
        QSaveFile output(target);
        require(input.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly),
                "Cannot import " + relative);
        while (!input.atEnd()) {
            const auto bytes = input.read(65536);
            require(!bytes.isEmpty() && output.write(bytes) == bytes.size(), "Cannot copy " + relative);
        }
        require(output.commit(), "Cannot save " + relative);
    }
}

QString prismDefinition(const QString &directory) {
    QDirIterator files(directory, {"mmc-pack.json"}, QDir::Files | QDir::NoSymLinks,
                       QDirIterator::Subdirectories);
    QStringList matches;
    while (files.hasNext())
        matches.append(files.next());
    require(matches.size() <= 1, "Prism export contains multiple instances");
    return matches.value(0);
}

QString uniqueChild(const QString &parent, QString name) {
    name = name.trimmed();
    require(!name.isEmpty() && !name.contains('/') && !name.contains('\\'), "Invalid imported name");
    auto candidate = safePath(parent, name);
    for (int suffix = 2; QFileInfo::exists(candidate); ++suffix)
        candidate = safePath(parent, name + " (" + QString::number(suffix) + ")");
    return candidate;
}
} // namespace

Json Engine::officialArtwork(Json instance, const Json &project, bool curse) {
    QString url;
    if (curse) {
        url = project["logo"].toObject()["url"].toString();
        instance["provider"] = "curseforge";
        instance["projectId"] = project["id"].toVariant().toString();
    } else {
        auto id = project["project_id"].toString(project["id"].toString());
        require(!id.isEmpty(), "Modrinth project identity missing");
        auto metadata = http.json("https://api.modrinth.com/v2/project/" +
                                  QString::fromLatin1(QUrl::toPercentEncoding(id)));
        require(metadata["project_type"] == "modpack", "Choose a Modrinth modpack project");
        url = metadata["icon_url"].toString();
        for (const auto &image : metadata["gallery"].toArray())
            if (image.toObject()["featured"].toBool())
                url = image.toObject()["url"].toString(url);
        instance["provider"] = "modrinth";
        instance["projectId"] = id;
    }
    require(!url.isEmpty(), "This project has no official artwork");
    const auto dir = safePath(root, "instances/" + instance["id"].toString());
    http.download(url, dir + "/official-artwork.img");
    instance["cover"] = "official-artwork.img";
    instance["artworkUrl"] = url;
    writeJson(dir + "/instance.json", instance);
    return instance;
}

QJsonArray Engine::search(const QString &query, bool curse, const QString &key) {
    return searchPage(query, curse, key, {{"limit", 30}})["hits"].toArray();
}
Json Engine::searchPage(const QString &query, bool curse, const QString &key, const Json &options) {
    QUrl u(curse ? "https://api.curseforge.com/v1/mods/search" : "https://api.modrinth.com/v2/search");
    QUrlQuery q;
    const auto limit = qBound(1, options["limit"].toInt(20), 50);
    const auto offset = qMax(0, options["offset"].toInt());
    const auto version = options["version"].toString();
    const auto sort = options["sort"].toString("relevance");
    const QMap<QString, int> curseLoaders{{"forge", 1}, {"fabric", 4}, {"quilt", 5}, {"neoforge", 6}};
    if (curse) {
        require(!key.isEmpty(), "Add a CurseForge API key in Settings first");
        q.addQueryItem("gameId", "432");
        const QMap<QString, int> curseClasses{{"mod", 6},
                                               {"resourcepack", 12},
                                               {"world", 17},
                                               {"modpack", 4471},
                                               {"shader", 6552}};
        const auto type = options["type"].toString("modpack");
        require(curseClasses.contains(type), "Unsupported CurseForge content type");
        q.addQueryItem("classId", QString::number(curseClasses[type]));
        q.addQueryItem("searchFilter", query);
        q.addQueryItem("pageSize", QString::number(limit));
        q.addQueryItem("index", QString::number(offset));
        q.addQueryItem("sortField", sort == "downloads" ? "6"
                                    : sort == "updated" ? "3"
                                    : sort == "newest"  ? "11"
                                                        : "1");
        q.addQueryItem("sortOrder", "desc");
        if (!version.isEmpty()) {
            q.addQueryItem("gameVersion", version);
            if (options["type"] == "mod" && curseLoaders.contains(options["loader"].toString()))
                q.addQueryItem("modLoaderType", QString::number(curseLoaders[options["loader"].toString()]));
        }
    } else {
        QJsonArray facets{QJsonArray{"project_type:" + options["type"].toString("modpack")}};
        for (const auto &field : {QString("loader"), QString("category"), QString("version")}) {
            const auto value = options[field].toString();
            if (!value.isEmpty())
                facets.append(QJsonArray{(field == "version" ? "versions:" : "categories:") + value});
        }
        q.addQueryItem("query", query);
        q.addQueryItem("facets", QString::fromUtf8(QJsonDocument(facets).toJson(QJsonDocument::Compact)));
        q.addQueryItem("index", sort);
        q.addQueryItem("limit", QString::number(limit));
        q.addQueryItem("offset", QString::number(offset));
    }
    u.setQuery(q);
    const auto response = http.json(u.toString(), {},
                                    curse ? QMap<QByteArray, QByteArray>{{"x-api-key", key.toUtf8()}}
                                          : QMap<QByteArray, QByteArray>{});
    return {{"hits", response[curse ? "data" : "hits"]},
            {"total", curse ? response["pagination"].toObject()["totalCount"] : response["total_hits"]}};
}
Json Engine::projectDetails(const Json &project, bool curse, const QString &key) {
    if (curse) {
        require(!key.isEmpty(), "Add a CurseForge API key in Settings first");
        const auto id = project["id"].toInt();
        require(id > 0, "Invalid CurseForge project ID");
        auto details =
            http.json("https://api.curseforge.com/v1/mods/" + QString::number(id), {},
                      {{"x-api-key", key.toUtf8()}})["data"]
                .toObject();
        require(!details.isEmpty(), "CurseForge returned no project details");
        try {
            details["_descriptionHtml"] =
                http.json("https://api.curseforge.com/v1/mods/" + QString::number(id) + "/description",
                          {}, {{"x-api-key", key.toUtf8()}})["data"];
        } catch (const Error &) {
            http.checkCancelled();
        }
        return details;
    }
    auto id = project["project_id"].toString();
    if (id.isEmpty())
        id = project["id"].toString();
    require(QRegularExpression("^[A-Za-z0-9_-]+$").match(id).hasMatch(),
            "Invalid Modrinth project ID");
    auto details = http.json("https://api.modrinth.com/v2/project/" + id);
    const auto team = details["team"].toString();
    if (!team.isEmpty()) {
        try {
            QJsonArray authors;
            for (const auto &value : http.array("https://api.modrinth.com/v2/team/" + team + "/members")) {
                const auto member = value.toObject();
                if (!member["accepted"].toBool(true))
                    continue;
                const auto user = member["user"].toObject();
                authors.append(user["username"].toString(user["name"].toString()));
            }
            details["_authors"] = authors;
        } catch (const Error &) {
            http.checkCancelled();
        }
    }
    return details;
}

QString Engine::downloadPack(const Json &project, bool curse, const QString &key) {
    QString url, hash;
    if (curse) {
        auto id = QString::number(project["id"].toInt());
        QUrl endpoint("https://api.curseforge.com/v1/mods/" + id + "/files");
        QUrlQuery filters;
        const auto version = project["_search"].toObject()["version"].toString();
        if (!version.isEmpty())
            filters.addQueryItem("gameVersion", version);
        endpoint.setQuery(filters);
        auto files = http.json(endpoint.toString(), {}, {{"x-api-key", key.toUtf8()}})["data"].toArray();
        require(!files.isEmpty(), "No pack files available");
        auto f = files.first().toObject();
        url = f["downloadUrl"].toString();
        for (const auto &h : f["hashes"].toArray())
            if (h.toObject()["algo"].toInt() == 1)
                hash = h.toObject()["value"].toString();
    } else {
        QUrl endpoint("https://api.modrinth.com/v2/project/" + project["project_id"].toString() + "/version");
        QUrlQuery filters;
        for (const auto &field : {QString("version"), QString("loader")}) {
            const auto value = project["_search"].toObject()[field].toString();
            if (!value.isEmpty())
                filters.addQueryItem(
                    field == "version" ? "game_versions" : "loaders",
                    QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)));
        }
        endpoint.setQuery(filters);
        auto versions = http.array(endpoint.toString());
        require(!versions.isEmpty(), "No pack versions available");
        auto files = versions.first().toObject()["files"].toArray();
        require(!files.isEmpty(), "Pack version has no files");
        auto f = files.first().toObject();
        for (const auto &x : files)
            if (x.toObject()["primary"].toBool())
                f = x.toObject();
        url = f["url"].toString();
        hash = f["hashes"].toObject()["sha1"].toString();
    }
    require(!url.isEmpty(), "The author has disabled third-party downloads. Download the pack from its "
                            "website and use Import pack.");
    auto path =
        root + "/cache/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + (curse ? ".zip" : ".mrpack");
    report("Downloading modpack");
    http.download(url, path, hash);
    return path;
}

QString Engine::importWorld(const Json &instance, const QString &archive) {
    const auto id = instance["id"].toString();
    require(!id.isEmpty() && !id.contains('/') && !id.contains('\\'), "Invalid instance ID");
    QTemporaryDir staging;
    require(staging.isValid(), "Cannot stage world import");
    extract(archive, staging.path());
    QStringList levels;
    QDirIterator files(staging.path(), {"level.dat"}, QDir::Files | QDir::NoSymLinks,
                       QDirIterator::Subdirectories);
    while (files.hasNext())
        levels.append(files.next());
    require(levels.size() == 1, levels.isEmpty() ? "Archive contains no Java Edition world (level.dat)"
                                                 : "Archive contains multiple worlds; import them separately");
    const auto source = QFileInfo(levels.first()).absolutePath();
    const auto saves = safePath(root, "instances/" + id + "/saves");
    require(QDir().mkpath(saves), "Cannot create saves directory");
    auto name = QFileInfo(source).fileName();
    if (QDir(source).absolutePath() == QDir(staging.path()).absolutePath())
        name = QFileInfo(archive).completeBaseName();
    const auto target = uniqueChild(saves, name);
    require(QDir().mkpath(target), "Cannot create world directory");
    bool complete = false;
    auto cleanup = qScopeGuard([&] {
        if (!complete)
            QDir(target).removeRecursively();
    });
    copyTree(source, target, [&] { http.checkCancelled(); });
    complete = true;
    report("Imported world " + QFileInfo(target).fileName());
    return target;
}

QString Engine::installContent(const Json &instance, const Json &project, const QString &kind, bool curse,
                               const QString &key) {
    require(QStringList{"world", "resourcepack", "shader"}.contains(kind),
            "Unsupported content type");
    require(kind != "world" || curse, "World projects are available from CurseForge only");
    require(!curse || !key.isEmpty(), "Add a CurseForge API key in Settings first");
    const auto version = instance["version"].toString();
    QString url, hash, filename;
    auto algorithm = QCryptographicHash::Sha1;
    if (curse) {
        const auto id = QString::number(project["id"].toInt());
        require(project["id"].toInt() > 0, "Invalid CurseForge project");
        QUrl endpoint("https://api.curseforge.com/v1/mods/" + id + "/files");
        QUrlQuery filters;
        if (!version.isEmpty())
            filters.addQueryItem("gameVersion", version);
        filters.addQueryItem("pageSize", "50");
        endpoint.setQuery(filters);
        const auto files = http.json(endpoint.toString(), {}, {{"x-api-key", key.toUtf8()}})["data"].toArray();
        require(!files.isEmpty(), "No compatible CurseForge files are available");
        const auto file = files.first().toObject();
        url = file["downloadUrl"].toString();
        filename = file["fileName"].toString();
        QString md5;
        for (const auto &value : file["hashes"].toArray()) {
            const auto item = value.toObject();
            if (item["algo"].toInt() == 1)
                hash = item["value"].toString();
            if (item["algo"].toInt() == 2)
                md5 = item["value"].toString();
        }
        if (hash.isEmpty() && !md5.isEmpty()) {
            hash = md5;
            algorithm = QCryptographicHash::Md5;
        }
        require(!url.isEmpty(), "The author disabled third-party downloads. Open the CurseForge project "
                                "page and import the downloaded ZIP manually.");
    } else {
        const auto id = project["project_id"].toString(project["id"].toString());
        require(!id.isEmpty(), "Invalid Modrinth project");
        QUrl endpoint("https://api.modrinth.com/v2/project/" +
                      QString::fromLatin1(QUrl::toPercentEncoding(id)) + "/version");
        QUrlQuery filters;
        if (!version.isEmpty())
            filters.addQueryItem(
                "game_versions",
                QString::fromUtf8(QJsonDocument(QJsonArray{version}).toJson(QJsonDocument::Compact)));
        endpoint.setQuery(filters);
        const auto releases = http.array(endpoint.toString());
        require(!releases.isEmpty(), "No compatible Modrinth versions are available");
        const auto files = releases.first().toObject()["files"].toArray();
        require(!files.isEmpty(), "Modrinth version has no files");
        auto file = files.first().toObject();
        for (const auto &value : files)
            if (value.toObject()["primary"].toBool())
                file = value.toObject();
        url = file["url"].toString();
        filename = file["filename"].toString();
        const auto hashes = file["hashes"].toObject();
        hash = hashes["sha512"].toString();
        algorithm = QCryptographicHash::Sha512;
        if (hash.isEmpty()) {
            hash = hashes["sha1"].toString();
            algorithm = QCryptographicHash::Sha1;
        }
    }
    require(!filename.isEmpty() && !filename.contains('/') && !filename.contains('\\'),
            "Provider returned an unsafe filename");
    require(filename.endsWith(".zip", Qt::CaseInsensitive), "Content download is not a ZIP archive");
    const auto cache = safePath(root, "cache/content-" + QUuid::createUuid().toString(QUuid::WithoutBraces) +
                                          ".zip");
    auto cleanup = qScopeGuard([&] { QFile::remove(cache); });
    report("Downloading " + filename);
    http.download(url, cache, hash, algorithm);
    if (kind == "world")
        return importWorld(instance, cache);
    const auto child = kind == "shader" ? "shaderpacks" : "resourcepacks";
    const auto directory = safePath(root, "instances/" + instance["id"].toString() + "/" + child);
    require(QDir().mkpath(directory), "Cannot create content directory");
    auto target = uniqueChild(directory, filename);
    require(QFile::rename(cache, target), "Cannot install downloaded content");
    report("Installed " + QFileInfo(target).fileName());
    return target;
}
Json curseDownloadRequirement(const Json &metadata, int projectId, int fileId, const QString &website) {
    const auto name = metadata["fileName"].toString();
    require(!name.isEmpty() && !name.contains('/') && !name.contains('\\'), "Invalid CurseForge filename");
    safePath(QDir::tempPath(), name);
    QString sha1, md5;
    for (const auto &value : metadata["hashes"].toArray()) {
        const auto hash = value.toObject();
        if (hash["algo"].toInt() == 1)
            sha1 = hash["value"].toString();
        if (hash["algo"].toInt() == 2)
            md5 = hash["value"].toString();
    }
    require(QRegularExpression("^[a-fA-F0-9]{40}$").match(sha1).hasMatch() ||
                QRegularExpression("^[a-fA-F0-9]{32}$").match(md5).hasMatch(),
            "CurseForge provided no usable integrity hash for " + name);
    if (!QRegularExpression("^[a-fA-F0-9]{40}$").match(sha1).hasMatch())
        sha1.clear();
    QUrl page(website);
    require(page.scheme() == "https" &&
                (page.host() == "www.curseforge.com" || page.host() == "curseforge.com"),
            "CurseForge provided no official project website for " + name);
    auto path = page.path();
    while (path.endsWith('/'))
        path.chop(1);
    page.setPath(path + "/files/" + QString::number(fileId));
    page.setQuery(QString{});
    page.setFragment(QString{});
    return {{"fileName", name}, {"projectId", projectId},    {"fileId", fileId}, {"sha1", sha1},
            {"md5", md5},       {"pageUrl", page.toString()}};
}
Json Engine::completeDownloads(const Json &snapshot, const QStringList &files) {
    const auto dir = safePath(root, "instances/" + snapshot["id"].toString());
    auto instance = readJson(dir + "/instance.json");
    QJsonArray pending;
    for (const auto &value : instance["pendingDownloads"].toArray()) {
        http.checkCancelled();
        auto required = value.toObject();
        const auto name = required["fileName"].toString();
        require(!name.isEmpty() && !name.contains('/') && !name.contains('\\'), "Invalid pending filename");
        const auto target = safePath(dir, "mods/" + name);
        const auto sha1 = required["sha1"].toString(), md5 = required["md5"].toString();
        require(QRegularExpression("^[a-fA-F0-9]{40}$").match(sha1).hasMatch() ||
                    QRegularExpression("^[a-fA-F0-9]{32}$").match(md5).hasMatch(),
                "Missing download has no valid checksum");
        const bool useSha1 = QRegularExpression("^[a-fA-F0-9]{40}$").match(sha1).hasMatch();
        auto candidates = files;
        candidates.prepend(target);
        QString matched;
        for (const auto &path : candidates) {
            http.checkCancelled();
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly) || !QFileInfo(path).isFile())
                continue;
            QCryptographicHash digest(useSha1 ? QCryptographicHash::Sha1 : QCryptographicHash::Md5);
            if (!digest.addData(&file))
                continue;
            if (QString::fromLatin1(digest.result().toHex())
                    .compare(useSha1 ? sha1 : md5, Qt::CaseInsensitive) == 0) {
                matched = path;
                break;
            }
        }
        if (matched.isEmpty()) {
            pending.append(required);
            continue;
        }
        if (QFileInfo(matched).absoluteFilePath() != QFileInfo(target).absoluteFilePath()) {
            require(QDir().mkpath(QFileInfo(target).absolutePath()), "Cannot create mods directory");
            QFile source(matched);
            const auto temp = target + ".import-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
            auto cleanup = qScopeGuard([&] { QFile::remove(temp); });
            QSaveFile destination(temp);
            require(source.open(QIODevice::ReadOnly) && destination.open(QIODevice::WriteOnly),
                    "Cannot import " + name);
            QCryptographicHash copied(useSha1 ? QCryptographicHash::Sha1 : QCryptographicHash::Md5);
            while (!source.atEnd()) {
                http.checkCancelled();
                auto bytes = source.read(65536);
                copied.addData(bytes);
                require(!bytes.isEmpty() && destination.write(bytes) == bytes.size(), "Cannot copy " + name);
            }
            require(QString::fromLatin1(copied.result().toHex())
                            .compare(useSha1 ? sha1 : md5, Qt::CaseInsensitive) == 0,
                    "Downloaded file changed while copying " + name);
            source.close();
            require(destination.commit(), "Cannot save " + name);
            http.checkCancelled();
            publishFile(temp, target);
        }
        ModStorage(root, [&] { http.checkCancelled(); }, progress).shareFile(target);
        report("Verified manual download: " + name);
    }
    if (pending.isEmpty())
        instance.remove("pendingDownloads");
    else
        instance["pendingDownloads"] = pending;
    writeJson(dir + "/instance.json", instance);
    return instance;
}

Json Engine::importPack(const QString &archive, const QString &key) {
    QTemporaryDir temp;
    require(temp.isValid(), "Cannot create temporary directory");
    report("Inspecting modpack");
    extract(archive, temp.path());
    const auto prismPack = prismDefinition(temp.path());
    if (!prismPack.isEmpty()) {
        const auto prismRoot = QFileInfo(prismPack).absolutePath();
        const auto minecraft = QDir(prismRoot + "/.minecraft").exists()
                                   ? prismRoot + "/.minecraft"
                                   : prismRoot + "/minecraft";
        require(QDir(minecraft).exists(), "Prism export has no .minecraft directory");
        const auto definition = readJson(prismPack);
        require(definition["formatVersion"].toInt() >= 1, "Unsupported Prism export format");
        QString game, loader = "Vanilla", loaderVersion;
        const QMap<QString, QString> loaders{{"net.fabricmc.fabric-loader", "Fabric"},
                                              {"org.quiltmc.quilt-loader", "Quilt"},
                                              {"net.minecraftforge", "Forge"},
                                              {"net.neoforged", "NeoForge"}};
        for (const auto &value : definition["components"].toArray()) {
            const auto component = value.toObject();
            const auto uid = component["uid"].toString();
            if (uid == "net.minecraft")
                game = component["version"].toString();
            if (!loaders.contains(uid))
                continue;
            require(loader == "Vanilla", "Prism export specifies conflicting mod loaders");
            loader = loaders[uid];
            loaderVersion = component["version"].toString();
        }
        require(!game.isEmpty(), "Prism export is missing its Minecraft component");
        QString name = QFileInfo(prismRoot).fileName();
        const auto configuration = prismRoot + "/instance.cfg";
        if (QFile::exists(configuration)) {
            QSettings settings(configuration, QSettings::IniFormat);
            name = settings.value("name", settings.value("General/name", name)).toString();
        }
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto dir = safePath(root, "instances/" + id);
        require(QDir().mkpath(dir), "Cannot create Prism instance");
        bool done = false;
        auto cleanup = qScopeGuard([&] {
            if (!done)
                QDir(dir).removeRecursively();
        });
        copyTree(minecraft, dir, [&] { http.checkCancelled(); });
        Json instance{{"id", id},
                      {"name", name.trimmed().isEmpty() ? QString("Imported Prism instance") : name.trimmed()},
                      {"version", game},
                      {"loader", loader},
                      {"loaderVersion", loaderVersion},
                      {"memory", 4096},
                      {"minMemory", 512},
                      {"java", "java"}};
        writeJson(dir + "/instance.json", instance);
        ModStorage(root, [&] { http.checkCancelled(); }, progress).optimizeInstance(id);
        done = true;
        report("Imported Prism Launcher instance");
        return instance;
    }
    if (QFile::exists(temp.path() + "/instance.json") &&
        !QFile::exists(temp.path() + "/modrinth.index.json") &&
        !QFile::exists(temp.path() + "/manifest.json")) {
        auto instance = readJson(temp.path() + "/instance.json");
        require(!instance["version"].toString().isEmpty(), "Backup has no Minecraft version");
        instance["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        instance["name"] = instance["name"].toString("Restored instance") + " (restored)";
        const auto dir = safePath(root, "instances/" + instance["id"].toString());
        bool done = false;
        auto cleanup = qScopeGuard([&] {
            if (!done)
                QDir(dir).removeRecursively();
        });
        extract(archive, dir);
        http.checkCancelled();
        writeJson(dir + "/instance.json", instance);
        ModStorage(
            root, [&] { http.checkCancelled(); }, progress)
            .optimizeInstance(instance["id"].toString());
        done = true;
        return instance;
    }
    bool mr = QFile::exists(temp.path() + "/modrinth.index.json");
    auto m = readJson(temp.path() + (mr ? "/modrinth.index.json" : "/manifest.json"));
    if (mr)
        require(m["formatVersion"].toInt() == 1 && m["game"].toString() == "minecraft",
                "Unsupported Modrinth pack format");
    else
        require(m["manifestType"].toString() == "minecraftModpack" && m["manifestVersion"].toInt() == 1,
                "Unsupported CurseForge pack format");
    auto deps = m["dependencies"].toObject();
    QString game = mr ? deps["minecraft"].toString() : m["minecraft"].toObject()["version"].toString();
    QString loader = "Vanilla", lv;
    if (mr) {
        for (auto it = deps.begin(); it != deps.end(); ++it) {
            if (it.key() == "fabric-loader") {
                require(loader == "Vanilla", "Pack specifies conflicting loaders");
                loader = "Fabric";
                lv = it.value().toString();
            } else if (it.key() == "quilt-loader") {
                require(loader == "Vanilla", "Pack specifies conflicting loaders");
                loader = "Quilt";
                lv = it.value().toString();
            } else if (it.key() == "forge" || it.key() == "neoforge") {
                require(loader == "Vanilla", "Pack specifies conflicting loaders");
                loader = it.key() == "forge" ? "Forge" : "NeoForge";
                lv = it.value().toString();
            } else
                require(it.key() == "minecraft",
                        "This pack requires " + it.key() + ". This loader is unsupported.");
        }
    } else {
        for (const auto &l : m["minecraft"].toObject()["modLoaders"].toArray()) {
            require(loader == "Vanilla", "Pack specifies conflicting loaders");
            auto id = l.toObject()["id"].toString();
            if (id.startsWith("fabric-")) {
                loader = "Fabric";
                lv = id.mid(7);
            } else if (id.startsWith("quilt-")) {
                loader = "Quilt";
                lv = id.mid(6);
            } else if (id.startsWith("forge-") || id.startsWith("neoforge-")) {
                loader = id.startsWith("neoforge-") ? "NeoForge" : "Forge";
                lv = id.mid(loader == "Forge" ? 6 : 9);
            } else
                throw Error("Unsupported mod loader in CurseForge pack");
        }
    }
    require(!game.isEmpty(), "Pack is missing its Minecraft version");
    auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto dir = safePath(root, "instances/" + id);
    require(QDir().mkpath(dir), "Cannot create instance");
    bool done = false;
    auto cleanup = qScopeGuard([&] {
        if (!done)
            QDir(dir).removeRecursively();
    });
    QJsonArray pendingDownloads;
    int count = 0;
    for (const auto &item : m["files"].toArray()) {
        auto f = item.toObject();
        report(QString("Pack file %1 / %2").arg(++count).arg(m["files"].toArray().size()));
        if (mr) {
            if (f["env"].toObject()["client"].toString() == "unsupported")
                continue;
            auto urls = f["downloads"].toArray();
            require(!urls.isEmpty(), "Pack file has no download URL");
            auto hashes = f["hashes"].toObject();
            auto hash = hashes["sha512"].toString();
            auto algo = QCryptographicHash::Sha512;
            if (hash.isEmpty()) {
                hash = hashes["sha1"].toString();
                algo = QCryptographicHash::Sha1;
            }
            require(!hash.isEmpty(), "Pack file has no integrity hash");
            const auto relative = f["path"].toString();
            const auto target = safePath(dir, relative);
            if (relative.startsWith("mods/") && relative.endsWith(".jar", Qt::CaseInsensitive))
                ModStorage(
                    root, [&] { http.checkCancelled(); }, progress)
                    .download(http, urls.first().toString(), target, hash, algo);
            else
                http.download(urls.first().toString(), target, hash, algo);
        } else {
            require(!key.isEmpty(), "CurseForge import requires your API key in Settings");
            auto meta = http.json(QString("https://api.curseforge.com/v1/mods/%1/files/%2")
                                      .arg(f["projectID"].toInt())
                                      .arg(f["fileID"].toInt()),
                                  {}, {{"x-api-key", key.toUtf8()}})["data"]
                            .toObject();
            auto url = meta["downloadUrl"].toString();
            if (url.isEmpty()) {
                auto project =
                    http.json(QString("https://api.curseforge.com/v1/mods/%1").arg(f["projectID"].toInt()),
                              {}, {{"x-api-key", key.toUtf8()}})["data"]
                        .toObject();
                pendingDownloads.append(
                    curseDownloadRequirement(meta, f["projectID"].toInt(), f["fileID"].toInt(),
                                             project["links"].toObject()["websiteUrl"].toString()));
                report("Manual download needed: " + meta["fileName"].toString());
                continue;
            }
            QString hash;
            for (const auto &h : meta["hashes"].toArray())
                if (h.toObject()["algo"].toInt() == 1)
                    hash = h.toObject()["value"].toString();
            ModStorage(
                root, [&] { http.checkCancelled(); }, progress)
                .download(http, url, safePath(dir, "mods/" + meta["fileName"].toString()), hash);
        }
    }
    QString overrides = mr ? "overrides" : m["overrides"].toString("overrides");
    safePath(temp.path(), overrides);
    extract(archive, dir, overrides);
    if (mr)
        extract(archive, dir, "client-overrides");
    Json instance{{"id", id},
                  {"name", m["name"].toString("Imported pack")},
                  {"version", game},
                  {"loader", loader},
                  {"loaderVersion", lv},
                  {"memory", 4096},
                  {"java", "java"}};
    if (!pendingDownloads.isEmpty())
        instance["pendingDownloads"] = pendingDownloads;
    writeJson(dir + "/instance.json", instance);
    ModStorage(root, [&] { http.checkCancelled(); }, progress).optimizeInstance(instance["id"].toString());
    done = true;
    return instance;
}
} // namespace neko
