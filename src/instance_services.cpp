#include "instance_services.hpp"
#include <algorithm>

namespace neko {
namespace {
QString instanceDirectory(const QString &root, const QString &id) {
    require(!id.isEmpty() && !id.contains('/') && !id.contains('\\'), "Invalid instance ID");
    return safePath(root, "instances/" + id);
}
QString fileHash(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read snapshot");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&file), "Cannot verify snapshot");
    return QString::fromLatin1(hash.result().toHex());
}
} // namespace
QString formatDuration(qint64 seconds) {
    seconds = qMax(qint64(0), seconds);
    if (seconds < 60)
        return QString("%1s").arg(seconds);
    if (seconds < 3600)
        return QString("%1m").arg(seconds / 60);
    return QString("%1h %2m").arg(seconds / 3600).arg(seconds / 60 % 60);
}
int commandScore(const QString &query, const QString &title, const QString &keywords) {
    const auto needle = query.simplified().toCaseFolded();
    const auto label = title.toCaseFolded();
    if (needle.isEmpty())
        return 1;
    if (label == needle)
        return 1000;
    if (label.startsWith(needle))
        return 800;
    if (label.contains(needle))
        return 600 - label.indexOf(needle);
    const auto haystack = label + ' ' + keywords.toCaseFolded();
    bool allWords = true;
    for (const auto &word : needle.split(' '))
        allWords &= haystack.contains(word);
    if (allWords)
        return 400;
    int position = 0, gaps = 0;
    for (const auto ch : needle) {
        const auto next = haystack.indexOf(ch, position);
        if (next < 0)
            return -1;
        gaps += next - position;
        position = next + 1;
    }
    return qMax(1, 150 - gaps);
}
QList<InstanceCheck> inspectInstance(Engine &engine, const Json &instance) {
    QList<InstanceCheck> checks;
    auto add = [&](CheckState state, QString title, QString detail) {
        checks.append({state, std::move(title), std::move(detail)});
    };
    const auto dir = instanceDirectory(engine.root, instance["id"].toString());
    const auto pending = instance["pendingDownloads"].toArray().size();
    add(pending ? CheckState::Error : CheckState::Pass, "Pack downloads",
        pending
            ? QString("%1 file(s) still need browser downloads. Use Complete missing downloads.").arg(pending)
            : "No pending manual downloads.");
    const auto minimum = instance["minMemory"].toInt(512), maximum = instance["memory"].toInt(4096);
    add(minimum > 0 && maximum >= minimum ? CheckState::Pass : CheckState::Error, "Memory settings",
        QString("Initial %1 MiB · maximum %2 MiB").arg(minimum).arg(maximum));
    try {
        const auto metadata = engine.version(instance["version"].toString());
        const auto required = metadata["javaVersion"].toObject()["majorVersion"].toInt(8);
        auto runtime = selectJava(instance["java"].toString("java"), required,
                                  instance["javaAuto"].toBool(true), {engine.root + "/runtimes"});
        add(CheckState::Pass, "Compatible Java",
            QString("Java %1 · %2").arg(runtime.major).arg(runtime.executable));
        const auto client = safePath(engine.root, "versions/" + instance["version"].toString() + "/" +
                                                      instance["version"].toString() + ".jar");
        add(QFileInfo(client).size() > 0 ? CheckState::Pass : CheckState::Warning, "Game client",
            QFileInfo(client).size() > 0 ? "Client is cached. Libraries and assets are verified at launch."
                                         : "Client needs downloading on the next connected launch.");
    } catch (const std::exception &error) {
        engine.http.checkCancelled();
        add(CheckState::Error, "Version and Java", QString::fromUtf8(error.what()));
    }
    QStorageInfo storage(dir);
    if (storage.isValid() && storage.isReady())
        add(storage.bytesAvailable() < qint64(2) * 1024 * 1024 * 1024 ? CheckState::Warning
                                                                      : CheckState::Pass,
            "Free storage",
            QString("%1 GiB available on the instance volume.")
                .arg(storage.bytesAvailable() / 1073741824., 0, 'f', 1));
    QDir mods(dir + "/mods");
    add(CheckState::Pass, "Local mods",
        QString("%1 enabled · %2 disabled. Mod compatibility is checked by the game loader.")
            .arg(mods.entryList({"*.jar"}, QDir::Files).size())
            .arg(mods.entryList({"*.jar.disabled"}, QDir::Files).size()));
    add(instance["snapshotBeforeLaunch"].toBool() ? CheckState::Pass : CheckState::Warning, "World safety",
        instance["snapshotBeforeLaunch"].toBool()
            ? "A world snapshot is created before each launch when saves exist."
            : "Automatic snapshots are off. Enable them in World snapshots.");
    return checks;
}
WorldSnapshots::WorldSnapshots(QString dataRoot, std::function<void()> cancellation)
    : root(std::move(dataRoot)), check(std::move(cancellation)) {}
QString WorldSnapshots::directory(const QString &instanceId) const {
    instanceDirectory(root, instanceId);
    return safePath(root, "snapshots/" + instanceId);
}
QJsonArray WorldSnapshots::list(const QString &instanceId) const {
    QDir folder(directory(instanceId));
    QList<Json> records;
    for (const auto &name : folder.entryList({"*.json"}, QDir::Files | QDir::NoSymLinks)) {
        try {
            auto record = readJson(folder.filePath(name));
            const auto id = record["id"].toString();
            if (record["instanceId"] != instanceId || id + ".json" != name || id.contains('/') ||
                id.contains('\\'))
                continue;
            if (QFileInfo(safePath(folder.path(), id + ".worlds.zip")).size() > 0)
                records.append(record);
        } catch (const std::exception &) { /* A damaged record must not hide other recovery points. */
        }
    }
    std::sort(records.begin(), records.end(), [](const Json &a, const Json &b) {
        return a["createdAt"].toDouble() > b["createdAt"].toDouble();
    });
    QJsonArray result;
    for (const auto &record : records)
        result.append(record);
    return result;
}
Json WorldSnapshots::create(const QString &instanceId, bool automatic, const QString &reason) {
    if (check)
        check();
    const auto source = instanceDirectory(root, instanceId);
    const auto saves = safePath(source, "saves");
    QDirIterator worlds(saves, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    require(worlds.hasNext(), "No local world files to snapshot yet.");
    const auto instance = readJson(source + "/instance.json");
    const auto folder = directory(instanceId);
    require(QDir().mkpath(folder), "Cannot create snapshot directory");
    const auto id = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmsszzz") + "-" +
                    QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto archive = safePath(folder, id + ".worlds.zip");
    bool saved = false;
    auto cleanup = qScopeGuard([&] {
        if (!saved)
            QFile::remove(archive);
    });
    backupInstance(source, archive, check, {"saves/"});
    Json record{{"id", id},
                {"instanceId", instanceId},
                {"createdAt", double(QDateTime::currentMSecsSinceEpoch()) / 1000.},
                {"reason", reason.isEmpty() ? (automatic ? "Before launch" : "Manual") : reason},
                {"version", instance["version"]},
                {"automatic", automatic},
                {"bytes", double(QFileInfo(archive).size())},
                {"sha256", fileHash(archive)}};
    if (check)
        check();
    writeJson(safePath(folder, id + ".json"), record);
    saved = true;
    // Retention applies only to managed automatic snapshots; manual recovery points are retained.
    if (automatic) {
        int retained = 0;
        const auto keep = qBound(1, instance["snapshotKeep"].toInt(5), 50);
        for (const auto &value : list(instanceId)) {
            const auto old = value.toObject();
            if (!old["automatic"].toBool() || ++retained <= keep || old["id"] == id)
                continue;
            const auto oldId = old["id"].toString();
            if (QFile::remove(safePath(folder, oldId + ".worlds.zip")))
                QFile::remove(safePath(folder, oldId + ".json"));
        }
    }
    return record;
}
void WorldSnapshots::restore(const QString &instanceId, const QString &snapshotId) {
    if (check)
        check();
    const auto folder = directory(instanceId);
    const auto record = readJson(safePath(folder, snapshotId + ".json"));
    require(record["instanceId"] == instanceId && record["id"] == snapshotId,
            "Snapshot belongs to another instance");
    const auto source = instanceDirectory(root, instanceId);
    const auto instance = readJson(source + "/instance.json");
    require(record["version"] == instance["version"],
            "Select the snapshot's Minecraft version before restoring these worlds.");
    const auto archive = safePath(folder, snapshotId + ".worlds.zip");
    require(fileHash(archive) == record["sha256"].toString(),
            "Snapshot integrity check failed. Current worlds were not changed.");
    QTemporaryDir staging(source + "/.restore-XXXXXX");
    require(staging.isValid(), "Cannot stage world restore");
    extract(archive, staging.path());
    require(QDir(staging.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot) ==
                    QStringList{"saves"} &&
                QDir(staging.path() + "/saves").exists(),
            "Snapshot contains unexpected files");
    const auto current = source + "/saves", previous = staging.path() + "/previous-saves";
    const bool hadWorlds = QDir(current).exists();
    if (hadWorlds)
        create(instanceId, false,
               "Before restore"); // A durable recovery point exists before any replacement.
    if (check)
        check();
    if (hadWorlds)
        require(QDir().rename(current, previous), "Cannot move current worlds. Close Minecraft first.");
    if (!QDir().rename(staging.path() + "/saves", current)) {
        if (hadWorlds && !QDir().rename(previous, current)) {
            staging.setAutoRemove(false);
            throw Error(("Restore failed; original worlds are preserved in " + previous).toStdString());
        }
        throw Error("Restore failed; original worlds were kept.");
    }
}
QJsonArray sessionHistory(const QString &root, const QString &instanceId) {
    const auto path = instanceDirectory(root, instanceId) + "/history.json";
    return QFile::exists(path) ? readJson(path)["sessions"].toArray() : QJsonArray{};
}
Json sessionStatistics(const QString &root, const QString &instanceId) {
    const auto dir = instanceDirectory(root, instanceId);
    const auto instance = readJson(dir + "/instance.json");
    const auto path = dir + "/history.json";
    const auto history = QFile::exists(path) ? readJson(path) : Json{};
    return {{"totalSeconds", history["totalSeconds"].toDouble(instance["playtimeSeconds"].toDouble())},
            {"sessionCount", history["sessionCount"].toInt(instance["sessionCount"].toInt())}};
}
void recordSession(const QString &root, const QString &instanceId, const GameSession &session) {
    require(session.startedAt > 0 && session.durationSeconds >= 0, "Invalid game session");
    const auto dir = instanceDirectory(root, instanceId);
    QLockFile lock(safePath(dir, "history.lock"));
    require(lock.tryLock(5000), "Session history is being updated by another launcher");
    auto instance = readJson(dir + "/instance.json");
    const auto statistics = sessionStatistics(root, instanceId);
    const auto total = statistics["totalSeconds"].toDouble() + double(session.durationSeconds);
    const auto count = statistics["sessionCount"].toInt() + 1;
    auto sessions = sessionHistory(root, instanceId);
    sessions.prepend(Json{{"startedAt", double(session.startedAt)},
                          {"durationSeconds", double(session.durationSeconds)},
                          {"exitCode", session.exitCode},
                          {"crashed", session.crashed},
                          {"java", session.java},
                          {"stopped", session.stopped}});
    while (sessions.size() > 100)
        sessions.removeLast();
    // The ledger and lifetime totals share one atomic write. Instance stats are a display cache.
    writeJson(dir + "/history.json",
              {{"sessions", sessions}, {"totalSeconds", total}, {"sessionCount", count}});
    instance["playtimeSeconds"] = total;
    instance["sessionCount"] = count;
    instance["lastExitCode"] = session.exitCode;
    writeJson(dir + "/instance.json", instance);
}
} // namespace neko
