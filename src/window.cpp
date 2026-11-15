#include "window.hpp"
#include "cards.hpp"
#include "ui/widgets.hpp"
using namespace neko;
using namespace neko::ui;

Window::Window() {
    if (!settings.value("designV4", false).toBool()) {
        settings.setValue("theme", "Shadcn Dark");
        settings.setValue("designV4", true);
    }
    artworkNetwork = new QNetworkAccessManager(this);
    root = settings.value("dataRoot", QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
               .toString();
    QDir().mkpath(root + "/instances");
    setWindowTitle("Neko Launcher");
    resize(1320, 880);
    setMinimumSize(700, 480);
    auto *shell = new QWidget;
    auto *row = new QHBoxLayout(shell);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    setCentralWidget(shell);
    sidebar = new QWidget;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(226);
    auto *nav = new QVBoxLayout(sidebar);
    nav->setContentsMargins(16, 20, 16, 20);
    nav->setSpacing(6);
    auto *mark = new QLabel;
    mark->setObjectName("brandMark");
    mark->setAlignment(Qt::AlignCenter);
    mark->setFixedHeight(48);
    auto *brand = new QWidget;
    brand->setObjectName("sidebarBrand");
    auto *brandRow = new QHBoxLayout(brand);
    brandRow->setContentsMargins(6, 0, 6, 14);
    brandRow->setSpacing(10);
    mark->setFixedWidth(40);
    brandRow->addWidget(mark);
    auto *word = label("neko", "sidebarWordmark");
    brandRow->addWidget(word, 1);
    nav->addWidget(brand);
    auto *workspaceLabel = label("WORKSPACE", "navSection");
    nav->addWidget(workspaceLabel);
    const QStringList names{"Library", "Discover", "Mods", "Account", "Settings", "Console"};
    const QList<Glyph> symbols{Glyph::Home,    Glyph::Explore,  Glyph::Mods,
                               Glyph::Account, Glyph::Settings, Glyph::Console};
    auto *group = new QButtonGroup(this);
    navigation = group;
    group->setExclusive(true);
    for (int i = 0; i < names.size(); ++i) {
        if (i == 3) {
            nav->addStretch();
            nav->addWidget(label("LAUNCHER", "navSection"));
        }
        auto *b = new NavigationButton(names[i], symbols[i]);
        nav->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, i] { showPage(i); });
        b->setProperty("fullText", names[i]);
        b->setToolTip(names[i]);
        b->setCheckable(true);
        b->setObjectName("nav");
        group->addButton(b, i);
        if (i == 0)
            b->setChecked(true);
    }
    nav->addSpacing(10);
    nav->addWidget(label("NEKO  ·  " + QCoreApplication::applicationVersion(), "navVersion"));
    connect(group, &QButtonGroup::idClicked, this, [group](int id) { group->button(id)->setChecked(true); });
    identity = label("Offline profile", "muted");
    identity->hide();
    row->addWidget(sidebar);
    auto *main = new BackdropWidget;
    main->setObjectName("workspace");
    auto *mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    auto *top = new QWidget;
    top->setObjectName("topbar");
    auto *topRow = new QHBoxLayout(top);
    topRow->setContentsMargins(30, 14, 30, 14);
    pageTitle = label("Library", "wordmark");
    topRow->addWidget(pageTitle, 1);
    auto *javaBadge = label("Minecraft · Java Edition", "muted");
    javaBadge->setObjectName("editionBadge");
    topRow->addWidget(javaBadge);
    topRow->addSpacing(12);
    auto *commands = button("Quick switcher   Ctrl K", topRow, [this] { commandPalette(); });
    commands->setObjectName("commandButton");
    commands->setToolTip("Find instances and actions · Ctrl+K");
    auto *paletteShortcut = new QShortcut(QKeySequence("Ctrl+K"), this);
    connect(paletteShortcut, &QShortcut::activated, this, &Window::commandPalette);
    accountButton = button("Local profile", topRow, [this] { showPage(3); });
    accountButton->setObjectName("accountButton");
    accountButton->setIcon(glyph(Glyph::Account, QColor("#a6adc8")));
    mainLayout->addWidget(top);
    pages = new QStackedWidget;
    pages->setObjectName("pages");
    auto *pageOpacity = new QGraphicsOpacityEffect(pages);
    pageOpacity->setOpacity(1);
    pages->setGraphicsEffect(pageOpacity);
    pageTransition = new QPropertyAnimation(pageOpacity, "opacity", this);
    pageTransition->setDuration(170);
    pageTransition->setEasingCurve(QEasingCurve::OutCubic);
    auto addPage = [this](QWidget *content) {
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->viewport()->setAutoFillBackground(false);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        content->setMinimumWidth(0);
        scroll->setWidget(content);
        pages->addWidget(scroll);
    };
    addPage(libraryPage());
    addPage(discoverPage());
    addPage(modsPage());
    addPage(accountsPage());
    addPage(settingsPage());
    QVBoxLayout *logs;
    auto *logPage =
        page(logs, "Under the hood", "Download activity, launch diagnostics and live game output.");
    console = new QPlainTextEdit;
    console->setReadOnly(true);
    console->setMaximumBlockCount(6000);
    logs->addWidget(console, 1);
    auto *logButtons = new QHBoxLayout;
    logs->addLayout(logButtons);
    button("Clear", logButtons, [this] { console->clear(); });
    button("Save log…", logButtons, [this] {
        auto path = QFileDialog::getSaveFileName(this, "Save redacted log", {}, "Text (*.txt)");
        if (path.isEmpty())
            return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(console->toPlainText().toUtf8());
    });
    console->setMinimumHeight(220);
    addPage(logPage);
    mainLayout->addWidget(pages, 1);
    auto *footer = new QWidget;
    footer->setObjectName("footer");
    auto *foot = new QHBoxLayout(footer);
    foot->setContentsMargins(28, 12, 28, 12);
    status = new ElidedLabel("Ready to explore");
    status->setObjectName("muted");
    status->setWordWrap(false);
    status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    foot->addWidget(status, 1);
    progress = new QProgressBar;
    progress->setFixedWidth(130);
    progress->setRange(0, 0);
    progress->hide();
    foot->addWidget(progress);
    cancel = button("Cancel", foot, [this] {
        if (cancellation)
            cancellation->store(true);
    });
    cancel->hide();
    auto *logsLink = button("View logs", foot, [this] { showPage(5); });
    logsLink->setObjectName("logsLink");
    logsLink->setProperty("glyph", int(Glyph::Console));
    mainLayout->addWidget(footer);
    row->addWidget(main, 1);
    applyTheme();
    reload();
    log("Ready to play. Your worlds are waiting.");
    if (!QCoreApplication::arguments().contains("--ui-test") &&
        !QCoreApplication::arguments().contains("--smoke-test"))
        QTimer::singleShot(0, this, &Window::restoreAccount);
}
Window::~Window() {
    if (searchCancellation)
        searchCancellation->store(true);
    if (game && game->state() != QProcess::NotRunning) {
        stopGame();
        if (!game->waitForFinished(3000)) {
            game->kill();
            game->waitForFinished(1000);
        }
    }
}
void Window::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (sidebar)
        sidebar->setFixedWidth(width() >= 1120 ? 226 : 80);
    if (auto *word = sidebar->findChild<QLabel *>("sidebarWordmark"))
        word->setVisible(width() >= 1120);
    for (auto *caption : sidebar->findChildren<QLabel *>())
        if (caption->objectName() == "navSection" || caption->objectName() == "navVersion")
            caption->setVisible(width() >= 1120);
    if (auto *badge = findChild<QLabel *>("editionBadge"))
        badge->setVisible(width() >= 800);
    if (auto *commands = findChild<QPushButton *>("commandButton"))
        commands->setText(width() < 950 ? "Ctrl K" : "Quick switcher   Ctrl K");
    if (librarySplit)
        librarySplit->setDirection(QBoxLayout::TopToBottom);
    if (heroLayout)
        heroLayout->setDirection(QBoxLayout::TopToBottom);
    if (heroActionsLayout)
        heroActionsLayout->setDirection(QBoxLayout::LeftToRight);
    if (heroCard)
        heroCard->setMinimumHeight(width() < 850 ? 214 : 246);
    if (libraryHeader)
        libraryHeader->setDirection(width() < 950 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    if (profileActions)
        profileActions->setDirection(width() < 850 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    if (collectionGrid && groupFilter && sortOrder && viewMode && favoritesOnly) {
        const int columns = width() < 950 ? 2 : 4;
        if (collectionGrid->property("columns").toInt() != columns) {
            while (auto *item = collectionGrid->takeAt(0))
                delete item;
            const QList<QWidget *> fields{groupFilter, favoritesOnly, sortOrder, viewMode};
            for (int i = 0; i < fields.size(); ++i)
                collectionGrid->addWidget(fields[i], i / columns, i % columns);
            collectionGrid->setProperty("columns", columns);
        }
    }
    if (instances && instances->viewMode() == QListView::IconMode)
        instances->setGridSize(QSize(240, qMax(238, QFontMetrics(instances->font()).height() * 3 + 164)));
    if (libraryToolbar)
        libraryToolbar->setDirection(width() < 850 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    if (discoverFilters) {
        const bool compact = width() < 1000;
        const int columns = compact ? 2 : 1;
        discoverFilterPanel->setVisible(!compact || searchFiltersExpanded);
        searchFiltersToggle->setVisible(compact);
        discoverBody->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        discoverFilterPanel->setMinimumWidth(compact ? 0 : 185);
        discoverFilterPanel->setMaximumWidth(compact ? QWIDGETSIZE_MAX : 185);
        QList<QLayoutItem *> items;
        while (auto *item = discoverFilters->takeAt(0))
            items << item;
        for (int i = 0; i < items.size(); ++i) {
            const int field = i / 2;
            discoverFilters->addItem(items[i], (field / columns) * 2 + i % 2, field % columns);
        }
    }
    if (discoverToolbar)
        discoverToolbar->setDirection(QBoxLayout::LeftToRight);
}
void Window::closeEvent(QCloseEvent *e) {
    if (busy) {
        QMessageBox::information(this, "Operation in progress",
                                 "Wait for the current download or sign-in to finish before closing.");
        e->ignore();
        return;
    }
    if (game && game->state() != QProcess::NotRunning &&
        QMessageBox::question(this, "Minecraft is running",
                              "Closing Neko will stop the game. Close anyway?") != QMessageBox::Yes) {
        e->ignore();
        return;
    }
    e->accept();
}
void Window::showPage(int index) {
    const bool changed = pages->currentIndex() != index;
    pages->setCurrentIndex(index);
    if (pageTransition) {
        pageTransition->stop();
        auto *opacity = qobject_cast<QGraphicsOpacityEffect *>(pages->graphicsEffect());
        if (changed && !settings.value("reduceMotion", false).toBool() &&
            !QCoreApplication::arguments().contains("--ui-test") &&
            !QCoreApplication::arguments().contains("--smoke-test")) {
            pageTransition->setStartValue(.65);
            pageTransition->setEndValue(1.);
            pageTransition->start();
        } else if (opacity)
            opacity->setOpacity(1);
    }
    pageTitle->setText(
        QStringList{"Library", "Discover", "Mods", "Account", "Settings", "Console"}.value(index));
    if (navigation && navigation->button(index))
        navigation->button(index)->setChecked(true);
    if (index == 1 && !searchStarted && !QCoreApplication::arguments().contains("--ui-test") &&
        !QCoreApplication::arguments().contains("--smoke-test"))
        searchPacks();
    if (index == 2)
        refreshMods();
}
