#include "core.hpp"

namespace neko {
Json Engine::installForge(const Json &instance, const Json &base) {
    const auto game = instance["version"].toString();
    const bool neo = instance["loader"] == "NeoForge";
    auto ver = instance["loaderVersion"].toString();
    if (ver.isEmpty() && !neo) {
        auto promos =
            http.json("https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json")
                ["promos"]
                    .toObject();
        ver = promos[game + "-recommended"].toString(promos[game + "-latest"].toString());
    }
    require(!ver.isEmpty(),
            "Choose a loader version in instance settings (modpacks supply this automatically).");
    require(QRegularExpression("^[A-Za-z0-9.+_-]+$").match(ver).hasMatch(), "Invalid loader version");
    const auto artifactName = neo && game != "1.20.1" ? QString("neoforge") : QString("forge");
    if (artifactName == "forge" && !ver.startsWith(game + "-"))
        ver = game + "-" + ver;
    const auto maven = neo ? QString("https://maven.neoforged.net/releases/net/neoforged/")
                           : QString("https://maven.minecraftforge.net/net/minecraftforge/");
    auto coordinate = ver;
    auto url = maven + artifactName + "/" + coordinate + "/" + artifactName + "-" + coordinate +
               "-installer.jar";
    auto installer = safePath(root, "cache/" + artifactName + "-" + coordinate + "-installer.jar");
    report("Downloading official " + instance["loader"].toString() + " installer " + ver);
    auto downloadInstaller = [&] {
        QString checksum;
        try {
            const auto published = QString::fromUtf8(http.request(url + ".sha1"))
                                       .trimmed()
                                       .section(QRegularExpression("\\s+"), 0, 0);
            if (QRegularExpression("^[a-fA-F0-9]{40}$").match(published).hasMatch())
                checksum = published;
        } catch (const Error &) {
            http.checkCancelled();
            // Some legacy releases retain the official installer but no longer publish its sidecar.
        }
        http.download(url, installer, checksum);
    };
    try {
        downloadInstaller();
    } catch (const Error &) {
        http.checkCancelled();
        require(artifactName == "forge" && !coordinate.endsWith("-" + game),
                "Official loader installer is unavailable at " + url);
        // A subset of legacy Forge builds (including 1.8.9-11.15.1.2318) use a Maven
        // coordinate with the Minecraft version repeated as a suffix.
        coordinate += "-" + game;
        url = maven + artifactName + "/" + coordinate + "/" + artifactName + "-" + coordinate +
              "-installer.jar";
        installer = safePath(root, "cache/" + artifactName + "-" + coordinate + "-installer.jar");
        report("Trying legacy Forge coordinate " + coordinate);
        downloadInstaller();
    }
    const auto versionDir = safePath(root, "versions/" + game);
    writeJson(versionDir + "/" + game + ".json", base);
    artifact(base["downloads"].toObject()["client"].toObject(), versionDir + "/" + game + ".jar");
    if (!QFile::exists(root + "/launcher_profiles.json"))
        writeJson(root + "/launcher_profiles.json", {{"profiles", Json{}}});
    QTemporaryDir unpacked;
    require(unpacked.isValid(), "Cannot inspect installer");
    extract(installer, unpacked.path());
    auto install = readJson(unpacked.path() + "/install_profile.json");
    Json profile;
    if (install.contains("versionInfo"))
        profile = install["versionInfo"].toObject();
    else {
        auto path = install["json"].toString("/version.json");
        if (path.startsWith('/'))
            path.remove(0, 1);
        profile = readJson(safePath(unpacked.path(), path));
    }
    const auto profileId = profile["id"].toString();
    require(!profileId.isEmpty(), "Installer has no client version profile");
    if (install.contains("versionInfo")) {
        // Forge's legacy SimpleInstaller only exposes GUI, server and extraction modes. Its
        // client install consists of registering versionInfo and placing the bundled universal
        // JAR at the unclassified Maven path expected by that profile.
        const auto legacy = install["install"].toObject();
        const auto libraryCoordinate = legacy["path"].toString();
        const auto libraryParts = libraryCoordinate.split(':');
        const auto bundledName = legacy["filePath"].toString();
        require(libraryParts.size() == 3 && !bundledName.isEmpty(),
                "Legacy Forge installer metadata is incomplete");
        auto group = libraryParts[0];
        group.replace('.', '/');
        const auto libraryRelative = group + "/" + libraryParts[1] + "/" + libraryParts[2] + "/" +
                                     libraryParts[1] + "-" + libraryParts[2] + ".jar";
        const auto bundled = safePath(unpacked.path(), bundledName);
        const auto library = safePath(root, "libraries/" + libraryRelative);
        require(QFileInfo(bundled).size() > 0, "Legacy Forge installer is missing its universal JAR");
        require(QDir().mkpath(QFileInfo(library).absolutePath()), "Cannot create Forge library directory");
        if (QFileInfo(library).size() <= 0) {
            QSaveFile output(library);
            QFile input(bundled);
            require(input.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly),
                    "Cannot install legacy Forge library");
            const auto bytes = input.readAll();
            require(output.write(bytes) == bytes.size() && output.commit(),
                    "Cannot save legacy Forge library");
        }
        const auto installed = safePath(root, "versions/" + profileId + "/" + profileId + ".json");
        writeJson(installed, profile);
        report("Legacy Forge installation complete");
        return profile;
    }
    QProcess process;
    process.setWorkingDirectory(QFileInfo(installer).absolutePath());
    process.setProcessEnvironment(launchEnvironment(instance));
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(instance["java"].toString("java"),
                  {"-Djava.awt.headless=true", "-jar", installer, "--installClient", root});
    require(process.waitForStarted(10000), "Cannot start loader installer with the selected Java runtime");
    auto cleanup = qScopeGuard([&] {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished();
        }
    });
    QByteArray tail;
    report("Running official client installer; downloading and patching libraries…");
    QElapsedTimer timer, update;
    timer.start();
    update.start();
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(100);
        http.checkCancelled();
        auto chunk = process.readAll();
        tail += chunk;
        if (!chunk.isEmpty() && update.elapsed() > 1500) {
            const auto lines =
                QString::fromUtf8(chunk).split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
            if (!lines.isEmpty())
                report(lines.last().left(240));
            update.restart();
        }
        if (tail.size() > 12000)
            tail = tail.right(12000);
        require(timer.elapsed() < 1200000, "Loader installer timed out after 20 minutes");
    }
    tail += process.readAll();
    require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            "Official loader installer failed. Check the selected Java runtime.\n" +
                QString::fromUtf8(tail.right(6000)));
    const auto installed = safePath(root, "versions/" + profileId + "/" + profileId + ".json");
    require(QFile::exists(installed), "Installer did not produce a client profile; this installer version "
                                      "may not support command-line client installation.");
    report(instance["loader"].toString() + " installation complete");
    return readJson(installed);
}
Json Engine::loaderProfile(const QString &game, const QString &loader, const QString &requested) {
    if (loader == "Vanilla" || loader.isEmpty())
        return {};
    require(loader == "Fabric" || loader == "Quilt",
            "Automatic installation currently supports Vanilla, Fabric and Quilt. Forge/NeoForge packs need "
            "a compatible installed profile.");
    auto host =
        loader == "Fabric" ? QString("https://meta.fabricmc.net/v2") : QString("https://meta.quiltmc.org/v3");
    auto ver = requested;
    if (ver.isEmpty()) {
        auto list = http.array(host + "/versions/loader/" + game);
        require(!list.isEmpty(), "No compatible loader version");
        ver = list[0].toObject()["loader"].toObject()["version"].toString();
    }
    return http.json(host + "/versions/loader/" + game + "/" + ver + "/profile/json");
}
} // namespace neko
