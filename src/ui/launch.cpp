#include "instance_services.hpp"
#include "ui/widgets.hpp"
#include "window.hpp"
using namespace neko;
using namespace neko::ui;

void Window::launch() {
    auto j = selected();
    if (j.isEmpty() || busy || (game && game->state() != QProcess::NotRunning))
        return;
    if (!j["pendingDownloads"].toArray().isEmpty()) {
        missingDownloads(j);
        return;
    }
    bool local = offline->isChecked();
    auto a = account;
    if (local) {
        auto name = offlineName->text();
        if (!offlineName->hasAcceptableInput()) {
            QMessageBox::warning(this, "Invalid player name", "Use 1–16 letters, numbers or underscores.");
            return;
        }
        a = {name, offlineUuid(name), {}, 0};
    }
    processToken = a.token;
    graphicsFailed = false;
    play->setText("Preparing…");
    auto prepared = std::make_shared<Launch>();
    auto refreshed = std::make_shared<Account>(a);
    task(
        [this, j, local, prepared, refreshed](Engine &e) {
            if (!local && refreshed->expires <= QDateTime::currentSecsSinceEpoch() + 120) {
                *refreshed = e.refresh(*refreshed);
                const auto saved = *refreshed;
                QMetaObject::invokeMethod(
                    this,
                    [this, saved] {
                        account = saved;
                        saveAccount();
                    },
                    Qt::QueuedConnection);
            }
            auto required =
                e.version(j["version"].toString())["javaVersion"].toObject()["majorVersion"].toInt(8);
            auto runtime = selectJava(j["java"].toString("java"), required, j["javaAuto"].toBool(true),
                                      {e.root + "/runtimes"});
            auto configured = j;
            configured["java"] = runtime.executable;
            if (e.progress)
                e.progress(QString("Using Java %1: %2").arg(runtime.major).arg(runtime.executable));
            if (j["snapshotBeforeLaunch"].toBool()) {
                const auto saves = safePath(e.root, "instances/" + j["id"].toString() + "/saves");
                QDirIterator files(saves, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
                if (files.hasNext()) {
                    if (e.progress)
                        e.progress("Protecting your worlds before launch…");
                    WorldSnapshots(e.root, [&e] {
                        e.http.checkCancelled();
                    }).create(j["id"].toString(), true);
                }
            }
            *prepared = e.prepare(configured, *refreshed, local);
            return QVariant{};
        },
        [this, prepared, j, refreshed, local](QVariant) {
            if (!local) {
                account = *refreshed;
                processToken = account.token;
            }
            try {
                auto updated = readJson(safePath(root, "instances/" + j["id"].toString() + "/instance.json"));
                updated["lastJava"] = prepared->java;
                saveInstance(updated);
                reload();
            } catch (const std::exception &e) {
                log(e.what());
            }
            startGame(*prepared, j);
        });
}

void Window::stopGame() {
    if (!game || game->state() == QProcess::NotRunning)
        return;
    stopRequested = true;
    game->terminate();
}

void Window::startGame(const Launch &plan, const Json &j) {
    if (game)
        game->deleteLater();
    game = new QProcess(this);
    game->setWorkingDirectory(plan.directory);
    game->setProcessEnvironment(plan.environment);
    game->setProcessChannelMode(QProcess::MergedChannels);
    sessionActive = false;
    stopRequested = false;
    connect(game, &QProcess::started, this, [this, id = j["id"].toString()] {
        activeInstanceId = id;
        sessionStartedAt = QDateTime::currentSecsSinceEpoch();
        sessionClock.start();
        sessionActive = true;
        try {
            auto fresh = readJson(safePath(root, "instances/" + id + "/instance.json"));
            fresh["lastPlayed"] = double(sessionStartedAt);
            saveInstance(fresh);
            reload();
        } catch (const std::exception &error) {
            log(error.what());
        }
    });
    auto audioHintShown = std::make_shared<bool>(false);
    connect(game, &QProcess::readyReadStandardOutput, this, [this, audioHintShown] {
        auto output = QString::fromUtf8(game->readAllStandardOutput());
        if ((output.contains("No supported graphics backend") || output.contains("EGL_BAD_DISPLAY") ||
             output.contains("Failed to create backend OpenGL")))
            graphicsFailed = true;
        log(output);
        if (!*audioHintShown && (output.contains("Failed to open OpenAL device") ||
                                 output.contains("Error starting SoundSystem"))) {
            *audioHintShown = true;
            log("Minecraft could not open an audio device. Check your desktop audio output and service. Edit "
                "instance → Runtime → Linux audio backend can select PipeWire or PulseAudio; restart the "
                "game after changing it. NixOS builds also need the audio runtime libraries from shell.nix.");
        }
    });
    connect(game, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        log("Game process error: " + game->errorString());
        play->setEnabled(true);
        stop->setEnabled(false);
        heroStop->hide();
        play->setText("Launch Minecraft");
    });
    connect(game, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus exitStatus) {
                if (sessionActive) {
                    sessionActive = false;
                    try {
                        recordSession(root, activeInstanceId,
                                      {sessionStartedAt, sessionClock.elapsed() / 1000, code,
                                       exitStatus == QProcess::CrashExit, game->program(), stopRequested});
                        reload();
                    } catch (const std::exception &error) {
                        log(QString("Could not save session history: ") + error.what());
                    }
                }
                log(QString("Minecraft exited with code %1.").arg(code));
                play->setEnabled(true);
                stop->setEnabled(false);
                heroStop->hide();
                play->setText("Launch Minecraft");
                if (graphicsFailed) {
                    log("Graphics initialization reported errors. In Edit instance → Runtime, try "
                        "X11/XWayland or check native library paths. Exit code 0 does not guarantee "
                        "a window was created. If Iris reports no current OpenGL context after "
                        "Vulkan fallback, "
                        "fix the OpenGL display initialization first; Iris needs an OpenGL context.");
                    showPage(5);
                }
            });
    game->start(plan.java, plan.arguments);
    play->setEnabled(false);
    stop->setEnabled(true);
    heroStop->show();
    play->setText("Running");
    log("Starting Minecraft…");
}
