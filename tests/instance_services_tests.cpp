#include "instance_services.hpp"
#include <iostream>
using namespace neko;
namespace {
int checks = 0;
void check(bool condition, const char *message) {
    ++checks;
    if (!condition)
        throw Error(message);
}
template <class F> void rejects(F action, const char *message) {
    bool rejected = false;
    try {
        action();
    } catch (const Error &) {
        rejected = true;
    }
    check(rejected, message);
}
void put(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size(),
          "fixture write");
}
QByteArray get(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "fixture read");
    return file.readAll();
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        check(commandScore("settings", "Settings") > commandScore("set", "World settings"),
              "exact command ranks first");
        check(commandScore("fab opt", "Play Fabulously Optimized") > 0, "multiword search");
        check(commandScore("fbo", "Fabulously Optimized") > 0, "fuzzy command search");
        check(commandScore("banana", "Settings", "appearance") < 0, "unrelated command excluded");
        check(formatDuration(59) == "59s" && formatDuration(125) == "2m" && formatDuration(7320) == "2h 2m",
              "playtime formatting");
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary directory");
        const auto root = temporary.path();
        const auto dir = root + "/instances/test";
        writeJson(dir + "/instance.json", {{"id", "test"},
                                           {"name", "Test"},
                                           {"version", "1.21.1"},
                                           {"snapshotKeep", 2},
                                           {"favorite", true}});
        put(dir + "/saves/World/level.dat", "original-world");
        put(dir + "/mods/test.jar", "not-world-data");
        WorldSnapshots snapshots(root);
        auto first = snapshots.create("test");
        check(snapshots.list("test").size() == 1, "snapshot listed");
        const auto archive = root + "/snapshots/test/" + first["id"].toString() + ".worlds.zip";
        extract(archive, root + "/inspection");
        check(get(root + "/inspection/saves/World/level.dat") == "original-world", "world files included");
        check(!QFile::exists(root + "/inspection/mods/test.jar") &&
                  !QFile::exists(root + "/inspection/instance.json"),
              "snapshot excludes mods and configuration");
        put(dir + "/saves/World/level.dat", "newer-world");
        snapshots.restore("test", first["id"].toString());
        check(get(dir + "/saves/World/level.dat") == "original-world", "snapshot restored");
        QString recovery;
        for (const auto &value : snapshots.list("test"))
            if (value.toObject()["reason"] == "Before restore")
                recovery = value.toObject()["id"].toString();
        check(!recovery.isEmpty(), "recovery point created before replacement");
        snapshots.restore("test", recovery);
        check(get(dir + "/saves/World/level.dat") == "newer-world", "pre-restore worlds recoverable");
        put(archive, "corrupted archive");
        rejects([&] { snapshots.restore("test", first["id"].toString()); }, "corrupted snapshot accepted");
        check(get(dir + "/saves/World/level.dat") == "newer-world",
              "corruption leaves current saves unchanged");
        auto settings = readJson(dir + "/instance.json");
        settings["version"] = "1.20.1";
        writeJson(dir + "/instance.json", settings);
        rejects([&] { snapshots.restore("test", recovery); }, "wrong-version restore accepted");
        settings["version"] = "1.21.1";
        writeJson(dir + "/instance.json", settings);
        for (int i = 0; i < 4; ++i) {
            put(dir + "/saves/World/level.dat", QByteArray::number(i));
            snapshots.create("test", true);
        }
        int automatic = 0, manual = 0;
        for (const auto &value : snapshots.list("test"))
            value.toObject()["automatic"].toBool() ? ++automatic : ++manual;
        check(automatic == 2 && manual >= 3, "retention preserves manual recovery points");
        const auto before = QDir(root + "/snapshots/test").entryList(QDir::Files);
        int calls = 0;
        WorldSnapshots cancelled(root, [&] {
            if (++calls >= 3)
                throw Error("cancelled");
        });
        rejects([&] { cancelled.create("test"); }, "cancelled snapshot completed");
        check(QDir(root + "/snapshots/test").entryList(QDir::Files) == before,
              "cancelled snapshot leaves no partial archive");
        rejects([&] { snapshots.restore("test", "../../outside"); }, "snapshot traversal accepted");
        writeJson(root + "/instances/empty/instance.json", {{"id", "empty"}, {"version", "1.21.1"}});
        rejects([&] { snapshots.create("empty"); }, "empty snapshot created");
        recordSession(root, "test", {1000, 120, 0, false, "java"});
        recordSession(root, "test", {2000, 15, 6, true, "java"});
        recordSession(root, "test", {3000, 30, 143, false, "java", true});
        auto sessions = sessionHistory(root, "test");
        check(sessions.size() == 3 && sessions[0].toObject()["stopped"].toBool() &&
                  sessions[1].toObject()["crashed"].toBool(),
              "session results preserved");
        settings = readJson(dir + "/instance.json");
        check(settings["playtimeSeconds"] == 165 && settings["sessionCount"] == 3 &&
                  settings["favorite"].toBool(),
              "playtime totals preserve instance preferences");
        for (int i = 0; i < 102; ++i)
            recordSession(root, "test", {4000 + i, 1, 0, false, "java"});
        check(sessionHistory(root, "test").size() == 100 &&
                  readJson(dir + "/instance.json")["sessionCount"] == 105,
              "bounded history preserves lifetime counts");
        rejects([&] { recordSession(root, "test", {0, -1}); }, "invalid session accepted");
        settings = readJson(dir + "/instance.json");
        settings["playtimeSeconds"] = 0;
        settings["sessionCount"] = 0;
        writeJson(dir + "/instance.json", settings);
        recordSession(root, "test", {6000, 10, 0, false, "java"});
        check(sessionStatistics(root, "test")["totalSeconds"] == 277 &&
                  readJson(dir + "/instance.json")["sessionCount"] == 106,
              "atomic ledger heals stale display totals");
        check(get(dir + "/mods/test.jar") == "not-world-data", "world restore preserves mods");
        writeJson(root + "/versions/health-fixture/health-fixture.json",
                  {{"javaVersion", Json{{"majorVersion", 21}}}});
        auto unhealthy = settings;
        unhealthy["version"] = "health-fixture";
        unhealthy["java"] = root + "/missing-java";
        unhealthy["javaAuto"] = false;
        unhealthy["memory"] = 256;
        unhealthy["minMemory"] = 512;
        unhealthy["pendingDownloads"] = QJsonArray{Json{{"fileName", "missing.jar"}}};
        Engine engine(root);
        const auto report = inspectInstance(engine, unhealthy);
        int errors = 0;
        for (const auto &entry : report)
            if (entry.state == CheckState::Error)
                ++errors;
        check(errors >= 3, "health report catches pending files, invalid memory and Java");
        std::cout << checks << " instance service checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
