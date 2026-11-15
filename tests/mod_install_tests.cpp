#include "mod_install.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <iostream>
using namespace neko;
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    require(value, message);
}
template <class F> void rejects(F function, const char *message) {
    bool failed = false;
    try {
        function();
    } catch (const Error &) {
        failed = true;
    }
    check(failed, message);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        const auto root = temporary.path();
        Json instance{{"id", "mods-test"},
                      {"name", "Mod test"},
                      {"version", "1.21.1"},
                      {"loader", "Fabric"},
                      {"favorite", true}};
        const QByteArray bytes = "synthetic mod content";
        const auto sha =
            QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
        auto release = [&](QString project, QString id, QString name,
                           QJsonArray dependencies = QJsonArray{}) {
            return Json{{"project_id", project},
                        {"id", id},
                        {"loaders", QJsonArray{"fabric"}},
                        {"game_versions", QJsonArray{"1.21.1"}},
                        {"dependencies", dependencies},
                        {"files", QJsonArray{Json{{"primary", true},
                                                  {"filename", name},
                                                  {"url", "https://example.com/" + name},
                                                  {"hashes", Json{{"sha1", sha}}}}}}};
        };
        auto primary =
            release("main", "vmain", "main.jar",
                    {Json{{"dependency_type", "required"}, {"project_id", "dep"}, {"version_id", "vdep"}},
                     Json{{"dependency_type", "optional"}, {"project_id", "optional"}}});
        auto dependency = release("dep", "vdep", "dep.jar",
                                  {Json{{"dependency_type", "required"}, {"project_id", "main"}}});
        ModMetadata fetch = [&](const QString &address) -> QJsonValue {
            QUrl url(address);
            if (url.path() == "/v2/project/main/version")
                return QJsonArray{primary};
            if (url.path() == "/v2/version/vdep")
                return dependency;
            if (url.path() == "/v2/project/main")
                return Json{{"project_type", "mod"}, {"title", "Main"}};
            if (url.path() == "/v2/project/dep")
                return Json{{"project_type", "mod"}, {"title", "Dependency"}};
            throw Error(("Unexpected dependency request: " + address).toStdString());
        };
        const Json project{{"project_id", "main"}};
        auto plan = resolveMods(instance, project, false, fetch);
        check(plan["files"].toArray().size() == 2,
              "Required dependencies included once; optional skipped and cycle terminated");
        auto invalid = instance;
        invalid["loader"] = "Vanilla";
        rejects([&] { resolveMods(invalid, project, false, fetch); }, "Vanilla mod install accepted");
        auto saved = dependency;
        dependency["loaders"] = QJsonArray{"forge"};
        rejects([&] { resolveMods(instance, project, false, fetch); }, "Wrong loader dependency accepted");
        dependency = saved;
        dependency["game_versions"] = QJsonArray{"1.20.1"};
        rejects([&] { resolveMods(instance, project, false, fetch); }, "Wrong game dependency accepted");
        dependency = saved;
        auto savedPrimary = primary;
        primary["dependencies"] =
            QJsonArray{Json{{"dependency_type", "required"}, {"file_name", "unknown.jar"}}};
        rejects([&] { resolveMods(instance, project, false, fetch); },
                "Unresolvable dependency silently skipped");
        primary = savedPrimary;
        const auto dir = root + "/instances/mods-test";
        writeJson(dir + "/instance.json", instance);
        auto download = [&](const Json &, const QString &target) {
            QFile file(target);
            require(file.open(QIODevice::WriteOnly), "fixture write");
            file.write(bytes);
        };
        auto updated = installResolvedMods(root, instance, plan, download);
        check(QFileInfo(dir + "/mods/main.jar").exists() && QFileInfo(dir + "/mods/dep.jar").exists(),
              "Downloaded mods committed");
        check(updated["favorite"].toBool() && updated["managedMods"].toArray().size() == 2,
              "Configuration preserved and receipts saved");
        int downloads = 0;
        installResolvedMods(root, instance, plan, [&](const Json &, const QString &) { ++downloads; });
        check(downloads == 0, "Matching existing files should be reused");
        QFile::rename(dir + "/mods/dep.jar", dir + "/mods/dep.jar.disabled");
        rejects([&] { installResolvedMods(root, instance, plan, download); },
                "Disabled dependency duplicated");
        QFile::rename(dir + "/mods/dep.jar.disabled", dir + "/mods/dep.jar");
        auto bad = plan;
        auto files = bad["files"].toArray();
        auto file = files[0].toObject();
        file["fileName"] = "../evil.jar";
        files[0] = file;
        bad["files"] = files;
        rejects([&] { installResolvedMods(root, instance, bad, download); }, "Path traversal accepted");
        auto other = instance;
        other["version"] = "1.20.1";
        writeJson(dir + "/instance.json", other);
        rejects([&] { installResolvedMods(root, instance, plan, download); }, "Stale plan accepted");
        writeJson(dir + "/instance.json", instance);
        QFile::remove(dir + "/mods/main.jar");
        QFile::remove(dir + "/mods/dep.jar");
        rejects(
            [&] {
                installResolvedMods(root, instance, plan, [](const Json &, const QString &target) {
                    QFile file(target);
                    require(file.open(QIODevice::WriteOnly), "fixture write");
                    file.write("bad");
                });
            },
            "Wrong checksum accepted");
        check(QDir(dir + "/mods").entryList(QDir::Files).isEmpty(), "Bad download changed mods");
        int checkpoints = 0;
        rejects(
            [&] {
                installResolvedMods(root, instance, plan, download, [&] {
                    if (++checkpoints == 5)
                        throw Error("cancelled");
                });
            },
            "Cancellation ignored");
        check(QDir(dir + "/mods").entryList(QDir::Files).isEmpty(), "Cancelled commit not rolled back");
        check(readJson(dir + "/instance.json") == instance, "Cancelled operation changed preferences");
        auto manual = plan;
        files = plan["files"].toArray();
        file = files[0].toObject();
        file["manual"] = true;
        file["pageUrl"] = "https://www.curseforge.com/minecraft/mc-mods/test/files/1";
        files[0] = file;
        manual["files"] = files;
        updated = installResolvedMods(root, instance, manual, download);
        check(updated["pendingDownloads"].toArray().size() == 1 &&
                  !QFileInfo(dir + "/mods/main.jar").exists(),
              "Restricted file must remain pending");
        auto completed = Engine(root).completeDownloads(updated, {dir + "/mods/dep.jar"});
        check(completed["pendingDownloads"].toArray().isEmpty() && QFileInfo(dir + "/mods/main.jar").exists(),
              "Manual mod completion verifies and installs");
        auto metadataJar = [&](QString id, QString fileName) {
            const auto path = root + "/" + fileName;
            auto *writer = archive_write_new();
            archive_write_set_format_zip(writer);
            require(archive_write_open_filename(writer, QFile::encodeName(path).constData()) == ARCHIVE_OK,
                    "fixture archive open");
            auto *entry = archive_entry_new();
            const QByteArray metadata =
                ("[[mods]]\nmodId = \"" + id + "\"\n[[dependencies." + id + "]]\nmodId = \"minecraft\"\n")
                    .toUtf8();
            archive_entry_set_pathname(entry, "META-INF/neoforge.mods.toml");
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);
            archive_entry_set_size(entry, metadata.size());
            require(archive_write_header(writer, entry) == ARCHIVE_OK, "fixture archive entry");
            archive_write_data(writer, metadata.constData(), metadata.size());
            archive_entry_free(entry);
            archive_write_free(writer);
            QFile file(path);
            require(file.open(QIODevice::ReadOnly), "fixture archive read");
            return file.readAll();
        };
        const auto jarA = metadataJar("original", "original.jar");
        const auto jarB = metadataJar("other", "other.jar");
        auto archivePlan = plan;
        auto archiveFile = plan["files"].toArray().first().toObject();
        archiveFile["fileName"] = "new-name.jar";
        archiveFile["project"] = "archive-project";
        archiveFile["sha1"] =
            QString::fromLatin1(QCryptographicHash::hash(jarA, QCryptographicHash::Sha1).toHex());
        archivePlan["files"] = QJsonArray{archiveFile};
        QFile old(dir + "/mods/original.jar");
        require(old.open(QIODevice::WriteOnly), "fixture mod write");
        old.write(jarA);
        old.close();
        auto archiveDownload = [&](const Json &, const QString &target) {
            QFile file(target);
            require(file.open(QIODevice::WriteOnly), "fixture write");
            file.write(jarA);
        };
        rejects([&] { installResolvedMods(root, instance, archivePlan, archiveDownload); },
                "Duplicate mod ID under a new filename accepted");
        check(!QFileInfo(dir + "/mods/new-name.jar").exists(), "Duplicate rejection changed mods");
        archiveFile["sha1"] =
            QString::fromLatin1(QCryptographicHash::hash(jarB, QCryptographicHash::Sha1).toHex());
        archivePlan["files"] = QJsonArray{archiveFile};
        installResolvedMods(root, instance, archivePlan, [&](const Json &, const QString &target) {
            QFile file(target);
            require(file.open(QIODevice::WriteOnly), "fixture write");
            file.write(jarB);
        });
        check(QFileInfo(dir + "/mods/new-name.jar").exists(),
              "Shared Minecraft dependency was mistaken for duplicate mod ID");
        int curseRequests = 0;
        bool wrongCurseLoader = false;
        ModMetadata curseFetch = [&](const QString &address) -> QJsonValue {
            ++curseRequests;
            QUrl url(address);
            const auto parts = url.path().split('/');
            const auto id = parts.value(3);
            if (url.path().endsWith("/files")) {
                QUrlQuery query(url);
                check(query.queryItemValue("modLoaderType") == "4" &&
                          query.queryItemValue("gameVersion") == "1.21.1",
                      "CurseForge request compatibility filters");
                return Json{
                    {"data",
                     QJsonArray{
                         Json{{"id", id.toInt() * 10},
                              {"fileName", id + ".jar"},
                              {"downloadUrl", id == "2" ? "" : "https://example.com/1.jar"},
                              {"gameVersions", QJsonArray{"1.21.1", wrongCurseLoader ? "Forge" : "Fabric"}},
                              {"hashes", QJsonArray{Json{{"algo", 1}, {"value", sha}}}},
                              {"dependencies", id == "1" ? QJsonArray{Json{{"modId", 2}, {"relationType", 3}},
                                                                      Json{{"modId", 3}, {"relationType", 2}}}
                                                         : QJsonArray{}}}}}};
            }
            return Json{
                {"data",
                 Json{{"classId", 6},
                      {"name", "Curse mod " + id},
                      {"links", Json{{"websiteUrl", "https://www.curseforge.com/minecraft/mc-mods/test"}}}}}};
        };
        auto cursePlan = resolveMods(instance, Json{{"id", 1}}, true, curseFetch);
        check(cursePlan["files"].toArray().size() == 2 && curseRequests == 4,
              "CurseForge required dependency resolution");
        check(cursePlan["files"].toArray()[1].toObject()["manual"].toBool(),
              "CurseForge restricted dependency must be manual");
        wrongCurseLoader = true;
        rejects([&] { resolveMods(instance, Json{{"id", 1}}, true, curseFetch); },
                "Wrong CurseForge loader accepted");
        if (app.arguments().contains("--network")) {
            QTemporaryDir live;
            Engine engine(live.path());
            auto response = engine.searchPage("mod menu", false, {},
                                              {{"type", "mod"}, {"loader", "fabric"}, {"version", "1.21.1"}});
            check(!response["hits"].toArray().isEmpty(), "Live mod search empty");
            writeJson(live.path() + "/instances/mods-test/instance.json", instance);
            auto livePlan = engine.resolveModInstall(instance, Json{{"project_id", "mOgUt4GM"}}, false, {});
            check(!livePlan["files"].toArray().isEmpty(), "Live Mod Menu resolution empty");
            auto installed = engine.installModPlan(instance, livePlan);
            check(!installed["managedMods"].toArray().isEmpty(), "Live mod installation failed");
            const auto fixture = qEnvironmentVariable("NEKO_MOD_FIXTURE");
            if (!fixture.isEmpty())
                writeJson(fixture, {{"query", "mod menu"}, {"hits", response["hits"]}});
        }
        std::cout << checks << " mod installation checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
