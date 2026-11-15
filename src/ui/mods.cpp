#include "mod_storage.hpp"
#include "ui/widgets.hpp"
#include "window.hpp"
using namespace neko;
using namespace neko::ui;

QWidget *Window::modsPage() {
    QVBoxLayout *l;
    auto *w = page(l, "Your mods",
                   "Build your loadout. Browse, organize and toggle mods for the selected instance.");
    auto *filters = new QHBoxLayout;
    modSearch = entry({}, "Search installed mods…");
    modSearch->setProperty("searchField", true);
    modSearch->setClearButtonEnabled(true);
    filters->addWidget(modSearch, 1);
    modState = new QComboBox;
    modState->addItems({"All mods", "Enabled", "Disabled"});
    filters->addWidget(modState);
    button("Refresh", filters, [this] { refreshMods(); })->setToolTip("Refresh installed mods · F5");
    auto *refreshShortcut = new QShortcut(QKeySequence(Qt::Key_F5), w);
    refreshShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(refreshShortcut, &QShortcut::activated, this, [this] { refreshMods(); });
    auto *findShortcut = new QShortcut(QKeySequence::Find, w);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, modSearch, qOverload<>(&QWidget::setFocus));
    l->addLayout(filters);
    modSummary = label({}, "muted");
    l->addWidget(modSummary);
    connect(modSearch, &QLineEdit::textChanged, this, [this] { filterMods(); });
    connect(modState, &QComboBox::currentIndexChanged, this, [this] { filterMods(); });
    mods = new QListWidget;
    mods->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mods->setAlternatingRowColors(true);
    mods->setMinimumHeight(240);
    mods->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    l->addWidget(mods, 1);
    auto *buttons = new QHBoxLayout;
    l->addLayout(buttons);
    connect(mods, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (busy || (game && game->state() != QProcess::NotRunning))
            return;
        mods->clearSelection();
        item->setSelected(true);
        toggleMods(item->data(Qt::UserRole).toString().endsWith(".disabled"));
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
        if (files.isEmpty())
            return;
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
    button("Open folder", buttons, [this] { openInstanceFolder("mods"); });
    auto *selection = new QHBoxLayout;
    l->addLayout(selection);
    button("Enable selected", selection, [this] { toggleMods(true); });
    button("Disable selected", selection, [this] { toggleMods(false); });
    button("Select visible", selection, [this] {
        for (int i = 0; i < mods->count(); ++i)
            mods->item(i)->setSelected(!mods->item(i)->isHidden());
    });
    connect(mods, &QListWidget::itemSelectionChanged, this, [this] { filterMods(); });
    l->addWidget(label("Use mods made for the instance’s Minecraft version and loader. Dependencies must be "
                       "included. Neko does not execute mods until you launch Minecraft.",
                       "muted"));
    return w;
}
void Window::refreshMods() {
    mods->clear();
    auto j = selected();
    if (j.isEmpty()) {
        filterMods();
        return;
    }
    QDir d(root + "/instances/" + j["id"].toString() + "/mods");
    for (const auto &file : d.entryList({"*.jar", "*.jar.disabled"}, QDir::Files | QDir::NoSymLinks)) {
        auto *item = new QListWidgetItem((file.endsWith(".disabled") ? "○  " : "●  ") + file);
        item->setData(Qt::UserRole, d.filePath(file));
        item->setToolTip(file);
        mods->addItem(item);
    }
    filterMods();
}

void Window::filterMods() {
    if (!mods || !modSearch || !modState)
        return;
    int enabled = 0, visible = 0, selectedCount = 0;
    for (int i = 0; i < mods->count(); ++i) {
        auto *item = mods->item(i);
        const bool disabled = item->data(Qt::UserRole).toString().endsWith(".disabled");
        enabled += !disabled;
        const bool match = item->text().contains(modSearch->text().trimmed(), Qt::CaseInsensitive) &&
                           (modState->currentIndex() == 0 || disabled == (modState->currentIndex() == 2));
        item->setHidden(!match);
        visible += match;
        selectedCount += match && item->isSelected();
    }
    const auto instance = selected();
    modSummary->setText(instance.isEmpty() ? "Choose an instance in your library first."
                                           : QString("%1 · %2 enabled · %3 disabled · %4 shown · %5 selected")
                                                 .arg(instance["name"].toString())
                                                 .arg(enabled)
                                                 .arg(mods->count() - enabled)
                                                 .arg(visible)
                                                 .arg(selectedCount));
}

void Window::toggleMods(bool enabled) {
    if (busy || (game && game->state() != QProcess::NotRunning))
        return;
    QStringList failures;
    for (auto *item : mods->selectedItems()) {
        if (item->isHidden())
            continue;
        const auto path = item->data(Qt::UserRole).toString();
        const bool disabled = path.endsWith(".disabled");
        if (enabled != disabled)
            continue;
        const auto target = enabled ? path.left(path.size() - 9) : path + ".disabled";
        // Rename the directory entry; shared mod contents stay immutable.
        if (!QFile::rename(path, target))
            failures << QFileInfo(path).fileName();
    }
    refreshMods();
    if (!failures.isEmpty())
        QMessageBox::warning(this, "Some mods could not be changed",
                             "An existing file or file permissions prevented toggling:\n" +
                                 failures.join('\n'));
}
