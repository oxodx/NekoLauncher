#include "core.hpp"

namespace neko {
QProcessEnvironment launchEnvironment(const Json &instance, QProcessEnvironment inherited) {
    const auto overrides = instance["environment"].toObject();
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        inherited.insert(it.key(), it.value().toString());
#ifdef Q_OS_LINUX
    const auto audio = instance["linuxAudio"].toString("auto");
    require(QStringList{"auto", "pipewire", "pulse", "alsa"}.contains(audio), "Invalid Linux audio backend");
    if (audio != "auto")
        inherited.insert("ALSOFT_DRIVERS", audio);
    const auto display = instance["linuxDisplay"].toString("auto");
    require(display == "auto" || display == "x11" || display == "wayland", "Invalid Linux display backend");
    if (display != "auto")
        inherited.insert("SDL_VIDEODRIVER", display);
    if (QFile::exists("/etc/NIXOS")) {
        const QMap<QString, QString> defaults{
            {"__EGL_VENDOR_LIBRARY_DIRS", "/run/opengl-driver/share/glvnd/egl_vendor.d"},
            {"__EGL_EXTERNAL_PLATFORM_CONFIG_DIRS", "/run/opengl-driver/share/egl/egl_external_platform.d"}};
        for (auto it = defaults.begin(); it != defaults.end(); ++it)
            if (!inherited.contains(it.key()) && QDir(it.value()).exists())
                inherited.insert(it.key(), it.value());
    }
    QStringList paths = instance["nativePaths"].toString().split(QDir::listSeparator(), Qt::SkipEmptyParts);
    if (QFile::exists("/etc/NIXOS") && QDir("/run/opengl-driver/lib").exists())
        paths << "/run/opengl-driver/lib";
#ifdef NEKO_VULKAN_LIBRARY_PATH
    if (QDir(QString::fromUtf8(NEKO_VULKAN_LIBRARY_PATH)).exists())
        paths << QString::fromUtf8(NEKO_VULKAN_LIBRARY_PATH);
#endif
#ifdef NEKO_NATIVE_LIBRARY_PATHS
    for (const auto &path : QString::fromUtf8(NEKO_NATIVE_LIBRARY_PATHS).split(':', Qt::SkipEmptyParts))
        if (QDir(path).exists())
            paths << path;
#endif
    const auto previous = inherited.value("LD_LIBRARY_PATH");
    if (!previous.isEmpty())
        paths << previous;
    paths.removeDuplicates();
    if (!paths.isEmpty())
        inherited.insert("LD_LIBRARY_PATH", paths.join(':'));
#endif
    return inherited;
}
Launch Engine::prepare(const Json &instance, const Account &account, bool offline) {
    require(instance["pendingDownloads"].toArray().isEmpty(),
            "This pack needs manual downloads. Complete missing downloads from the instance menu before "
            "launching.");
    require(!account.name.isEmpty(), "Choose an account first");
    require(offline || (!account.token.isEmpty() && account.expires > QDateTime::currentSecsSinceEpoch()),
            "Microsoft session expired. Sign in again, or select offline mode.");
    auto id = instance["version"].toString();
    report("Resolving Minecraft " + id);
    auto v = version(id);
    auto dir = safePath(root, "instances/" + instance["id"].toString());
    QDir().mkpath(dir);
    Json profile;
    auto custom = instance["profile"].toObject();
    if (!custom.isEmpty())
        profile = custom;
    else if (instance["loader"].toString("Vanilla") != "Vanilla") {
        auto p = dir + "/loader.json";
        if (QFile::exists(p))
            profile = readJson(p);
        else {
            profile =
                (instance["loader"] == "Forge" || instance["loader"] == "NeoForge")
                    ? installForge(instance, v)
                    : loaderProfile(id, instance["loader"].toString(), instance["loaderVersion"].toString());
            writeJson(p, profile);
        }
    }
    auto libs = v["libraries"].toArray();
    for (const auto &l : profile["libraries"].toArray()) {
        auto key = [](const QJsonValue &v) {
            auto p = v.toObject()["name"].toString().split(':');
            return p.value(0) + ":" + p.value(1) + ":" + p.value(3);
        };
        for (int i = libs.size() - 1; i >= 0; --i)
            if (key(libs[i]) == key(l))
                libs.removeAt(i);
        libs.append(l);
    }
    auto natives = dir + "/natives";
    QDir().mkpath(natives);
    for (const auto &child : QStringList{"java", "jna", "lwjgl", "netty"})
        require(QDir().mkpath(natives + "/" + child), "Cannot create native directory");
    QStringList cp;
    for (const auto &item : libs) {
        const auto l = item.toObject();
        if (!allowed(l, platform(), architecture()))
            continue;
        auto downloads = l["downloads"].toObject();
        auto art = downloads["artifact"].toObject();
        if (art.isEmpty() && !l.contains("downloads")) {
            auto parts = l["name"].toString().split(':');
            require(parts.size() >= 3 && parts.size() <= 4, "Invalid Maven coordinate");
            auto group = parts[0];
            group.replace('.', '/');
            auto file = parts[1] + "-" + parts[2] + (parts.size() == 4 ? "-" + parts[3] : "") + ".jar";
            auto path = group + "/" + parts[1] + "/" + parts[2] + "/" + file;
            art = {
                {"sha1", l["sha1"]},
                {"path", path},
                {"url", l["url"].toString("https://libraries.minecraft.net/") +
                            (l["url"].toString("https://libraries.minecraft.net/").endsWith('/') ? "" : "/") +
                            path}};
        }
        if (!art.isEmpty()) {
            auto path = safePath(root, "libraries/" + art["path"].toString());
            report("Library: " + QFileInfo(path).fileName());
            artifact(art, path);
            cp << path;
        }
        auto classifier = l["natives"].toObject()[platform()].toString();
        classifier.replace("${arch}", QSysInfo::WordSize == 64 ? "64" : "32");
        if (!classifier.isEmpty()) {
            auto n = downloads["classifiers"].toObject()[classifier].toObject();
            require(!n.isEmpty(), "Native library unavailable for this platform");
            auto path = safePath(root, "libraries/" + n["path"].toString());
            artifact(n, path);
            extract(path, natives, {}, true);
        }
    }
    auto jar = safePath(root, "versions/" + id + "/" + id + ".jar");
    report("Verifying game client");
    artifact(v["downloads"].toObject()["client"].toObject(), jar);
    cp << jar;
    const auto index = v["assetIndex"].toObject();
    auto assetId = index["id"].toString();
    if (!index.isEmpty()) {
        auto path = safePath(root, "assets/indexes/" + assetId + ".json");
        artifact(index, path);
        auto assets = readJson(path);
        auto objects = assets["objects"].toObject();
        int count = 0;
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            auto hash = it.value().toObject()["hash"].toString();
            require(QRegularExpression("^[a-f0-9]{40}$").match(hash).hasMatch(), "Invalid asset hash");
            auto obj = root + "/assets/objects/" + hash.left(2) + "/" + hash;
            if (++count % 100 == 0)
                report(QString("Assets %1 / %2").arg(count).arg(objects.size()));
            http.download("https://resources.download.minecraft.net/" + hash.left(2) + "/" + hash, obj, hash);
            for (const auto &target :
                 QStringList{assets["virtual"].toBool() ? root + "/assets/virtual/" + assetId : QString{},
                             assets["map_to_resources"].toBool() ? dir + "/resources" : QString{}})
                if (!target.isEmpty()) {
                    auto dest = safePath(target, it.key());
                    QDir().mkpath(QFileInfo(dest).absolutePath());
                    if (!QFile::exists(dest))
                        require(QFile::copy(obj, dest), "Cannot map legacy asset");
                }
        }
    }
#ifdef Q_OS_WIN
    auto separator = ";";
#else
    auto separator = ":";
#endif
    QMap<QString, QString> vars{
        {"auth_player_name", account.name},
        {"auth_uuid", account.uuid},
        {"auth_access_token", offline ? "0" : account.token},
        {"auth_session", offline ? "0" : "token:" + account.token + ":" + account.uuid},
        {"user_type", offline ? "legacy" : "msa"},
        {"user_properties", "{}"},
        {"version_name", profile["id"].toString(id)},
        {"version_type", v["type"].toString()},
        {"game_directory", dir},
        {"assets_root", root + "/assets"},
        {"assets_index_name", assetId},
        {"game_assets", root + "/assets/virtual/" + assetId},
        {"natives_directory", natives},
        {"launcher_name", "NekoLauncher"},
        {"launcher_version", "0.1.0"},
        {"classpath", cp.join(separator)},
        {"classpath_separator", separator},
        {"library_directory", root + "/libraries"},
        {"resolution_width", QString::number(instance["width"].toInt(1280))},
        {"resolution_height", QString::number(instance["height"].toInt(720))},
        {"clientid", ""},
        {"auth_xuid", ""}};
    auto substitute = [&](QString arg) {
        for (auto it = vars.begin(); it != vars.end(); ++it)
            arg.replace("${" + it.key() + "}", it.value());
        require(!arg.contains("${"), "Unsupported launch placeholder: " + arg);
        return arg;
    };
    auto arguments = [&](const QJsonArray &list) {
        QStringList result;
        for (const auto &a : list) {
            if (a.isString())
                result << substitute(a.toString());
            else if (allowed(a.toObject(), platform(), architecture())) {
                auto val = a.toObject()["value"];
                if (val.isArray())
                    for (const auto &x : val.toArray())
                        result << substitute(x.toString());
                else
                    result << substitute(val.toString());
            }
        }
        return result;
    };
    QStringList args;
    auto modern = v["arguments"].toObject();
    if (modern.contains("jvm"))
        args = arguments(modern["jvm"].toArray());
    else
        args << "-Djava.library.path=" + natives << "-cp" << cp.join(separator);
    args << "-Xms" + QString::number(instance["minMemory"].toInt(512)) + "M"
         << "-Xmx" + QString::number(instance["memory"].toInt(4096)) + "M";
    auto loaderJvm = arguments(profile["arguments"].toObject()["jvm"].toArray());
    // BootstrapLauncher excludes the original client by filename. Our client keeps
    // the base version name even when the loader profile has a different ID.
    for (auto &argument : loaderJvm)
        if (argument.startsWith("-DignoreList=")) {
            const auto clientName = QFileInfo(jar).fileName();
            if (!argument.mid(13).split(',').contains(clientName))
                argument += "," + clientName;
        }
    args << loaderJvm;
    args << QProcess::splitCommand(instance["jvmArgs"].toString());
    auto log = v["logging"].toObject()["client"].toObject();
    if (!log.isEmpty()) {
        auto f = log["file"].toObject();
        auto path = safePath(root, "assets/log_configs/" + f["id"].toString());
        artifact(f, path);
        auto a = log["argument"].toString();
        a.replace("${path}", path);
        args << a;
    }
    args << profile["mainClass"].toString(v["mainClass"].toString());
    if (modern.contains("game"))
        args << arguments(modern["game"].toArray());
    else
        for (const auto &a : QProcess::splitCommand(
                 profile["minecraftArguments"].toString(v["minecraftArguments"].toString())))
            args << substitute(a);
    args << arguments(profile["arguments"].toObject()["game"].toArray());
    args << "--width" << vars["resolution_width"] << "--height" << vars["resolution_height"];
    args << QProcess::splitCommand(instance["gameArgs"].toString());
    report("Game files verified. Launch configuration is ready.");
    return {instance["java"].toString("java"), dir, args, launchEnvironment(instance)};
}
} // namespace neko
