#include "ui/storage.hpp"
#include "mod_storage.hpp"
#include "ui/widgets.hpp"
#include "window.hpp"
using namespace neko;
using namespace neko::ui;
void Window::storageAction(int action) {
    if (busy)
        return;
    if (action != 1 && game && game->state() != QProcess::NotRunning) {
        QMessageBox::information(this, "Close Minecraft first",
                                 "Close Minecraft before changing mod storage.");
        return;
    }
    const auto instance = selected();
    if (action >= 3 && instance.isEmpty()) {
        QMessageBox::information(this, "Select an instance", "Choose an instance in your library first.");
        return;
    }
    task(
        [=](Engine &engine) {
            ModStorage storage(engine.root, [&] { engine.http.checkCancelled(); }, engine.progress);
            if (action == 0)
                return QVariant(storage.optimizeAll());
            if (action == 2) {
                const auto bytes = storage.cleanUnused();
                auto report = storage.usage();
                report["reclaimedBytes"] = double(bytes);
                return QVariant(report);
            }
            if (action == 3)
                storage.makePrivate(instance["id"].toString());
            if (action == 4) {
                const auto path =
                    safePath(engine.root, "instances/" + instance["id"].toString() + "/instance.json");
                auto updated = readJson(path);
                updated["privateMods"] = false;
                writeJson(path, updated);
                storage.optimizeInstance(instance["id"].toString());
            }
            return QVariant(storage.usage());
        },
        [this](QVariant value) {
            reload();
            refreshMods();
            showStorageReport(this, value.toJsonObject());
        });
}
void neko::ui::showStorageReport(QWidget *parent, const Json &report) {
    QDialog dialog(parent);
    dialog.setObjectName("storageReport");
    dialog.setWindowTitle("Mod storage");
    dialog.resize(560, 420);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    layout->addWidget(label("More room for your worlds", "sectionTitle"));
    auto size = [&](const char *key) { return QLocale().formattedDataSize(qint64(report[key].toDouble())); };
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->addRow("Mod files", label(QString::number(report["files"].toInt())));
    form->addRow("Files using shared storage", label(QString::number(report["sharedFiles"].toInt())));
    form->addRow("Duplicate bytes saved", label(size("savedBytes"), "sectionTitle"));
    form->addRow("Shared object data", label(size("storeBytes")));
    form->addRow("Unused shared data", label(size("unusedBytes")));
    if (report.contains("reclaimedBytes"))
        form->addRow("Reclaimed in this cleanup", label(size("reclaimedBytes")));
    layout->addLayout(form);
    layout->addWidget(label("Sharing uses identical file contents, not filenames or version labels. Cleanup "
                            "removes only objects with no remaining hard links. Backups contain full mod "
                            "files and work independently of shared storage.",
                            "muted"));
    button("Close", layout, [&] { dialog.accept(); });
    dialog.exec();
}
