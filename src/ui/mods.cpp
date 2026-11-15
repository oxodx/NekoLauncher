#include "mod_storage.hpp"
#include "ui/widgets.hpp"
#include "window.hpp"
using namespace neko;
using namespace neko::ui;

QWidget *Window::modsPage() {
    QVBoxLayout *l;
    auto *w = page(l, "Your mods",
                   "Manage the mods in the selected instance. Double-click a mod to enable or disable it.");
    mods = new QListWidget;
    mods->setMinimumHeight(240);
    mods->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    l->addWidget(mods, 1);
    auto *buttons = new QHBoxLayout;
    l->addLayout(buttons);
    connect(mods, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (busy || (game && game->state() != QProcess::NotRunning))
            return;
        auto path = item->data(Qt::UserRole).toString();
        auto target = path.endsWith(".disabled") ? path.left(path.size() - 9) : path + ".disabled";
        if (!QFile::rename(path, target))
            QMessageBox::warning(this, "Mod toggle failed", "Could not rename mod file.");
        refreshMods();
    });
    button("Browse mods", buttons, [this] {
        showPage(1);
        searchType->setCurrentIndex(1);
        const auto id = selected()["id"].toString();
        for (int i = 0; i < searchTarget->count(); ++i)
            if (searchTarget->itemData(i).toJsonObject()["id"].toString() == id)
                searchTarget->setCurrentIndex(i);
    })->setObjectName("primary");
    button("Add .jar files…", buttons, [this] {
        auto j = selected();
        if (j.isEmpty() || busy || (game && game->state() != QProcess::NotRunning))
            return;
        auto files = QFileDialog::getOpenFileNames(this, "Add mods", {}, "Java mods (*.jar)");
        auto dir = root + "/instances/" + j["id"].toString() + "/mods";
        QDir().mkpath(dir);
        task(
            [=](Engine &engine) {
                ModStorage storage(engine.root, [&] { engine.http.checkCancelled(); }, engine.progress);
                for (const auto &file : files) {
                    engine.http.checkCancelled();
                    const auto target = safePath(dir, QFileInfo(file).fileName());
                    require(QFile::copy(file, target),
                            "File already exists or cannot be copied: " + QFileInfo(file).fileName());
                    storage.shareFile(target);
                }
                return QVariant{};
            },
            [this](QVariant) { refreshMods(); });
    });
    button("Refresh", buttons, [this] { refreshMods(); });
    l->addWidget(label("Use mods made for the instance’s Minecraft version and loader. Dependencies must be "
                       "included. Neko does not execute mods until you launch Minecraft.",
                       "muted"));
    return w;
}
void Window::refreshMods() {
    mods->clear();
    auto j = selected();
    if (j.isEmpty())
        return;
    QDir d(root + "/instances/" + j["id"].toString() + "/mods");
    for (const auto &file : d.entryList({"*.jar", "*.jar.disabled"}, QDir::Files | QDir::NoSymLinks)) {
        auto *item = new QListWidgetItem((file.endsWith(".disabled") ? "○  " : "●  ") + file);
        item->setData(Qt::UserRole, d.filePath(file));
        mods->addItem(item);
    }
}
