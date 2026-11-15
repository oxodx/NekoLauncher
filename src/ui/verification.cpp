#include "cards.hpp"
#include "instance_services.hpp"
#include "ui/command_palette.hpp"
#include "ui/instance_tools.hpp"
#include "ui/mod_browser.hpp"
#include "ui/storage.hpp"
#include "ui/widgets.hpp"
#include "window.hpp"
using namespace neko;
using namespace neko::ui;

int Window::verifyUi(const QString &output) {
    QDir().mkpath(output);
    if (QFile::exists(root + "/cache/ui-search.json")) {
        const auto fixture = readJson(root + "/cache/ui-search.json");
        query->setText(fixture["query"].toString());
        showSearchResults(fixture["hits"].toArray(), false);
    }
    int failures = 0;
    {
        QListWidget fixture;
        fixture.setViewMode(QListView::IconMode);
        fixture.addItem("Play target");
        int activations = 0;
        InstanceDelegate delegate(&fixture, [&](QModelIndex index) {
            if (index.row() == 0)
                ++activations;
        });
        QStyleOptionViewItem option;
        option.rect = QRect(0, 0, 232, 230);
        QMouseEvent click(QEvent::MouseButtonRelease, QPointF(200, 200), QPointF(200, 200), Qt::LeftButton,
                          Qt::NoButton, Qt::NoModifier);
        if (!delegate.editorEvent(&click, fixture.model(), option, fixture.model()->index(0, 0)) ||
            activations != 1)
            ++failures;
        QMouseEvent elsewhere(QEvent::MouseButtonRelease, QPointF(20, 20), QPointF(20, 20), Qt::LeftButton,
                              Qt::NoButton, Qt::NoModifier);
        if (delegate.editorEvent(&elsewhere, fixture.model(), option, fixture.model()->index(0, 0)) ||
            activations != 1)
            ++failures;
    }
    if (!QImageReader::supportedImageFormats().contains("webp")) {
        qWarning() << "WebP artwork decoder missing";
        ++failures;
    }
    {
        const auto fixture =
            QFile::exists(root + "/cache/ui-search.json") ? readJson(root + "/cache/ui-search.json") : Json{};
        const auto records = fixture["hits"].toArray();
        if (!records.isEmpty()) {
            const auto generation = searchGeneration;
            searchCancellation = std::make_shared<std::atomic_bool>(false);
            query->setText("a different search");
            if (searchGeneration <= generation || !searchCancellation->load() || results->count() ||
                searchInstall->isEnabled() || searchNext->isEnabled())
                ++failures;
            query->clear();
            showSearchResults({}, false);
            if (searchInstall->isEnabled() || !searchSummary->text().contains("No results"))
                ++failures;
            showSearchResults(records, false);
            if (!searchInstall->isEnabled())
                ++failures;
        }
    }
    {
        // Exercise toggling with private fixture files, never installed game mods.
        QTemporaryDir directory;
        const auto enabled = directory.filePath("example.jar");
        QFile file(enabled);
        if (!file.open(QIODevice::WriteOnly) || file.write("fixture") != 7)
            ++failures;
        file.close();
        mods->clear();
        auto *item = new QListWidgetItem("example.jar", mods);
        item->setData(Qt::UserRole, enabled);
        modSearch->setText("does-not-match");
        if (!item->isHidden())
            ++failures;
        modSearch->clear();
        item->setSelected(true);
        toggleMods(false);
        if (!QFile::exists(enabled + ".disabled") || QFile::exists(enabled))
            ++failures;
        mods->clear();
        item = new QListWidgetItem("example.jar.disabled", mods);
        item->setData(Qt::UserRole, enabled + ".disabled");
        modState->setCurrentIndex(1);
        if (!item->isHidden())
            ++failures;
        modState->setCurrentIndex(2);
        if (item->isHidden())
            ++failures;
        item->setSelected(true);
        toggleMods(true);
        if (!QFile::exists(enabled) || QFile::exists(enabled + ".disabled"))
            ++failures;
        modState->setCurrentIndex(0);
        refreshMods();
    }
    resize(700, 560);
    showPage(1);
    QApplication::processEvents();
    searchFiltersToggle->click();
    QApplication::processEvents();
    if (!discoverFilterPanel->isVisible())
        ++failures;
    if (!grab().save(output + "/search-filters-expanded.png"))
        ++failures;
    searchFiltersToggle->click();
    if (discoverFilterPanel->isVisible())
        ++failures;
    auto visibleCount = [this] {
        int count = 0;
        for (int i = 0; i < instances->count(); ++i)
            if (!instances->item(i)->isHidden())
                ++count;
        return count;
    };
    const auto total = instances->count();
    instanceSearch->setText("__neko_impossible_filter__");
    if (visibleCount() != 0)
        ++failures;
    instanceSearch->clear();
    int favoriteCount = 0;
    for (int i = 0; i < total; ++i)
        if (instances->item(i)->data(Qt::UserRole).toJsonObject()["favorite"].toBool())
            ++favoriteCount;
    favoritesOnly->setChecked(true);
    if (visibleCount() != favoriteCount)
        ++failures;
    favoritesOnly->setChecked(false);
    if (groupFilter->count() > 1) {
        groupFilter->setCurrentIndex(1);
        for (int i = 0; i < total; ++i) {
            auto *item = instances->item(i);
            if (!item->isHidden() && item->data(Qt::UserRole).toJsonObject()["group"].toString("Ungrouped") !=
                                         groupFilter->currentText())
                ++failures;
        }
        groupFilter->setCurrentIndex(0);
    }

    for (const auto &flavor : QStringList{"Shadcn Dark", "Shadcn Light", "Catppuccin Mocha",
                                          "Catppuccin Macchiato", "Catppuccin Frappé", "Catppuccin Latte"}) {
        theme->setCurrentText(flavor);
        const auto flavorOutput = output + "/" + flavor.section(' ', 1);
        QDir().mkpath(flavorOutput);
        for (int textSize : {14, 18}) {
            fontSize->setCurrentText(QString::number(textSize));
            for (const QSize size : {QSize(700, 480), QSize(760, 560), QSize(980, 640), QSize(1160, 780)}) {
                resize(size);
                for (int i = 0; i < pages->count(); ++i) {
                    showPage(i);
                    QApplication::processEvents();
                    QApplication::processEvents();
                    const auto name = QString("%1/page-%2-%3x%4-font%5.png")
                                          .arg(flavorOutput)
                                          .arg(i)
                                          .arg(size.width())
                                          .arg(size.height())
                                          .arg(textSize);
                    if (!grab().save(name))
                        ++failures;
                    for (auto *text : pages->currentWidget()->findChildren<QLabel *>()) {
                        if (!text->isVisible() || !text->wordWrap() || text->text().isEmpty())
                            continue;
                        if (text->heightForWidth(text->width()) > text->height() + 2) {
                            qWarning() << "Clipped label" << size << textSize << i << text->text()
                                       << text->size() << text->heightForWidth(text->width());
                            ++failures;
                        }
                    }
                }
            }
        }
    }
    if (instances->currentItem()) {
        fontSize->setCurrentText("18");
        QTimer::singleShot(0, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "instanceEditor") {
                ++failures;
                return;
            }
            auto *tabs = dialog->findChild<QTabWidget *>();
            dialog->resize(640, 500);
            for (int i = 0; i < tabs->count(); ++i) {
                tabs->setCurrentIndex(i);
                QApplication::processEvents();
                dialog->grab().save(output + QString("/editor-tab-%1.png").arg(i));
                for (auto *text : tabs->currentWidget()->findChildren<QLabel *>())
                    if (text->isVisible() && text->wordWrap() &&
                        text->heightForWidth(text->width()) > text->height() + 2) {
                        qWarning() << "Editor clipped" << i << text->text();
                        ++failures;
                    }
            }
            dialog->reject();
        });
        editInstance();
    }
    QTimer::singleShot(0, this, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "missingDownloads") {
            ++failures;
            return;
        }
        dialog->resize(640, 480);
        QApplication::processEvents();
        dialog->grab().save(output + "/manual-downloads.png");
        for (auto *text : dialog->findChildren<QLabel *>())
            if (text->isVisible() && text->wordWrap() &&
                text->heightForWidth(text->width()) > text->height() + 2)
                ++failures;
        dialog->reject();
    });
    missingDownloads(
        Json{{"id", "ui-test"},
             {"pendingDownloads", QJsonArray{Json{{"fileName", "moreoverlays-required-version.jar"},
                                                  {"pageUrl", "https://www.curseforge.com/"}}}}});
    // Test real process callbacks with an isolated data root and a portable, short-lived child.
    {
        QTemporaryDir processData;
        const auto originalRoot = root;
        const auto originalId = selected()["id"].toString();
        auto restoreState = qScopeGuard([&] {
            if (game) {
                if (game->state() != QProcess::NotRunning) {
                    game->kill();
                    game->waitForFinished();
                }
                delete game;
                game = nullptr;
            }
            sessionActive = false;
            root = originalRoot;
            settings.setValue("lastInstance", originalId);
            reload();
            for (int i = 0; i < instances->count(); ++i)
                if (instances->item(i)->data(Qt::UserRole).toJsonObject()["id"] == originalId)
                    instances->setCurrentRow(i);
        });
        root = processData.path();
        Json fixture{{"id", "process-test"}, {"name", "Process lifecycle fixture"}, {"version", "test"}};
        writeJson(root + "/instances/process-test/instance.json", fixture);
        auto runProbe = [&](const QString &program, const QStringList &arguments) {
            startGame({program, root, arguments, QProcessEnvironment::systemEnvironment()}, fixture);
            QEventLoop loop;
            QTimer timeout;
            timeout.setSingleShot(true);
            connect(&timeout, &QTimer::timeout, &loop, [&] {
                ++failures;
                game->kill();
                loop.quit();
            });
            connect(game, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &loop,
                    &QEventLoop::quit);
            connect(game, &QProcess::errorOccurred, &loop, [&](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart)
                    loop.quit();
            });
            if (game->state() != QProcess::NotRunning) {
                timeout.start(5000);
                loop.exec();
            }
            if (game->state() != QProcess::NotRunning) {
                game->kill();
                game->waitForFinished();
            }
            QApplication::processEvents();
        };
        runProbe(QCoreApplication::applicationFilePath(), {"--process-test-exit", "0"});
        if (sessionHistory(root, "process-test").size() != 1)
            ++failures;
        runProbe(root + "/missing-program", {});
        if (sessionHistory(root, "process-test").size() != 1)
            ++failures;
        runProbe(QCoreApplication::applicationFilePath(), {"--process-test-exit", "7"});
        const auto history = sessionHistory(root, "process-test");
        if (history.size() != 2 || history.first().toObject()["exitCode"] != 7)
            ++failures;
    }
    // Exercise keyboard filtering and action selection without starting Minecraft.
    {
        int chosen = 0;
        CommandPalette palette({{"Settings", "Appearance", "theme", [&] { chosen = 1; }},
                                {"World snapshots", "Recovery", "saves", [&] { chosen = 2; }}},
                               this);
        QTimer::singleShot(0, &palette, [&] {
            auto *field = palette.findChild<QLineEdit *>("paletteSearch");
            if (!field) {
                ++failures;
                palette.reject();
                return;
            }
            field->setText("snap");
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(field, &enter);
        });
        if (palette.exec() != QDialog::Accepted || !palette.selectedAction())
            ++failures;
        else
            palette.selectedAction()();
        if (chosen != 2)
            ++failures;
    }
    theme->setCurrentText("Shadcn Dark");
    fontSize->setCurrentText("14");
    auto captureFeature = [&](const QString &name, const QString &file) {
        QTimer::singleShot(0, this, [&, name, file] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != name) {
                ++failures;
                return;
            }
            dialog->resize(640, 500);
            QApplication::processEvents();
            if (!dialog->grab().save(output + "/" + file))
                ++failures;
            for (auto *text : dialog->findChildren<QLabel *>())
                if (text->isVisible() && text->wordWrap() &&
                    text->heightForWidth(text->width()) > text->height() + 2)
                    ++failures;
            dialog->reject();
        });
    };
    if (!selected().isEmpty()) {
        captureFeature("worldSnapshots", "world-snapshots.png");
        worldSnapshots();
        captureFeature("sessionHistory", "session-history.png");
        showHistory();
    }
    captureFeature("instanceHealth", "instance-health.png");
    showHealthReport(
        this, selected(),
        {{CheckState::Pass, "Compatible Java", "A compatible runtime was found."},
         {CheckState::Warning, "World safety", "Enable automatic snapshots to protect local saves."}});
    captureFeature("commandPalette", "command-palette.png");
    commandPalette();
    captureFeature("storageReport", "mod-storage.png");
    showStorageReport(this, Json{{"files", 300},
                                 {"sharedFiles", 240},
                                 {"objects", 120},
                                 {"savedBytes", double(1024 * 1024 * 300)},
                                 {"storeBytes", double(1024 * 1024 * 350)},
                                 {"unusedBytes", double(1024 * 1024 * 20)}});
    captureFeature("modInstallPlan", "mod-install-plan.png");
    confirmModPlan(this, Json{{"name", "Mod browser fixture"}, {"version", "1.21.1"}, {"loader", "Fabric"}},
                   Json{{"files", QJsonArray{Json{{"title", "Mod Menu"}, {"fileName", "modmenu-11.jar"}},
                                             Json{{"title", "Required library"},
                                                  {"fileName", "library.jar"},
                                                  {"manual", true}}}}});
    showPage(1);
    searchType->setCurrentIndex(1);
    for (int i = 0; i < searchTarget->count(); ++i)
        if (searchTarget->itemData(i).toJsonObject()["loader"].toString().compare("fabric",
                                                                                  Qt::CaseInsensitive) == 0) {
            searchTarget->setCurrentIndex(i);
            break;
        }
    if (!searchTarget->isVisible() || searchVersion->isEnabled() || searchLoader->isEnabled() ||
        searchInstall->text() != "Install mod")
        ++failures;
    if (QFile::exists(root + "/cache/ui-mod-search.json")) {
        auto fixture = readJson(root + "/cache/ui-mod-search.json");
        query->setText(fixture["query"].toString());
        showSearchResults(fixture["hits"].toArray(), false);
    }
    theme->setCurrentText("Shadcn Dark");
    fontSize->setCurrentText("14");
    for (const QSize size : {QSize(700, 480), QSize(1160, 780)}) {
        resize(size);
        showPage(1);
        QApplication::processEvents();
        if (!grab().save(output + QString("/mod-browser-%1.png").arg(size.width())))
            ++failures;
    }
    const QStringList contentActions{"Install resource pack", "Install shader"};
    const QStringList contentPlaceholders{"Search resource packs…", "Search shaders…"};
    for (int mode = 2; mode <= 3; ++mode) {
        searchType->setCurrentIndex(mode);
        QApplication::processEvents();
        if (!searchTarget->isVisible() || searchVersion->isEnabled() || searchLoader->isEnabled() ||
            searchInstall->text() != contentActions[mode - 2] ||
            query->placeholderText() != contentPlaceholders[mode - 2] || searchCategory->count() < 2)
            ++failures;
        if (!grab().save(output + QString(mode == 2 ? "/resourcepack-browser.png"
                                                     : "/shader-browser.png")))
            ++failures;
    }
    searchType->setCurrentIndex(0);
    if (searchTarget->isVisible() || !searchVersion->isEnabled())
        ++failures;
    log("Ready to play. Your worlds are waiting.");
    if (QCoreApplication::arguments().contains("--network") && results->count()) {
        int icons = 0;
        for (int i = 0; i < results->count(); ++i)
            if (!results->item(i)->data(Qt::UserRole + 2).value<QPixmap>().isNull())
                ++icons;
        if (!icons) {
            qWarning() << "No official project thumbnails loaded";
            ++failures;
        } else
            qInfo() << "Official project thumbnails loaded:" << icons;
    }
    theme->setCurrentText("Shadcn Dark");
    fontSize->setCurrentText("14");
    resize(1320, 880);
    showPage(0);
    QApplication::processEvents();
    grab().save("neko-preview.png");
    return failures;
}
