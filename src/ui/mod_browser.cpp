#include "ui/mod_browser.hpp"
#include "ui/widgets.hpp"
#include "window.hpp"
using namespace neko;
using namespace neko::ui;

void Window::refreshSearchTargets() {
    if (!searchTarget)
        return;
    auto previous = searchTarget->currentData().toJsonObject()["id"].toString();
    if (previous.isEmpty())
        previous = selected()["id"].toString();
    QSignalBlocker block(searchTarget);
    searchTarget->clear();
    int chosen = -1;
    const bool modsOnly = searchType->currentIndex() == 1;
    for (int i = 0; i < instances->count(); ++i) {
        const auto instance = instances->item(i)->data(Qt::UserRole).toJsonObject();
        if (modsOnly && !QStringList{"fabric", "quilt", "forge", "neoforge"}.contains(
                            instance["loader"].toString().toLower()))
            continue;
        searchTarget->addItem("Install into: " + instance["name"].toString() + " · " +
                                  instance["version"].toString() + " · " + instance["loader"].toString(),
                              instance);
        if (instance["id"].toString() == previous)
            chosen = searchTarget->count() - 1;
    }
    if (chosen >= 0)
        searchTarget->setCurrentIndex(chosen);
    if (!searchTarget->count())
        searchTarget->addItem(modsOnly ? "Create a modded instance to install mods"
                                      : "Create an instance before installing content");
}
void Window::updateSearchMode() {
    const int mode = searchType->currentIndex();
    const bool mod = mode == 1;
    const bool instanceContent = mode >= 1;
    refreshSearchTargets();
    searchTarget->setVisible(instanceContent);
    const QStringList nouns{"modpacks", "mods", "resource packs", "shaders"};
    const QStringList actions{"Install pack", "Install mod", "Install resource pack", "Install shader"};
    query->setPlaceholderText("Search " + nouns.value(mode, "modpacks") + "…");
    searchInstall->setText(actions.value(mode, "Install pack"));
    searchInstall->setToolTip(
        mod ? "Review the compatible release and required dependencies before installing into this instance."
        : mode == 2 ? "Download the latest compatible resource pack into the selected instance."
        : mode == 3 ? "Download the latest compatible shader into the selected instance."
                    : "Install a new modpack instance.");
    {
        QSignalBlocker versionBlock(searchVersion), loaderBlock(searchLoader), categoryBlock(searchCategory);
        searchVersion->setCurrentIndex(0);
        searchLoader->setCurrentIndex(0);
        const auto instance = searchTarget->currentData().toJsonObject();
        if (instanceContent && !instance.isEmpty()) {
            searchVersion->setCurrentText(instance["version"].toString());
            if (mod)
                searchLoader->setCurrentIndex(
                    qMax(0, searchLoader->findData(instance["loader"].toString().toLower())));
        }
        searchCategory->clear();
        searchCategory->addItem("All categories", "");
        const QStringList categories =
            mod ? QStringList{"Adventure",      "Cursed",       "Decoration",      "Equipment",  "Food",
                              "Game Mechanics", "Library",      "Magic",           "Management", "Minigame",
                              "Mobs",           "Optimization", "Social",          "Storage",    "Technology",
                              "Transportation", "Utility",      "World Generation"}
                : mode == 2 ? QStringList{"8x",          "16x",         "32x",        "64x",
                                          "128x",        "256x",        "512x+",      "Audio",
                                          "Blocks",      "Decoration",  "Entities",   "Environment",
                                          "Equipment",   "Fonts",       "GUI",        "Items",
                                          "Models",      "Modded",      "Themed",     "Tweaks",
                                          "Utility"}
                : mode == 3 ? QStringList{"Fantasy", "Realistic", "Vanilla-like", "Cartoon",
                                          "High", "Medium", "Low", "Potato"}
                : QStringList{"Adventure", "Challenging", "Combat",       "Kitchen Sink", "Lightweight",
                              "Magic",     "Multiplayer", "Optimization", "Quests",       "Technology"};
        for (const auto &category : categories)
            searchCategory->addItem(
                category, category == "World Generation" ? "worldgen" : category.toLower().replace(' ', '-'));
    }
    searchVersion->setEnabled(!instanceContent);
    searchLoader->setEnabled(mode == 0 && provider->currentIndex() == 0);
    scheduleSearch();
}
void Window::installSelectedMod() {
    if (busy)
        return;
    if (game && game->state() != QProcess::NotRunning) {
        QMessageBox::information(this, "Close Minecraft first",
                                 "Close the running game before changing mods.");
        return;
    }
    auto *item = results->currentItem();
    if (!item)
        return;
    const auto instance = searchTarget->currentData().toJsonObject();
    if (instance.isEmpty()) {
        QMessageBox::information(this, "Choose an instance",
                                 "Create a Fabric, Quilt, Forge or NeoForge instance first.");
        return;
    }
    const auto project = item->data(Qt::UserRole).toJsonObject();
    const bool curse = item->data(Qt::UserRole + 1).toBool();
    const auto key = settings.value("curseKey").toString();
    task([=](Engine &engine) { return QVariant(engine.resolveModInstall(instance, project, curse, key)); },
         [this, instance](QVariant value) {
             const auto plan = value.toJsonObject();
             if (!confirmModPlan(this, instance, plan))
                 return;
             if (game && game->state() != QProcess::NotRunning)
                 return;
             task([=](Engine &engine) { return QVariant(engine.installModPlan(instance, plan)); },
                  [this](QVariant value) {
                      const auto updated = value.toJsonObject();
                      reload();
                      for (int i = 0; i < instances->count(); ++i)
                          if (instances->item(i)->data(Qt::UserRole).toJsonObject()["id"] == updated["id"])
                              instances->setCurrentRow(i);
                      showPage(2);
                      missingDownloads(updated);
                  });
         });
}
void Window::installSelectedContent() {
    if (busy)
        return;
    if (game && game->state() != QProcess::NotRunning) {
        QMessageBox::information(this, "Close Minecraft first",
                                 "Close the running game before changing instance content.");
        return;
    }
    auto *item = results->currentItem();
    if (!item)
        return;
    const auto instance = searchTarget->currentData().toJsonObject();
    if (instance.isEmpty()) {
        QMessageBox::information(this, "Choose an instance", "Create an instance first.");
        return;
    }
    const int mode = searchType->currentIndex();
    const auto kind = mode == 2 ? QString("resourcepack") : QString("shader");
    const auto title = mode == 2 ? QString("Resource pack") : QString("Shader");
    const auto project = item->data(Qt::UserRole).toJsonObject();
    const bool curse = item->data(Qt::UserRole + 1).toBool();
    const auto key = settings.value("curseKey").toString();
    task(
        [=](Engine &engine) {
            return QVariant(engine.installContent(instance, project, kind, curse, key));
        },
        [this, instance, title](QVariant value) {
            reload();
            for (int i = 0; i < instances->count(); ++i)
                if (instances->item(i)->data(Qt::UserRole).toJsonObject()["id"] == instance["id"])
                    instances->setCurrentRow(i);
            QMessageBox::information(this, title + " installed",
                                     QFileInfo(value.toString()).fileName() + " was installed into " +
                                         instance["name"].toString() + ".");
        });
}
bool neko::ui::confirmModPlan(QWidget *parent, const Json &instance, const Json &plan) {
    QDialog dialog(parent);
    dialog.setObjectName("modInstallPlan");
    dialog.setWindowTitle("Install mods");
    dialog.resize(680, 480);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(14);
    layout->addWidget(label("Review installation", "sectionTitle"));
    layout->addWidget(label("Install into " + instance["name"].toString() + " · " +
                                instance["version"].toString() + " · " + instance["loader"].toString(),
                            "muted"));
    auto *list = new QTreeWidget;
    list->setHeaderLabels({"Mod / required dependency", "File", "Download"});
    list->setRootIsDecorated(false);
    for (const auto &value : plan["files"].toArray()) {
        auto file = value.toObject();
        auto *item = new QTreeWidgetItem(list, {file["title"].toString(), file["fileName"].toString(),
                                                file["manual"].toBool() ? "Browser required" : "Automatic"});
        item->setToolTip(1, file["fileName"].toString());
    }
    list->header()->setSectionResizeMode(QHeaderView::Interactive);
    list->header()->setStretchLastSection(true);
    list->setColumnWidth(0, 200);
    list->setColumnWidth(1, 240);
    layout->addWidget(list, 1);
    layout->addWidget(label("Includes required dependencies declared by the provider. Optional dependencies "
                            "are skipped. Existing files are preserved; conflicts stop installation. Mods "
                            "already added outside Neko can still conflict with these releases.",
                            "muted"));
    auto *buttons = new QHBoxLayout;
    layout->addLayout(buttons);
    button("Cancel", buttons, [&] { dialog.reject(); });
    button("Install mods", buttons, [&] { dialog.accept(); })->setObjectName("primary");
    return dialog.exec() == QDialog::Accepted;
}
