#include "core.hpp"
#include <algorithm>

namespace neko {
int javaMajor(const QString &output) {
    const auto match =
        QRegularExpression("(?:version\\s+\\\"?|^(?:openjdk|java)\\s+\\\"?)([0-9]+)(?:\\.([0-9]+))?",
                           QRegularExpression::MultilineOption)
            .match(output);
    if (!match.hasMatch())
        return 0;
    return match.captured(1) == "1" ? match.captured(2).toInt() : match.captured(1).toInt();
}
JavaRuntime inspectJava(const QString &executable) {
    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.start(executable, {"-version"});
    if (!probe.waitForFinished(5000)) {
        probe.kill();
        probe.waitForFinished();
        return {executable, 0};
    }
    if (probe.exitCode() != 0 || probe.exitStatus() != QProcess::NormalExit)
        return {executable, 0};
    return {executable, javaMajor(QString::fromUtf8(probe.readAll()))};
}
JavaRuntime selectJava(const QString &preferred, int required, bool automatic,
                       const QStringList &extraHomes) {
    auto current = inspectJava(preferred.isEmpty() ? "java" : preferred);
    if (current.major == required || (!automatic && current.major >= required))
        return current;
    require(automatic, QString("Selected Java %1 cannot run this instance (requires Java %2). Enable "
                               "automatic Java selection or choose a compatible executable.")
                           .arg(current.major ? QString::number(current.major) : "could not be started")
                           .arg(required));
    QStringList homes = extraHomes;
    homes << qEnvironmentVariable("JAVA_HOME") << QDir::homePath() + "/.jdks" << "/usr/lib/jvm"
          << "/usr/java" << "/opt/java" << "/run/current-system/sw/lib/openjdk"
          << "/Library/Java/JavaVirtualMachines";
#ifdef Q_OS_WIN
    for (const auto &env : {"ProgramFiles", "ProgramW6432", "LOCALAPPDATA"})
        for (const auto &vendor : {"Java", "Eclipse Adoptium", "Microsoft", "Programs/Eclipse Adoptium"})
            homes << qEnvironmentVariable(env) + "/" + vendor;
#endif
    QStringList candidates;
    for (const auto &home : homes) {
        if (home.isEmpty() || !QDir(home).exists())
            continue;
        candidates << home;
        for (const auto &entry : QDir(home).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            candidates << home + "/" + entry << home + "/" + entry + "/Contents/Home";
    }
#ifdef Q_OS_LINUX
    for (const auto &name : QDir("/nix/store")
                                .entryList({"*-openjdk-*", "*-jdk-*", "*-jre-*", "*-jetbrains-jdk-*"},
                                           QDir::Dirs | QDir::NoDotAndDotDot))
        candidates << "/nix/store/" + name;
#endif
    candidates.removeDuplicates();
    std::stable_sort(candidates.begin(), candidates.end(), [](const QString &a, const QString &b) {
        return a.contains("jetbrains", Qt::CaseInsensitive) < b.contains("jetbrains", Qt::CaseInsensitive);
    });
    for (const auto &home : candidates) {
#ifdef Q_OS_WIN
        const auto executable = home + "/bin/java.exe";
#else
        const auto executable = home + "/bin/java";
#endif
        if (!QFileInfo(executable).isExecutable())
            continue;
        QFile release(home + "/release");
        if (release.open(QIODevice::ReadOnly)) {
            const auto match = QRegularExpression("JAVA_VERSION=\"([0-9]+)(?:\\.([0-9]+))?")
                                   .match(QString::fromUtf8(release.readAll()));
            if (match.hasMatch()) {
                const auto major =
                    match.captured(1) == "1" ? match.captured(2).toInt() : match.captured(1).toInt();
                if (major != required)
                    continue;
            }
        }
        auto found = inspectJava(executable);
        if (found.major == required)
            return found;
    }
    throw Error(
        QString("This instance needs Java %1. No matching installed runtime was found. Install Java %1, or "
                "choose its executable in Edit instance → Runtime. Your current selection is %2.")
            .arg(required)
            .arg(current.major ? "Java " + QString::number(current.major) : "unavailable")
            .toStdString());
}

} // namespace neko
