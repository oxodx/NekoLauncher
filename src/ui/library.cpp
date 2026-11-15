#include "cards.hpp"
#include "instance_services.hpp"
#include "mod_storage.hpp"
#include "ui/widgets.hpp"
#include "window.hpp"
using namespace neko;
using namespace neko::ui;

QWidget *Window::libraryPage() {
    QVBoxLayout *l;
    auto *w = new QWidget;
    w->setObjectName("libraryPage");
    auto *lLocal = new QVBoxLayout(w);
    l = lLocal;
    l->setContentsMargins(30, 24, 30, 28);
    l->setSpacing(24);
    l->setSizeConstraint(QLayout::SetMinimumSize);

    auto *header = new QBoxLayout(QBoxLayout::LeftToRight);
    libraryHeader = header;
    header->setSpacing(16);
    l->addLayout(header);
    auto *intro = new QVBoxLayout;
    intro->setSpacing(4);
    intro->addWidget(label("Your library", "libraryTitle"));
    intro->addWidget(label("Your worlds. Your mods. Your Minecraft.", "muted"));
    header->addLayout(intro, 1);
    auto *headerActions = new QHBoxLayout;
    headerActions->setSpacing(8);
    header->addLayout(headerActions);
    auto *toolbar = new QBoxLayout(QBoxLayout::LeftToRight);
    libraryToolbar = toolbar;
    l->addLayout(toolbar);
    auto *filter = entry({}, "Search your library…");
    instanceSearch = filter;
    filter->setProperty("searchField", true);
    toolbar->addWidget(filter, 1);
    connect(filter, &QLineEdit::textChanged, this, [this] { filterInstances(); });
    button("+ New instance", headerActions, [this] { newInstance(); })->setObjectName("secondaryAccent");
    button("Import pack", headerActions, [this] { importPack(); });
    auto *split = new QBoxLayout(QBoxLayout::LeftToRight);
    librarySplit = split;
    l->addLayout(split, 1);
    instances = new InstanceList;
    instances->setSpacing(4);
    instances->setMouseTracking(true);
    instances->setMinimumWidth(0);
    instances->setMinimumHeight(218);
    instances->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    instances->setItemDelegate(new InstanceDelegate(instances, [this](QModelIndex index) {
        instances->setCurrentRow(index.row());
        launch();
    }));
    split->addWidget(instances, 3);
    auto *card = new LandscapeCard;
    heroCard = card;
    card->setObjectName("card");
    card->setMinimumWidth(0);
    card->setMinimumHeight(246);
    auto *heroRow = new QBoxLayout(QBoxLayout::LeftToRight, card);
    heroLayout = heroRow;
    heroRow->setContentsMargins(30, 28, 30, 28);
    heroRow->setSpacing(14);
    auto *heroText = new QWidget;
    heroText->setObjectName("heroText");
    heroRow->addWidget(heroText, 1);
    auto *info = new QVBoxLayout(heroText);
    auto *actionPanel = new QWidget;
    actionPanel->setObjectName("heroActions");
    heroRow->addWidget(actionPanel, 0, Qt::AlignLeft);
    auto *actions = new QBoxLayout(QBoxLayout::LeftToRight, actionPanel);
    heroActionsLayout = actions;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(10);

    info->setContentsMargins(0, 0, 0, 0);
    info->setSpacing(10);
    auto *kicker = label("JUMP BACK IN", "heroKicker");
    info->addWidget(kicker);
    heading = new ElidedLabel("Welcome to your library");
    heading->setObjectName("hero");
    heading->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    heading->setMinimumWidth(0);
    info->addWidget(heading);
    detail =
        label("Create a vanilla instance or import a Modrinth / CurseForge modpack to get started.", "muted");
    info->addWidget(detail);
    info->addStretch();
    play = button("Launch Minecraft", actions, [this] { launch(); });
    play->setMinimumWidth(204);
    play->setMinimumHeight(48);
    play->setObjectName("primary");
    heroStop = button("Stop", actions, [this] {
        if (game && game->state() != QProcess::NotRunning)
            stopGame();
    });
    heroStop->setObjectName("heroStop");
    heroStop->hide();
    auto *edit = button("", actions, [this] { editInstance(); });
    edit->setObjectName("heroIcon");
    edit->setProperty("glyph", int(Glyph::Edit));
    edit->setToolTip("Edit instance · Ctrl+E");
    edit->setFixedSize(48, 48);
    button("Advanced instance JSON…", actions, [this] {
        auto j = selected();
        if (j.isEmpty() || busy || (game && game->state() != QProcess::NotRunning))
            return;
        QDialog dialog(this);
        dialog.setWindowTitle("Advanced instance configuration");
        dialog.resize(700, 600);
        auto *layout = new QVBoxLayout(&dialog);
        layout->addWidget(
            label("Edit all instance fields, including an optional loader profile. Keep the instance ID "
                  "unchanged. Invalid launch settings can prevent the game from starting.",
                  "muted"));
        auto *editor = new QPlainTextEdit(QString::fromUtf8(QJsonDocument(j).toJson()));
        layout->addWidget(editor);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            QJsonParseError error;
            auto doc = QJsonDocument::fromJson(editor->toPlainText().toUtf8(), &error);
            auto edited = doc.object();
            if (error.error != QJsonParseError::NoError || !doc.isObject() || edited["id"] != j["id"] ||
                edited["name"].toString().trimmed().isEmpty() || edited["version"].toString().isEmpty()) {
                QMessageBox::warning(
                    &dialog, "Invalid configuration",
                    "Use a JSON object with an unchanged ID, a name and a Minecraft version.");
                return;
            }
            try {
                saveInstance(edited);
                QFile::remove(root + "/instances/" + j["id"].toString() + "/loader.json");
                dialog.accept();
                reload();
            } catch (const std::exception &e) {
                QMessageBox::warning(&dialog, "Cannot save", e.what());
            }
        });
        dialog.exec();
    });
    button("Open instance folder", actions, [this] {
        auto j = selected();
        if (!j.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(root + "/instances/" + j["id"].toString()));
    });
    button("Duplicate", actions, [this] {
        auto instance = selected();
        if (instance.isEmpty() || busy || (game && game->state() != QProcess::NotRunning))
            return;
        const auto original = instance["id"].toString();
        instance["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        instance["name"] = instance["name"].toString() + " (copy)";
        task(
            [=](Engine &engine) {
                const auto source = safePath(engine.root, "instances/" + original);
                const auto destination = safePath(engine.root, "instances/" + instance["id"].toString());
                bool complete = false;
                auto rollback = qScopeGuard([&] {
                    if (!complete)
                        QDir(destination).removeRecursively();
                });
                writeJson(destination + "/instance.json", instance);
                ModStorage storage(engine.root, [&] { engine.http.checkCancelled(); }, engine.progress);
                QDirIterator files(source, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
                while (files.hasNext()) {
                    engine.http.checkCancelled();
                    const auto file = files.next(), relative = QDir(source).relativeFilePath(file);
                    if (relative == "instance.json" || relative.startsWith("natives/") ||
                        relative.startsWith("logs/"))
                        continue;
                    const auto target = safePath(destination, relative);
                    if (relative.startsWith("mods/") &&
                        (relative.endsWith(".jar", Qt::CaseInsensitive) ||
                         relative.endsWith(".jar.disabled", Qt::CaseInsensitive)))
                        storage.cloneFile(file, target);
                    else {
                        require(QDir().mkpath(QFileInfo(target).absolutePath()),
                                "Cannot create duplicate directory");
                        require(QFile::copy(file, target), "Cannot duplicate " + relative);
                    }
                }
                complete = true;
                return QVariant{};
            },
            [this](QVariant) { reload(); });
    });
    button("Delete instance…", actions, [this] {
        auto j = selected();
        if (j.isEmpty() || busy || (game && game->state() != QProcess::NotRunning))
            return;
        if (QMessageBox::warning(this, "Delete instance",
                                 "Permanently delete this instance, including saves and mods?",
                                 QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes)
            return;
        try {
            require(QDir(safePath(root, "instances/" + j["id"].toString())).removeRecursively(),
                    "Could not delete instance");
            reload();
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Delete failed", e.what());
        }
    });
    // Keep secondary actions available without crowding the launch card.
    auto *more = new QPushButton;
    more->setObjectName("heroIcon");
    more->setProperty("glyph", int(Glyph::More));
    more->setFixedSize(48, 48);
    more->setToolTip("Manage instance");
    auto *menu = new QMenu(more);
    for (int i = actions->count() - 1; i >= 0; --i) {
        auto *actionButton = qobject_cast<QPushButton *>(actions->itemAt(i)->widget());
        if (!actionButton)
            continue;
        if (actionButton->text().startsWith("Advanced") || actionButton->text() == "Duplicate" ||
            actionButton->text().startsWith("Delete") || actionButton->text().startsWith("Open instance")) {
            auto *action = menu->addAction(actionButton->text());
            connect(action, &QAction::triggered, actionButton, &QPushButton::click);
            actions->removeWidget(actionButton);
            actionButton->hide();
        }
    }
    instanceActions = menu;
    menu->addSeparator();
    menu->addAction("Check instance health", this, &Window::checkInstance);
    menu->addAction("World snapshots", this, &Window::worldSnapshots);
    menu->addAction("Session history", this, &Window::showHistory);
    more->setMenu(menu);
    actions->addWidget(more);
    split->removeWidget(instances);
    l->removeItem(toolbar);
    split->insertWidget(0, card);
    auto *collection = new QWidget;
    auto *collectionLayout = new QVBoxLayout(collection);
    collectionLayout->setContentsMargins(0, 0, 0, 0);
    collectionLayout->setSpacing(10);
    auto *collectionTitle = new QHBoxLayout;
    collectionLayout->addLayout(collectionTitle);
    collectionTitle->addWidget(label("All instances", "sectionTitle"), 1);
    collectionCount = label("0 instances", "muted");
    collectionCount->setWordWrap(false);
    collectionTitle->addWidget(collectionCount);
    collectionLayout->addLayout(toolbar);
    auto *organize = new QGridLayout;
    collectionGrid = organize;
    organize->setHorizontalSpacing(12);
    organize->setVerticalSpacing(8);
    collectionLayout->addLayout(organize);
    groupFilter = new QComboBox;
    groupFilter->addItem("All groups");
    organize->addWidget(groupFilter, 0, 0);
    favoritesOnly = new QCheckBox("Favorites");
    organize->addWidget(favoritesOnly, 0, 1);
    sortOrder = new QComboBox;
    sortOrder->addItems({"Name", "Recently played", "Favorites first"});
    organize->addWidget(sortOrder, 0, 2);
    auto *view = new QComboBox;
    view->addItems({"Grid view", "List view"});
    view->setCurrentIndex(settings.value("libraryView", 0).toInt());
    organize->addWidget(view, 0, 3);
    viewMode = view;
    auto changeView = [this](int index) {
        instances->setViewMode(index == 0 ? QListView::IconMode : QListView::ListMode);
        instances->setResizeMode(QListView::Adjust);
        instances->setMovement(QListView::Static);
        instances->setWrapping(index == 0);
        instances->setGridSize(
            index == 0 ? QSize(240, qMax(238, QFontMetrics(instances->font()).height() * 3 + 164)) : QSize{});
        instances->doItemsLayout();
        settings.setValue("libraryView", index);
    };
    changeView(view->currentIndex());
    connect(view, qOverload<int>(&QComboBox::currentIndexChanged), this, changeView);
    connect(groupFilter, &QComboBox::currentTextChanged, this, [this] { filterInstances(); });
    connect(favoritesOnly, &QCheckBox::toggled, this, [this] { filterInstances(); });
    connect(sortOrder, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { reload(); });
    collectionLayout->addWidget(instances, 1);
    split->addWidget(collection, 1);
    instances->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(instances, &QWidget::customContextMenuRequested, this, &Window::instanceMenu);

    auto shortcut = [this](QKeySequence key, std::function<void()> action) {
        auto *s = new QShortcut(key, this);
        connect(s, &QShortcut::activated, this, action);
    };
    shortcut(QKeySequence("Ctrl+N"), [this] { newInstance(); });
    shortcut(QKeySequence("Ctrl+I"), [this] { importPack(); });
    shortcut(QKeySequence("Ctrl+E"), [this] { editInstance(); });
    shortcut(QKeySequence("Ctrl+L"), [this] {
        showPage(0);
        instanceSearch->setFocus();
    });

    connect(instances, &QListWidget::currentItemChanged, this, [this] {
        auto j = selected();
        if (!j.isEmpty())
            settings.setValue("lastInstance", j["id"].toString());
        heading->setText(j.isEmpty() ? "Welcome to your library" : j["name"].toString());
        heading->setToolTip(heading->text());
        heroCard->setProperty("cover", instances->currentItem()
                                           ? instances->currentItem()->data(Qt::UserRole + 2)
                                           : QVariant{});
        heroCard->update();
        if (auto *kicker = heroCard->findChild<QLabel *>("heroKicker"))
            kicker->setText(j["lastPlayed"].toDouble() > 0 ? "CONTINUE PLAYING" : "SELECTED INSTANCE");
        detail->setText(j.isEmpty() ? "Create an instance. Make it yours."
                                    : QString("Minecraft %1  /  %2  /  %3 GB memory")
                                          .arg(j["version"].toString(), j["loader"].toString("Vanilla"),
                                               QString::number(j["memory"].toInt(4096) / 1024., 'g', 3)));
        if (j["playtimeSeconds"].toDouble() > 0)
            detail->setText(detail->text() + " · " + formatDuration(qint64(j["playtimeSeconds"].toDouble())) +
                            " played");
        if (!j["pendingDownloads"].toArray().isEmpty())
            detail->setText(
                detail->text() +
                QString(" · %1 manual download(s) needed").arg(j["pendingDownloads"].toArray().size()));
        play->setEnabled(!j.isEmpty() && !busy && (!game || game->state() == QProcess::NotRunning));
    });
    connect(instances, &QListWidget::itemActivated, this, [this] { launch(); });
    return w;
}
Json Window::selected() const {
    auto *i = instances->currentItem();
    return i ? i->data(Qt::UserRole).toJsonObject() : Json{};
}
void Window::saveInstance(const Json &j) {
    writeJson(safePath(root, "instances/" + j["id"].toString() + "/instance.json"), j);
}
void Window::reload() {
    auto id = selected()["id"].toString();
    if (id.isEmpty())
        id = settings.value("lastInstance").toString();
    auto group = groupFilter->currentText();
    instances->clear();
    QDir d(root + "/instances");
    QList<Json> all;
    QStringList groups;
    for (const auto &name : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        try {
            auto j = readJson(d.filePath(name + "/instance.json"));
            require(j["id"].toString() == name, "Instance directory ID mismatch");
            all << j;
            groups << j["group"].toString("Ungrouped");
        } catch (const std::exception &e) {
            log(e.what());
        }
    }
    groups.removeDuplicates();
    groups.sort(Qt::CaseInsensitive);
    {
        QSignalBlocker block(groupFilter);
        groupFilter->clear();
        groupFilter->addItem("All groups");
        groupFilter->addItems(groups);
        auto index = groupFilter->findText(group);
        groupFilter->setCurrentIndex(qMax(0, index));
    }
    const auto order = sortOrder->currentIndex();
    std::stable_sort(all.begin(), all.end(), [order](const Json &a, const Json &b) {
        if (order == 1 && a["lastPlayed"].toDouble() != b["lastPlayed"].toDouble())
            return a["lastPlayed"].toDouble() > b["lastPlayed"].toDouble();
        if (order == 2 && a["favorite"].toBool() != b["favorite"].toBool())
            return a["favorite"].toBool();
        return a["name"].toString().compare(b["name"].toString(), Qt::CaseInsensitive) < 0;
    });
    for (const auto &j : all) {
        auto *item = new QListWidgetItem(j["name"].toString() + "\nMinecraft " + j["version"].toString() +
                                         " · " + j["loader"].toString("Vanilla"));
        item->setData(Qt::UserRole, j);
        if (!j["cover"].toString().isEmpty())
            try {
                QPixmap cover(
                    safePath(root, "instances/" + j["id"].toString() + "/" + j["cover"].toString()));
                if (!cover.isNull())
                    item->setData(Qt::UserRole + 2, cover);
                else
                    log("Cannot decode artwork for " + j["name"].toString() +
                        ". Try refreshing its official artwork.");
            } catch (const std::exception &e) {
                log(e.what());
            }
        item->setToolTip(item->text() + "\n" + j["group"].toString("Ungrouped"));
        instances->addItem(item);
        if (j["id"].toString() == id)
            instances->setCurrentItem(item);
    }
    if (!instances->currentItem() && instances->count())
        instances->setCurrentRow(0);
    refreshSearchTargets();
    collectionCount->setText(QString("%1 instance%2").arg(all.size()).arg(all.size() == 1 ? "" : "s"));
    filterInstances();
}
void Window::filterInstances() {
    if (!instances || !groupFilter)
        return;
    for (int i = 0; i < instances->count(); ++i) {
        auto *item = instances->item(i);
        const auto j = item->data(Qt::UserRole).toJsonObject();
        const auto group = j["group"].toString("Ungrouped");
        item->setHidden(!item->text().contains(instanceSearch->text(), Qt::CaseInsensitive) ||
                        (groupFilter->currentIndex() > 0 && group != groupFilter->currentText()) ||
                        (favoritesOnly->isChecked() && !j["favorite"].toBool()));
    }
    if (!instances->currentItem() || instances->currentItem()->isHidden()) {
        QListWidgetItem *visible = nullptr;
        for (int i = 0; i < instances->count(); ++i)
            if (!instances->item(i)->isHidden()) {
                visible = instances->item(i);
                break;
            }
        instances->setCurrentItem(visible);
    }
}
void Window::openInstanceFolder(const QString &child) {
    auto j = selected();
    if (j.isEmpty())
        return;
    try {
        auto dir =
            safePath(root, "instances/" + j["id"].toString() + (child.isEmpty() ? QString{} : "/" + child));
        require(QDir().mkpath(dir), "Cannot create folder");
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "Cannot open folder", e.what());
    }
}
void Window::instanceMenu(const QPoint &position) {
    auto *item = instances->itemAt(position);
    if (!item)
        return;
    instances->setCurrentItem(item);
    auto j = selected();
    QMenu menu(this);
    auto add = [&](QString name, std::function<void()> action) {
        auto *a = menu.addAction(name);
        connect(a, &QAction::triggered, &menu, action);
        return a;
    };
    add("Launch", [this] {
        launch();
    })->setEnabled(!busy && (!game || game->state() == QProcess::NotRunning));
    add("Edit instance…  Ctrl+E", [this] { editInstance(); });
    if (!j["pendingDownloads"].toArray().isEmpty())
        add("Complete missing downloads…", [this, j] { missingDownloads(j); })->setEnabled(!busy);
    menu.addSeparator();
    auto save = [this](Json value) {
        try {
            saveInstance(value);
            reload();
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Cannot save", e.what());
        }
    };
    auto *favorite =
        add(j["favorite"].toBool() ? "Remove from favorites" : "Add to favorites", [=, this]() mutable {
            j["favorite"] = !j["favorite"].toBool();
            save(j);
        });
    favorite->setEnabled(!busy);
    add("Rename…", [=, this]() mutable {
        bool ok = false;
        auto name = QInputDialog::getText(this, "Rename instance", "Name", QLineEdit::Normal,
                                          j["name"].toString(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            j["name"] = name.trimmed();
            save(j);
        }
    })->setEnabled(!busy);
    add("Set group…", [=, this]() mutable {
        bool ok = false;
        auto name = QInputDialog::getText(this, "Instance group", "Group (empty = Ungrouped)",
                                          QLineEdit::Normal, j["group"].toString(), &ok);
        if (ok) {
            j["group"] = name.trimmed().isEmpty() ? "Ungrouped" : name.trimmed();
            save(j);
        }
    })->setEnabled(!busy);
    add("Choose tile color…", [=, this]() mutable {
        auto color = QColorDialog::getColor(QColor(j["color"].toString("#cba6f7")), this, "Instance color");
        if (color.isValid()) {
            j["color"] = color.name();
            save(j);
        }
    })->setEnabled(!busy);
    add("Use official Modrinth artwork…", [this, j] {
        bool ok = false;
        auto value =
            QInputDialog::getText(this, "Official pack artwork", "Modrinth modpack URL, slug or project ID",
                                  QLineEdit::Normal,
                                  j["provider"] == "modrinth" ? j["projectId"].toString() : QString{}, &ok)
                .trimmed();
        if (!ok || value.isEmpty())
            return;
        if (value.startsWith("https://modrinth.com/modpack/"))
            value = QUrl(value).path().section('/', 2, 2);
        if (!QRegularExpression("^[A-Za-z0-9_-]+$").match(value).hasMatch()) {
            QMessageBox::warning(this, "Invalid project",
                                 "Enter a Modrinth modpack URL, slug or project ID.");
            return;
        }
        task([j, value](
                 Engine &e) { return QVariant(e.officialArtwork(j, Json{{"project_id", value}}, false)); },
             [this](QVariant) { reload(); });
    })->setEnabled(!busy);
    add("Choose cover artwork…", [this, j, save]() mutable {
        auto path =
            QFileDialog::getOpenFileName(this, "Instance cover", {}, "Images (*.png *.jpg *.jpeg *.webp)");
        if (path.isEmpty())
            return;
        try {
            QImageReader reader(path);
            auto size = reader.size();
            require(size.isValid() && qint64(size.width()) * size.height() <= 64000000,
                    "Choose an image smaller than 64 megapixels");
            reader.setAutoTransform(true);
            reader.setScaledSize(size.scaled(2160, 1440, Qt::KeepAspectRatio));
            auto image = reader.read();
            require(!image.isNull(), "Cannot decode cover image");
            auto target = safePath(root, "instances/" + j["id"].toString() + "/cover.png");
            QSaveFile out(target);
            require(out.open(QIODevice::WriteOnly) && image.save(&out, "PNG") && out.commit(),
                    "Cannot save cover");
            j["cover"] = "cover.png";
            save(j);
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Cannot set cover", e.what());
        }
    })->setEnabled(!busy);
    if (j.contains("cover"))
        add("Use default artwork", [j, save]() mutable {
            j.remove("cover");
            save(j);
        })->setEnabled(!busy);
    auto *folders = menu.addMenu("Open folder");
    for (const auto &name : QStringList{"Instance", "mods", "resourcepacks", "shaderpacks", "saves",
                                        "screenshots", "logs", "crash-reports"}) {
        auto *a = folders->addAction(name);
        connect(a, &QAction::triggered, &menu,
                [this, name] { openInstanceFolder(name == "Instance" ? QString{} : name); });
    }
    menu.addSeparator();
    add("Back up instance…", [this, j] {
        if (busy || (game && game->state() != QProcess::NotRunning))
            return;
        auto path = QFileDialog::getSaveFileName(this, "Back up instance", j["name"].toString() + ".neko.zip",
                                                 "Neko backup (*.neko.zip)");
        if (path.isEmpty())
            return;
        auto source = root + "/instances/" + j["id"].toString();
        task([source, path](Engine &e) {
            backupInstance(source, path, [&e] { e.http.checkCancelled(); });
            return QVariant{};
        });
    });
    for (auto *action : instanceActions->actions())
        menu.addAction(action);
    menu.exec(instances->viewport()->mapToGlobal(position));
}
