#include "core.hpp"
#include <iostream>
using namespace neko;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 3 && args[1] == "--child") {
        QLibrary library(args[2]);
        if (!library.load()) {
            std::cerr << library.errorString().toStdString() << '\n';
            return 1;
        }
        using Open = void *(*)(const char *);
        using Close = char (*)(void *);
        auto open = reinterpret_cast<Open>(library.resolve("alcOpenDevice"));
        auto close = reinterpret_cast<Close>(library.resolve("alcCloseDevice"));
        if (!open || !close) {
            std::cerr << "OpenAL symbols missing\n";
            return 1;
        }
        auto *device = open(nullptr);
        if (!device) {
            std::cerr << "Failed to open default OpenAL device\n";
            return 1;
        }
        close(device);
        std::cout << "Default OpenAL device opened and closed successfully (no sound played).\n";
        return 0;
    }
    if (args.size() != 2) {
        std::cerr << "Usage: audio-tests /absolute/path/to/game/libopenal.so\n";
        return 2;
    }
    auto run = [&](const QString &label, QProcessEnvironment environment) {
        environment.insert("ALSOFT_LOGLEVEL", "3");
        QProcess process;
        process.setProcessEnvironment(environment);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(app.applicationFilePath(), {"--child", args[1]});
        if (!process.waitForStarted(5000) || !process.waitForFinished(15000)) {
            process.kill();
            process.waitForFinished();
            std::cerr << "OpenAL probe timed out\n";
            return false;
        }
        std::cout << "=== " << label.toStdString() << " ===\n" << process.readAll().toStdString() << '\n';
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    run("Inherited environment", QProcessEnvironment::systemEnvironment());
    return run("Neko game environment", launchEnvironment(Json{})) ? 0 : 1;
}
