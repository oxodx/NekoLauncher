#include "core.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <iostream>
using namespace neko;
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *message) {
    bool rejected = false;
    try {
        f();
    } catch (const Error &) {
        rejected = true;
    }
    check(rejected, message);
}
void zip(const QString &file, const QString &name, bool symlink = false) {
    auto *a = archive_write_new();
    archive_write_set_format_zip(a);
    archive_write_open_filename(a, QFile::encodeName(file).constData());
    auto *e = archive_entry_new();
    auto encoded = name.toUtf8();
    archive_entry_set_pathname(e, encoded.constData());
    archive_entry_set_filetype(e, symlink ? AE_IFLNK : AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_entry_set_size(e, symlink ? 0 : 3);
    if (symlink)
        archive_entry_set_symlink(e, "/tmp/outside");
    archive_write_header(a, e);
    if (!symlink)
        archive_write_data(a, "abc", 3);
    archive_entry_free(e);
    archive_write_close(a);
    archive_write_free(a);
}
void zipFiles(const QString &file, const QMap<QString, QByteArray> &files) {
    auto *a = archive_write_new();
    archive_write_set_format_zip(a);
    archive_write_open_filename(a, QFile::encodeName(file).constData());
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        auto *e = archive_entry_new();
        const auto name = it.key().toUtf8();
        archive_entry_set_pathname(e, name.constData());
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_entry_set_size(e, it.value().size());
        archive_write_header(a, e);
        archive_write_data(a, it.value().constData(), it.value().size());
        archive_entry_free(e);
    }
    archive_write_close(a);
    archive_write_free(a);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        auto publicClientError =
            microsoftAuthError(Json{{"error", "invalid_client"}, {"error_codes", QJsonArray{70002}}});
        check(publicClientError.contains("AADSTS70002") &&
                  publicClientError.contains("Allow public client flows"),
              "public-client configuration guidance");
        check(microsoftAuthError(Json{{"error", "expired_token"}}).contains("expired"),
              "expired device-code guidance");
        check(microsoftAuthError(Json{{"error", "invalid_client"}, {"error_codes", QJsonArray{700016}}})
                  .contains("Application (client) ID"),
              "unknown-client guidance");
        check(javaMajor("openjdk version \"25.0.4.1\" 2026-10-20") == 25, "Java 25 version detection");
        check(javaMajor("java version \"1.8.0_482\"") == 8, "legacy Java version detection");
        check(javaMajor("openjdk 21.0.2 2024-01-16") == 21, "unquoted Java version detection");
        check(javaMajor("OpenJDK warning: unrelated text") == 0, "invalid Java output rejected");
        rejects([&] { selectJava("/nonexistent/neko-java", 25, false); }, "invalid manual Java accepted");
        if (app.arguments().contains("--java-integration")) {
            auto runtime = selectJava("java", 25);
            check(runtime.major == 25 && inspectJava(runtime.executable).major == 25,
                  "Java 25 automatically selected over PATH runtime");
            std::cout << "Selected Java 25: " << runtime.executable.toStdString() << "\n";
        }
        QTemporaryDir temp;
        auto root = temp.path();
        check(safePath(root, "mods/a.jar") == root + "/mods/a.jar", "safe child");
        for (auto bad :
             QStringList{"../escape", "mods/../../escape", "/absolute", "C:/absolute", "mods\\evil", "."})
            rejects([&] { safePath(root, bad); }, "unsafe path accepted");
        QFile f(root + "/target");
        check(f.open(QIODevice::WriteOnly), "fixture file");
        f.write("a");
        f.close();
        if (QFile::link(root + "/target", root + "/link"))
            rejects([&] { safePath(root, "link"); }, "symlink accepted");
        check(offlineUuid("Notch") == "b50ad385829d3141a2167e7d7539ba7f", "offline UUID mismatch");
        Json rules{{"rules", QJsonArray{Json{{"action", "allow"}},
                                        Json{{"action", "disallow"}, {"os", Json{{"name", "windows"}}}}}}};
        check(allowed(rules, "linux", "x86_64"), "linux rule");
        check(!allowed(rules, "windows", "x86_64"), "windows rule");
        check(!allowed(Json{{"rules", QJsonArray{Json{{"action", "allow"},
                                                      {"features", Json{{"is_demo_user", true}}}}}}},
                       "linux", "x86_64"),
              "feature rule");
        zip(root + "/ok.zip", "overrides/config/a.txt");
        extract(root + "/ok.zip", root + "/extracted", "overrides");
        check(QFile::exists(root + "/extracted/config/a.txt"), "override extraction");
        zip(root + "/evil.zip", "../escaped.txt");
        rejects([&] { extract(root + "/evil.zip", root + "/bad"); }, "ZIP traversal accepted");
        zip(root + "/link.zip", "linked", true);
        rejects([&] { extract(root + "/link.zip", root + "/bad"); }, "ZIP symlink accepted");
        auto hash = QString::fromLatin1(QCryptographicHash::hash("client", QCryptographicHash::Sha1).toHex());
        auto dir = root + "/versions/test";
        QDir().mkpath(dir);
        QFile jar(dir + "/test.jar");
        check(jar.open(QIODevice::WriteOnly), "fixture jar");
        jar.write("client");
        jar.close();
        Json meta{
            {"id", "test"},
            {"mainClass", "net.minecraft.client.main.Main"},
            {"type", "release"},
            {"downloads", Json{{"client", Json{{"url", "https://example.invalid/client"}, {"sha1", hash}}}}},
            {"arguments",
             Json{{"jvm", QJsonArray{"-cp", "${classpath}", "-Djava.library.path=${natives_directory}"}},
                  {"game", QJsonArray{"--username", "${auth_player_name}", "--accessToken",
                                      "${auth_access_token}"}}}}};
        writeJson(dir + "/test.json", meta);
        Engine engine(root);
        if (app.arguments().contains("--search-integration")) {
            Json filters{{"version", "1.21.1"},
                         {"loader", "fabric"},
                         {"category", "optimization"},
                         {"sort", "downloads"},
                         {"limit", 3}};
            auto first = engine.searchPage({}, false, {}, filters);
            const auto hits = first["hits"].toArray();
            check(hits.size() == 3 && first["total"].toInt() > 3, "filtered search has paginated results");
            const auto details = engine.projectDetails(hits.first().toObject(), false, {});
            check(!details["title"].toString().isEmpty() && !details["body"].toString().isEmpty(),
                  "full project details resolve from search results");
            double previous = std::numeric_limits<double>::max();
            for (const auto &value : hits) {
                const auto hit = value.toObject();
                check(hit["versions"].toArray().contains("1.21.1"), "version facet respected");
                check(hit["categories"].toArray().contains("fabric"), "loader facet respected");
                check(hit["categories"].toArray().contains("optimization"), "category facet respected");
                check(hit["downloads"].toDouble() <= previous, "downloads sorted descending");
                previous = hit["downloads"].toDouble();
            }
            filters["offset"] = 3;
            auto second = engine.searchPage({}, false, {}, filters)["hits"].toArray();
            check(!second.isEmpty() &&
                      second.first().toObject()["project_id"] != hits.first().toObject()["project_id"],
                  "next search page differs");
            auto empty = engine.searchPage("neko-no-such-pack-947239847239", false, {}, filters);
            check(empty["hits"].toArray().isEmpty() && empty["total"].toInt() == 0, "empty search response");
            const auto fixture = qEnvironmentVariable("NEKO_SEARCH_FIXTURE");
            if (!fixture.isEmpty())
                writeJson(fixture, {{"query", ""}, {"hits", hits}});
        }
        if (app.arguments().contains("--content-integration")) {
            const Json contentInstance{{"id", "content-integration"}, {"version", "1.21.1"}};
            const auto resource = engine.installContent(contentInstance, Json{{"project_id", "soft-bits"}},
                                                        "resourcepack", false, {});
            const auto shader = engine.installContent(
                contentInstance, Json{{"project_id", "seaglass-shaders"}}, "shader", false, {});
            check(QFileInfo(resource).size() > 0 &&
                      resource.startsWith(root + "/instances/content-integration/resourcepacks/"),
                  "live Modrinth resource pack download");
            check(QFileInfo(shader).size() > 0 &&
                      shader.startsWith(root + "/instances/content-integration/shaderpacks/"),
                  "live Modrinth shader download");
        }
        if (app.arguments().contains("--curse-content-integration")) {
            const auto key = QSettings("NekoLauncher", "NekoLauncher").value("curseKey").toString();
            check(!key.isEmpty(), "CurseForge API key is not configured in NekoLauncher settings");
            const Json contentInstance{{"id", "curse-content-integration"}, {"version", "1.21.1"}};
            for (const auto &kind : QStringList{"resourcepack", "shader", "world"}) {
                const auto hits = engine
                                      .searchPage({}, true, key,
                                                  {{"type", kind},
                                                   {"version", "1.21.1"},
                                                   {"sort", "downloads"},
                                                   {"limit", 30}})["hits"]
                                      .toArray();
                check(!hits.isEmpty(), qPrintable("live CurseForge " + kind + " search"));
                bool installed = false;
                for (const auto &value : hits) {
                    const auto project = value.toObject();
                    QUrl endpoint("https://api.curseforge.com/v1/mods/" +
                                  QString::number(project["id"].toInt()) + "/files");
                    QUrlQuery query;
                    query.addQueryItem("gameVersion", "1.21.1");
                    query.addQueryItem("pageSize", "50");
                    endpoint.setQuery(query);
                    const auto files =
                        engine.http.json(endpoint.toString(), {}, {{"x-api-key", key.toUtf8()}})["data"]
                            .toArray();
                    if (files.isEmpty())
                        continue;
                    const auto file = files.first().toObject();
                    if (file["downloadUrl"].toString().isEmpty() ||
                        file["fileLength"].toDouble() > 64 * 1024 * 1024)
                        continue;
                    try {
                        const auto path = engine.installContent(contentInstance, project, kind, true, key);
                        installed = QFileInfo(path).exists();
                    } catch (const Error &) {
                    }
                    if (installed)
                        break;
                }
                check(installed, qPrintable("live CurseForge " + kind + " download"));
            }
        }
        Json instance{{"id", "test-instance"},
                      {"version", "test"},
                      {"java", "custom-java"},
                      {"memory", 2048},
                      {"jvmArgs", "-Dfoo=\"hello world\""}};
        auto plan = engine.prepare(instance, {"Notch", offlineUuid("Notch"), {}, 0}, true);
        check(plan.java == "custom-java", "runtime selection");
        check(plan.arguments.contains("-Xmx2048M"), "RAM argument");
        check(plan.arguments.contains("-Dfoo=hello world"), "argument quoting");
        check(plan.arguments.contains("Notch") && plan.arguments.contains("0"), "offline substitutions");
        rejects([&] { engine.prepare(instance, {"Notch", offlineUuid("Notch"), {}, 0}, false); },
                "expired auth accepted");
        // Fabric/Quilt libraries use Maven coordinates without a downloads object.
        // Looking up downloads on a mutable JSON object used to insert it, skipping this branch.
        auto loaderJar = root + "/libraries/net/fabricmc/fabric-loader/0.16.0/fabric-loader-0.16.0.jar";
        QDir().mkpath(QFileInfo(loaderJar).absolutePath());
        QFile loaderFile(loaderJar);
        check(loaderFile.open(QIODevice::WriteOnly), "loader fixture");
        loaderFile.write("cached-loader");
        loaderFile.close();
        instance["profile"] =
            Json{{"mainClass", "net.fabricmc.loader.impl.launch.knot.KnotClient"},
                 {"libraries", QJsonArray{Json{{"name", "net.fabricmc:fabric-loader:0.16.0"},
                                               {"url", "https://maven.fabricmc.net/"}}}}};
        plan = engine.prepare(instance, {"Notch", offlineUuid("Notch"), {}, 0}, true);
        auto bootstrapInstance = instance;
        bootstrapInstance["profile"] =
            Json{{"id", "neoforge-fixture"},
                 {"mainClass", "cpw.mods.bootstraplauncher.BootstrapLauncher"},
                 {"arguments", Json{{"jvm", QJsonArray{"-DignoreList=client-extra,${version_name}.jar"}}}}};
        auto bootstrapPlan = engine.prepare(bootstrapInstance, {"Notch", offlineUuid("Notch"), {}, 0}, true);
        bool clientIgnored = false;
        for (const auto &arg : bootstrapPlan.arguments)
            if (arg.startsWith("-DignoreList="))
                clientIgnored = arg.contains("neoforge-fixture.jar") &&
                                arg.contains(instance["version"].toString() + ".jar");
        check(clientIgnored, "BootstrapLauncher must ignore original Minecraft JAR filename");
        auto classpath = plan.arguments.value(plan.arguments.indexOf("-cp") + 1);
        check(classpath.contains(loaderJar), "Fabric loader omitted from classpath");
        check(plan.arguments.contains("net.fabricmc.loader.impl.launch.knot.KnotClient"),
              "Fabric entrypoint");
        instance.remove("profile");
        meta.remove("arguments");
        meta["minecraftArguments"] = "--username ${auth_player_name} --assetsDir ${game_assets}";
        writeJson(dir + "/test.json", meta);
        plan = engine.prepare(instance, {"Notch", offlineUuid("Notch"), {}, 0}, true);
        check(plan.arguments.contains("-cp") && plan.arguments.contains("Notch"), "legacy launch plan");
        auto packDir = root + "/pack";
        QDir().mkpath(packDir);
        Json index{{"formatVersion", 1},
                   {"game", "minecraft"},
                   {"name", "Empty test pack"},
                   {"dependencies", Json{{"minecraft", "test"}}},
                   {"files", QJsonArray{}}};
        writeJson(packDir + "/modrinth.index.json", index);
        // A hand-built pack verifies import registration and rollback for unsupported loaders.
        auto pack = [&](QString file) {
            auto *a = archive_write_new();
            archive_write_set_format_zip(a);
            archive_write_open_filename(a, QFile::encodeName(file).constData());
            auto data = QJsonDocument(index).toJson();
            auto *e = archive_entry_new();
            archive_entry_set_pathname(e, "modrinth.index.json");
            archive_entry_set_filetype(e, AE_IFREG);
            archive_entry_set_perm(e, 0644);
            archive_entry_set_size(e, data.size());
            archive_write_header(a, e);
            archive_write_data(a, data.constData(), data.size());
            archive_entry_free(e);
            archive_write_close(a);
            archive_write_free(a);
        };
        pack(root + "/test.mrpack");
        auto imported = engine.importPack(root + "/test.mrpack", {});
        check(imported["name"].toString() == "Empty test pack", "pack name");
        check(QFile::exists(root + "/instances/" + imported["id"].toString() + "/instance.json"),
              "pack persisted");
        index["dependencies"] = Json{{"minecraft", "test"}, {"unknown-loader", "1"}};
        pack(root + "/unsupported.mrpack");
        rejects([&] { engine.importPack(root + "/unsupported.mrpack", {}); }, "unsupported loader accepted");
        for (const auto &loader : QStringList{"forge", "neoforge"}) {
            index["dependencies"] = Json{{"minecraft", "1.20.1"}, {loader, "47.1.0"}};
            pack(root + "/loader.mrpack");
            auto packInstance = engine.importPack(root + "/loader.mrpack", {});
            check(packInstance["loader"].toString() == (loader == "forge" ? "Forge" : "NeoForge"),
                  "pack loader retained");
            check(packInstance["loaderVersion"] == "47.1.0", "pack loader version retained");
        }
        const Json prismDefinition{
            {"formatVersion", 1},
            {"components", QJsonArray{Json{{"uid", "net.minecraft"}, {"version", "1.20.1"}},
                                      Json{{"uid", "net.fabricmc.fabric-loader"}, {"version", "0.16.10"}}}}};
        zipFiles(root + "/prism.zip", {{"Prism Test/mmc-pack.json", QJsonDocument(prismDefinition).toJson()},
                                       {"Prism Test/instance.cfg", "name=Neko Prism Export\n"},
                                       {"Prism Test/.minecraft/saves/Existing World/level.dat", "world"},
                                       {"Prism Test/.minecraft/resourcepacks/example.zip", "resource"},
                                       {"Prism Test/.minecraft/shaderpacks/example.zip", "shader"},
                                       {"Prism Test/.minecraft/screenshots/example.png", "image"}});
        const auto prism = engine.importPack(root + "/prism.zip", {});
        const auto prismPath = root + "/instances/" + prism["id"].toString();
        check(prism["name"] == "Neko Prism Export" && prism["version"] == "1.20.1" &&
                  prism["loader"] == "Fabric" && prism["loaderVersion"] == "0.16.10",
              "Prism metadata imported");
        check(QFile::exists(prismPath + "/saves/Existing World/level.dat") &&
                  QFile::exists(prismPath + "/resourcepacks/example.zip") &&
                  QFile::exists(prismPath + "/shaderpacks/example.zip") &&
                  QFile::exists(prismPath + "/screenshots/example.png"),
              "Prism instance content imported");
        const Json worldTarget{{"id", "world-target"}};
        zipFiles(root + "/world.zip", {{"Downloaded World/level.dat", "world-data"},
                                       {"Downloaded World/region/r.0.0.mca", "region-data"}});
        const auto firstWorld = engine.importWorld(worldTarget, root + "/world.zip");
        const auto secondWorld = engine.importWorld(worldTarget, root + "/world.zip");
        check(QFile::exists(firstWorld + "/level.dat") && QFile::exists(firstWorld + "/region/r.0.0.mca"),
              "world ZIP imported with its contents");
        check(firstWorld != secondWorld && QFileInfo(secondWorld).fileName() == "Downloaded World (2)",
              "world import preserves an existing save");
        zipFiles(root + "/not-a-world.zip", {{"readme.txt", "not a save"}});
        rejects([&] { engine.importWorld(worldTarget, root + "/not-a-world.zip"); },
                "archive without level.dat accepted as a world");
        if (app.arguments().contains("--loader-integration")) {
            for (const auto &loader : QStringList{"Forge", "NeoForge"}) {
                Json loaderInstance{{"version", loader == "Forge" ? "1.20.1" : "1.21.1"},
                                    {"loader", loader},
                                    {"loaderVersion", loader == "Forge" ? "47.4.0" : "21.1.172"},
                                    {"java", qEnvironmentVariable("NEKO_TEST_JAVA", "java")}};
                auto profile =
                    engine.installForge(loaderInstance, engine.version(loaderInstance["version"].toString()));
                check(!profile["mainClass"].toString().isEmpty(),
                      "official loader installer creates client profile");
                loaderInstance["id"] = "integration-" + loader;
                loaderInstance["profile"] = profile;
                auto metadata = engine.version(loaderInstance["version"].toString());
                metadata.remove("assetIndex");
                writeJson(root + "/versions/" + loaderInstance["version"].toString() + "/" +
                              loaderInstance["version"].toString() + ".json",
                          metadata);
                auto plan = engine.prepare(loaderInstance, {"Notch", offlineUuid("Notch"), {}, 0}, true);
                check(plan.arguments.contains(profile["mainClass"].toString()),
                      "installed loader launch plan resolves");
            }
        }
        if (app.arguments().contains("--legacy-forge-integration")) {
            const Json legacy{{"version", "1.8.9"},
                              {"loader", "Forge"},
                              {"loaderVersion", "11.15.1.2318"},
                              {"java", qEnvironmentVariable("NEKO_TEST_JAVA", "java")}};
            const auto profile = engine.installForge(legacy, engine.version("1.8.9"));
            const auto coordinate = QString("1.8.9-11.15.1.2318-1.8.9");
            const auto library =
                root + "/libraries/net/minecraftforge/forge/" + coordinate + "/forge-" + coordinate + ".jar";
            check(profile["id"].toString().contains(coordinate) && QFileInfo(library).size() > 0,
                  "legacy Forge coordinate installs its bundled universal library");
            check(QFile::exists(root + "/versions/" + profile["id"].toString() + "/" +
                                profile["id"].toString() + ".json"),
                  "legacy Forge profile registered without unsupported client CLI mode");
        }
        const QByteArray requiredBytes("expected-mod-version");
        const auto requiredHash =
            QString::fromLatin1(QCryptographicHash::hash(requiredBytes, QCryptographicHash::Sha1).toHex());
        Json restrictedMeta{{"fileName", "moreoverlays-1.0.jar"},
                            {"hashes", QJsonArray{Json{{"algo", 1}, {"value", requiredHash}}}}};
        auto requirement = curseDownloadRequirement(
            restrictedMeta, 123, 456, "https://www.curseforge.com/minecraft/mc-mods/more-overlays/");
        check(requirement["pageUrl"] ==
                  "https://www.curseforge.com/minecraft/mc-mods/more-overlays/files/456",
              "exact official file page");
        rejects(
            [&] { curseDownloadRequirement(restrictedMeta, 123, 456, "https://example.invalid/project"); },
            "unofficial download page accepted");
        auto badMetadata = restrictedMeta;
        badMetadata["fileName"] = "../escape.jar";
        rejects([&] { curseDownloadRequirement(badMetadata, 123, 456, "https://curseforge.com/test"); },
                "unsafe manual download path accepted");
        badMetadata = restrictedMeta;
        badMetadata["hashes"] = QJsonArray{};
        rejects([&] { curseDownloadRequirement(badMetadata, 123, 456, "https://curseforge.com/test"); },
                "unverifiable manual download accepted");
        Json incomplete{{"id", "pending-test"},
                        {"name", "Pending pack"},
                        {"version", "test"},
                        {"pendingDownloads", QJsonArray{requirement}}};
        writeJson(root + "/instances/pending-test/instance.json", incomplete);
        rejects([&] { engine.prepare(incomplete, {"Notch", offlineUuid("Notch"), {}, 0}, true); },
                "incomplete pack allowed to launch");
        QFile manual(root + "/browser-renamed (1).jar");
        check(manual.open(QIODevice::WriteOnly), "manual download fixture");
        manual.write("wrong-version");
        manual.close();
        auto stillPending = engine.completeDownloads(incomplete, {manual.fileName()});
        check(stillPending["pendingDownloads"].toArray().size() == 1 &&
                  !QFile::exists(root + "/instances/pending-test/mods/moreoverlays-1.0.jar"),
              "wrong mod version rejected without losing pending state");
        check(manual.open(QIODevice::WriteOnly | QIODevice::Truncate), "replace download fixture");
        manual.write(requiredBytes);
        manual.close();
        Engine restarted(root);
        auto completed = restarted.completeDownloads(stillPending, {manual.fileName()});
        check(!completed.contains("pendingDownloads"),
              "download resumed after restart and browser filename change");
        QFile installed(root + "/instances/pending-test/mods/moreoverlays-1.0.jar");
        check(installed.open(QIODevice::ReadOnly) && installed.readAll() == requiredBytes,
              "verified mod installed under canonical filename");
        check(!readJson(root + "/instances/pending-test/instance.json").contains("pendingDownloads"),
              "completion persisted");
        auto md5Metadata = restrictedMeta;
        md5Metadata["hashes"] = QJsonArray{Json{
            {"algo", 2},
            {"value",
             QString::fromLatin1(QCryptographicHash::hash(requiredBytes, QCryptographicHash::Md5).toHex())}}};
        auto md5Requirement = curseDownloadRequirement(
            md5Metadata, 123, 456, "https://curseforge.com/minecraft/mc-mods/more-overlays");
        incomplete["pendingDownloads"] = QJsonArray{md5Requirement};
        writeJson(root + "/instances/pending-test/instance.json", incomplete);
        check(!restarted.completeDownloads(incomplete, {}).contains("pendingDownloads"),
              "MD5 fallback verifies already installed manual file");

        auto backupDir = root + "/backup-source";
        QDir().mkpath(backupDir + "/saves/world");
        writeJson(backupDir + "/instance.json",
                  Json{{"id", "original"}, {"name", "Backup test"}, {"version", "test"}});
        QFile world(backupDir + "/saves/world/level.dat");
        check(world.open(QIODevice::WriteOnly), "backup fixture");
        world.write("world-data");
        world.close();
        backupInstance(backupDir, root + "/instance.neko.zip");
        auto restored = engine.importPack(root + "/instance.neko.zip", {});
        check(restored["id"].toString() != "original", "restored backup gets new ID");
        QFile restoredWorld(root + "/instances/" + restored["id"].toString() + "/saves/world/level.dat");
        check(restoredWorld.open(QIODevice::ReadOnly) && restoredWorld.readAll() == "world-data",
              "backup preserves saves");
        rejects([&] { backupInstance(backupDir, backupDir + "/nested.zip"); },
                "backup inside source accepted");
        QProcessEnvironment inherited;
        inherited.insert("PATH", "/sample/bin");
        inherited.insert("LD_LIBRARY_PATH", "/old/lib");
        auto env = launchEnvironment(
            Json{{"nativePaths", "/custom/lib"}, {"environment", Json{{"NEKO_TEST", "value"}}}}, inherited);
        check(env.value("NEKO_TEST") == "value" &&
                  env.value("PATH").split(QDir::listSeparator()).first() == "/sample/bin",
              "launch environment preserves inherited variables");
#ifdef Q_OS_LINUX
        QTemporaryDir helperDirectory;
        QFile helper(helperDirectory.filePath("xrandr"));
        check(helper.open(QIODevice::WriteOnly) && helper.write("#!/bin/sh\nexit 0\n") > 0,
              "create custom display helper");
        helper.close();
        helper.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        auto helperEnvironment = inherited;
        helperEnvironment.insert("PATH", helperDirectory.path());
        check(launchEnvironment(Json{}, helperEnvironment).value("PATH") == helperDirectory.path(),
              "existing xrandr command keeps its PATH precedence");
        check(launchEnvironment(Json{{"linuxDisplay", "x11"}}, inherited).value("SDL_VIDEODRIVER") == "x11",
              "explicit X11 backend");
        check(!launchEnvironment(Json{}, inherited).contains("SDL_VIDEODRIVER"),
              "automatic backend preserves session defaults");
        rejects([&] { launchEnvironment(Json{{"linuxDisplay", "invalid"}}, inherited); },
                "invalid display backend accepted");
        auto audioInherited = inherited;
        audioInherited.insert("ALSOFT_DRIVERS", "pulse");
        check(launchEnvironment(Json{}, audioInherited).value("ALSOFT_DRIVERS") == "pulse",
              "Automatic audio preserves existing overrides");
        check(launchEnvironment(Json{{"linuxAudio", "pipewire"}}, audioInherited).value("ALSOFT_DRIVERS") ==
                  "pipewire",
              "Explicit PipeWire audio backend");
        check(!launchEnvironment(Json{}, inherited).contains("ALSOFT_DRIVERS"),
              "Automatic audio leaves driver selection to OpenAL");
        rejects([&] { launchEnvironment(Json{{"linuxAudio", "invalid"}}, inherited); },
                "Invalid audio backend accepted");
        check(env.value("LD_LIBRARY_PATH").contains("/custom/lib") &&
                  env.value("LD_LIBRARY_PATH").endsWith("/old/lib"),
              "native paths preserve existing search paths");
#endif
        check(QDir(root + "/instances/test-instance/natives/java").exists(),
              "Java native subdirectory exists");
        engine.http.cancellation = std::make_shared<std::atomic_bool>(true);
        rejects([&] { engine.prepare(instance, {"Notch", offlineUuid("Notch"), {}, 0}, true); },
                "cancelled preparation continued");
        engine.http.cancellation.reset();
        if (app.arguments().contains("--network")) {
            auto artwork = engine.officialArtwork(Json{{"id", "official-cover-test"}},
                                                  Json{{"project_id", "fabulously-optimized"}}, false);
            check(artwork["provider"] == "modrinth" &&
                      QFileInfo(root + "/instances/official-cover-test/official-artwork.img").size() > 0,
                  "official project artwork downloaded");
            auto manifest = engine.manifest();
            auto latest = manifest["latest"].toObject()["release"].toString();
            check(!latest.isEmpty(), "live Mojang catalog");
            auto version = engine.version(latest);
            check(!version["downloads"].toObject()["client"].toObject()["url"].toString().isEmpty(),
                  "live version metadata");
            auto fabric = engine.loaderProfile("1.20.1", "Fabric");
            check(!fabric["mainClass"].toString().isEmpty(), "live Fabric profile");
            instance["profile"] = fabric;
            auto fabricPlan = engine.prepare(instance, {"Notch", offlineUuid("Notch"), {}, 0}, true);
            auto fabricCp = fabricPlan.arguments.value(fabricPlan.arguments.indexOf("-cp") + 1);
#ifdef Q_OS_WIN
            const auto cpSeparator = ';';
#else
            const auto cpSeparator = ':';
#endif
            QString actualLoaderJar;
            for (const auto &entry : fabricCp.split(cpSeparator))
                if (QFileInfo(entry).fileName().startsWith("fabric-loader-"))
                    actualLoaderJar = entry;
            check(!actualLoaderJar.isEmpty(), "live Fabric loader classpath");
            auto *reader = archive_read_new();
            archive_read_support_format_zip(reader);
            auto release = qScopeGuard([&] { archive_read_free(reader); });
            check(archive_read_open_filename(reader, QFile::encodeName(actualLoaderJar).constData(), 65536) ==
                      ARCHIVE_OK,
                  "live Fabric JAR opens");
            archive_entry *entry = nullptr;
            bool entrypoint = false;
            while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
                if (QString::fromUtf8(archive_entry_pathname(entry)) ==
                    "net/fabricmc/loader/impl/launch/knot/KnotClient.class")
                    entrypoint = true;
                archive_read_data_skip(reader);
            }
            check(entrypoint, "downloaded Fabric JAR contains KnotClient");
            instance.remove("profile");
            auto quilt = engine.loaderProfile("1.20.1", "Quilt");
            check(!quilt["mainClass"].toString().isEmpty(), "live Quilt profile");
            check(!engine.search("optimization", false, {}).isEmpty(), "live Modrinth search");
        }
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
