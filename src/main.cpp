#include "window.hpp"
int main(int argc, char **argv) {
    // A tiny child process used by the GUI diagnostic to test process lifecycle without launching a game.
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--process-test-exit") {
        bool valid = false;
        const auto code = QString::fromLocal8Bit(argv[2]).toInt(&valid);
        return valid && code >= 0 && code <= 255 ? code : 2;
    }
    QApplication app(argc, argv);
    QApplication::setStyle("Fusion");
#ifdef NEKO_IMAGE_PLUGIN_PATH
    QCoreApplication::addLibraryPath(QString::fromUtf8(NEKO_IMAGE_PLUGIN_PATH));
#endif
    QCoreApplication::setOrganizationName("NekoLauncher");
    QCoreApplication::setApplicationName("NekoLauncher");
    QCoreApplication::setApplicationVersion("0.1.0");
    Window w;
    w.show();
    if (app.arguments().contains("--ui-test")) {
        QTimer::singleShot(300, &w, [&] { app.exit(w.verifyUi("build-verified/ui-previews") ? 1 : 0); });
    }
    if (app.arguments().contains("--smoke-test")) {
        QTimer::singleShot(300, &w, [&] {
            w.grab().save("neko-preview.png");
            app.quit();
        });
    }
    return app.exec();
}
