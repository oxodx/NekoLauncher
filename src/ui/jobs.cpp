#include "ui/widgets.hpp"
#include "window.hpp"
using namespace neko;
using namespace neko::ui;

void Window::log(const QString &text) {
    QString clean = text;
    if (!processToken.isEmpty())
        clean.replace(processToken, "[redacted]");
    if (!account.refreshToken.isEmpty())
        clean.replace(account.refreshToken, "[redacted]");
    if (!account.token.isEmpty())
        clean.replace(account.token, "[redacted]");
    auto key = settings.value("curseKey").toString();
    if (!key.isEmpty())
        clean.replace(key, "[redacted]");
    console->appendPlainText(QTime::currentTime().toString("HH:mm:ss") + "  " + clean);
    status->setText(clean.section('\n', 0, 0));
    status->setToolTip(clean);
}
void Window::task(std::function<QVariant(Engine &)> work, std::function<void(QVariant)> done) {
    if (busy) {
        log("Another operation is still running.");
        return;
    }
    busy = true;
    cancellation = std::make_shared<std::atomic_bool>(false);
    auto cancelFlag = cancellation;
    cancel->show();
    progress->show();
    play->setEnabled(false);
    auto path = root;
    struct Result {
        QVariant value;
        QString error;
    };
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, done] {
        auto r = watcher->result();
        watcher->deleteLater();
        busy = false;
        progress->hide();
        cancel->hide();
        play->setEnabled(!game || game->state() == QProcess::NotRunning);
        if (!r.error.isEmpty()) {
            play->setText("Launch Minecraft");
            log(r.error);
            QMessageBox::warning(this, "Operation could not complete", r.error);
        } else {
            if (done)
                done(r.value);
            log("Operation complete.");
        }
    });
    watcher->setFuture(QtConcurrent::run([this, path, work, cancelFlag] {
        try {
            Engine e(path, [this](const QString &text) {
                QMetaObject::invokeMethod(this, [this, text] { log(text); }, Qt::QueuedConnection);
            });
            e.http.cancellation = cancelFlag;
            return Result{work(e), {}};
        } catch (const std::exception &e) {
            return Result{{}, QString::fromUtf8(e.what())};
        }
    }));
}
