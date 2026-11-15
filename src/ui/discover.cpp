#include "cards.hpp"
#include "ui/widgets.hpp"
#include "window.hpp"
using namespace neko;
using namespace neko::ui;

QWidget *Window::discoverPage() {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(24, 20, 24, 20);
    l->setSizeConstraint(QLayout::SetMinimumSize);
    l->addWidget(label("Discover", "title"));
    searchType = new QTabBar;
    searchType->setObjectName("contentTabs");
    searchType->addTab("Modpacks");
    searchType->addTab("Mods");
    searchType->addTab("Resource packs");
    searchType->addTab("Shaders");
    searchType->setExpanding(false);
    searchType->setDrawBase(false);
    searchType->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    l->addWidget(searchType);
    searchTarget = new QComboBox;
    searchTarget->setObjectName("modTarget");
    searchTarget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    searchTarget->hide();
    l->addWidget(searchTarget);
    auto *bar = new QBoxLayout(QBoxLayout::LeftToRight);
    discoverToolbar = bar;
    l->addLayout(bar);
    provider = new QComboBox;
    provider->addItems({"Modrinth", "CurseForge"});
    bar->addWidget(provider);
    query = entry({}, "Search modpacks…");
    query->setProperty("searchField", true);
    bar->addWidget(query, 1);
    query->setMinimumWidth(0);
    query->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    query->setClearButtonEnabled(true);
    query->setObjectName("packSearch");
    searchTimer = new QTimer(this);
    searchTimer->setSingleShot(true);
    searchTimer->setInterval(450);
    connect(searchTimer, &QTimer::timeout, this, [this] { searchPacks(); });
    button("Search", bar, [this] { searchPacks(); })->setObjectName("primary");
    connect(query, &QLineEdit::returnPressed, this, [this] { searchPacks(); });
    connect(query, &QLineEdit::textChanged, this, [this] { scheduleSearch(); });
    l->setSpacing(12);
    auto *body = discoverBody = new QBoxLayout(QBoxLayout::LeftToRight);
    body->setSpacing(18);
    discoverFilterPanel = new QWidget;
    auto *filterLayout = new QVBoxLayout(discoverFilterPanel);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    auto *filters = discoverFilters = new QGridLayout;
    filterLayout->addLayout(filters);
    filterLayout->addStretch();
    filters->setHorizontalSpacing(12);

    searchVersion = new QComboBox;
    searchVersion->setEditable(true);
    searchVersion->setMaxVisibleItems(12);
    searchVersion->setInsertPolicy(QComboBox::NoInsert);
    searchVersion->addItem("Any version", "");
    searchVersion->lineEdit()->setPlaceholderText("Minecraft version…");
    searchVersion->setToolTip("Type an exact Minecraft version, including older releases or snapshots.");
    searchLoader = new QComboBox;
    searchLoader->addItem("Any loader", "");
    for (const auto &loader : {"Fabric", "Quilt", "Forge", "NeoForge"})
        searchLoader->addItem(loader, QString(loader).toLower());
    searchCategory = new QComboBox;
    searchCategory->addItem("All categories", "");
    for (const auto &category : {"Adventure", "Challenging", "Combat", "Kitchen Sink", "Lightweight", "Magic",
                                 "Multiplayer", "Optimization", "Quests", "Technology"})
        searchCategory->addItem(category, QString(category).toLower().replace(' ', '-'));
    searchSort = new QComboBox;
    searchSort->addItem("Relevance", "relevance");
    searchSort->addItem("Most downloads", "downloads");
    searchSort->addItem("Recently updated", "updated");
    searchSort->addItem("Newest", "newest");
    const QList<QPair<QString, QComboBox *>> fields{{"Minecraft", searchVersion},
                                                    {"Loader", searchLoader},
                                                    {"Category", searchCategory},
                                                    {"Sort by", searchSort}};
    for (int i = 0; i < fields.size(); ++i) {
        filters->addWidget(label(fields[i].first, "micro"), (i / 2) * 2, i % 2);
        filters->addWidget(fields[i].second, (i / 2) * 2 + 1, i % 2);
        fields[i].second->setMinimumWidth(0);
        fields[i].second->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(fields[i].second, &QComboBox::currentTextChanged, this, [this] { scheduleSearch(); });
    }
    auto *summary = new QHBoxLayout;
    l->addLayout(summary);
    searchSummary = label("Browse modpacks, or search for something you love.", "muted");
    summary->addWidget(searchSummary, 1);
    searchFiltersToggle = button("Filters", summary, [this] {
        searchFiltersExpanded = !searchFiltersExpanded;
        discoverFilterPanel->setVisible(searchFiltersExpanded);
        searchFiltersToggle->setText(searchFiltersExpanded ? "Hide filters" : "Filters");
    });
    searchFiltersToggle->hide();
    button("Reset filters", summary, [this] {
        for (auto *field : {searchVersion, searchLoader, searchCategory, searchSort}) {
            QSignalBlocker block(field);
            field->setCurrentIndex(0);
            if (field->isEditable())
                field->setEditText(field->itemText(0));
        }
        query->clear();
        if (searchType->currentIndex() >= 1)
            updateSearchMode();
        searchPacks();
    });
    connect(provider, &QComboBox::currentIndexChanged, this, [this](int index) {
        searchSort->setItemText(0, index ? "Featured" : "Relevance");
        searchLoader->setEnabled(index == 0 && searchType->currentIndex() == 0);
        searchCategory->setEnabled(index == 0);
        searchLoader->setToolTip(index ? "Loader filtering is available on Modrinth." : "");
        searchCategory->setToolTip(index ? "Category filtering is available on Modrinth." : "");
        searchPacks();
    });
    results = new QListWidget;
    results->setItemDelegate(new DiscoverDelegate(results));
    results->setMouseTracking(true);
    results->setWordWrap(false);
    results->setTextElideMode(Qt::ElideRight);
    results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    results->setMinimumHeight(180);
    results->setSpacing(8);
    body->addWidget(discoverFilterPanel);
    body->addWidget(results, 1);
    l->addLayout(body, 1);
    auto *pagination = new QHBoxLayout;
    l->addLayout(pagination);
    searchPrevious = button("← Previous", pagination, [this] {
        searchOffset = qMax(0, searchOffset - 20);
        searchPacks(false);
    });
    searchSelection = label("Select a pack to see its details.", "muted");
    searchSelection->hide();
    pagination->addStretch();
    searchNext = button("Next →", pagination, [this] {
        searchOffset += 20;
        searchPacks(false);
    });
    searchPrevious->setEnabled(false);
    searchNext->setEnabled(false);
    auto *actions = pagination;
    searchInstall = button("Install pack", actions, [this] {
        auto *item = results->currentItem();
        if (!item)
            return;
        if (searchType->currentIndex() == 1) {
            installSelectedMod();
            return;
        }
        if (searchType->currentIndex() >= 2) {
            installSelectedContent();
            return;
        }
        auto j = item->data(Qt::UserRole).toJsonObject();
        auto cf = item->data(Qt::UserRole + 1).toBool();
        auto key = settings.value("curseKey").toString();
        task(
            [=](Engine &e) {
                auto file = e.downloadPack(j, cf, key);
                auto guard = qScopeGuard([&] { QFile::remove(file); });
                auto instance = e.importPack(file, key);
                try {
                    instance = e.officialArtwork(instance, j, cf);
                } catch (const std::exception &error) {
                    if (e.progress)
                        e.progress(QString("Pack installed; artwork unavailable: ") + error.what());
                }
                return QVariant(instance);
            },
            [this](QVariant result) {
                reload();
                showPage(0);
                missingDownloads(result.toJsonObject());
            });
    });
    searchInstall->setObjectName("primary");
    searchInstall->setEnabled(false);
    connect(results, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        searchInstall->setEnabled(item != nullptr);
        if (!item) {
            searchSelection->setText("Select a pack to see its details.");
            return;
        }
        const auto project = item->data(Qt::UserRole).toJsonObject();
        const bool cf = item->data(Qt::UserRole + 1).toBool();
        searchSelection->setText((cf ? project["name"] : project["title"]).toString());
        searchSelection->setToolTip((cf ? project["summary"] : project["description"]).toString());
    });
    button("Project ↗", actions, [this] {
        auto *i = results->currentItem();
        if (!i)
            return;
        auto j = i->data(Qt::UserRole).toJsonObject();
        auto cf = i->data(Qt::UserRole + 1).toBool();
        const QStringList projectTypes{"modpack", "mod", "resourcepack", "shader"};
        QUrl url(cf ? j["links"].toObject()["websiteUrl"].toString()
                    : "https://modrinth.com/" + projectTypes.value(searchType->currentIndex(), "modpack") +
                          "/" + j["slug"].toString());
        if (url.scheme() == "https")
            QDesktopServices::openUrl(url);
    });
    searchInstall->setToolTip("Install the latest pack release matching the active version and loader "
                              "filters. Some CurseForge files need browser downloads.");
    connect(searchType, &QTabBar::currentChanged, this, [this] { updateSearchMode(); });
    connect(searchTarget, &QComboBox::currentIndexChanged, this, [this] {
        if (searchType->currentIndex() >= 1)
            updateSearchMode();
    });
    return w;
}
void Window::scheduleSearch() {
    ++searchGeneration;
    if (searchCancellation)
        searchCancellation->store(true);
    searchPrevious->setEnabled(false);
    searchNext->setEnabled(false);
    results->clear();
    searchSummary->setText("Search changed…");
    if (!QCoreApplication::arguments().contains("--ui-test") &&
        !QCoreApplication::arguments().contains("--smoke-test"))
        searchTimer->start();
}
void Window::searchPacks(bool reset) {
    searchTimer->stop();
    searchStarted = true;
    if (reset)
        searchOffset = 0;
    const auto generation = ++searchGeneration;
    const bool loadVersions = searchVersion->count() == 1;
    if (searchCancellation)
        searchCancellation->store(true);
    auto cancelled = searchCancellation = std::make_shared<std::atomic_bool>(false);
    QString version = searchVersion->currentText().trimmed();
    if (searchVersion->currentIndex() == 0 && version == searchVersion->itemText(0))
        version.clear();
    Json options{{"version", version},
                 {"sort", searchSort->currentData().toString()},
                 {"offset", searchOffset},
                 {"limit", 20}};
    const bool cf = provider->currentIndex() == 1;
    const QStringList contentTypes{"modpack", "mod", "resourcepack", "shader"};
    options["type"] = contentTypes.value(searchType->currentIndex(), "modpack");
    if (!cf) {
        options["loader"] = searchLoader->currentData().toString();
        options["category"] = searchCategory->currentData().toString();
    }
    if (searchType->currentIndex() >= 1 && !searchTarget->currentData().toJsonObject().isEmpty()) {
        const auto instance = searchTarget->currentData().toJsonObject();
        options["version"] = instance["version"];
        if (searchType->currentIndex() == 1)
            options["loader"] = instance["loader"].toString().toLower();
    }
    results->clear();
    searchPrevious->setEnabled(false);
    searchNext->setEnabled(false);
    searchSummary->setText("Searching…");
    struct Response {
        Json page;
        QString error;
    };
    auto *watcher = new QFutureWatcher<Response>(this);
    connect(watcher, &QFutureWatcher<Response>::finished, this, [this, watcher, generation, cf, options] {
        const auto response = watcher->result();
        watcher->deleteLater();
        if (generation != searchGeneration)
            return;
        if (!response.error.isEmpty()) {
            searchSummary->setText("Search failed: " + response.error);
            return;
        }
        if (response.page.contains("gameVersions")) {
            QSignalBlocker block(searchVersion);
            const auto chosen = searchVersion->currentText();
            for (const auto &value : response.page["gameVersions"].toArray())
                searchVersion->addItem(value.toObject()["version"].toString());
            searchVersion->setCurrentText(chosen);
        }
        auto records = response.page["hits"].toArray();
        for (int i = 0; i < records.size(); ++i) {
            auto project = records[i].toObject();
            project["_search"] = options;
            records[i] = project;
        }
        showSearchResults(records, cf);
        const int total = response.page["total"].toInt();
        searchSummary->setText(total ? QString("%1–%2 of %3 results · %4")
                                           .arg(searchOffset + 1)
                                           .arg(searchOffset + records.size())
                                           .arg(total)
                                           .arg(cf ? "CurseForge" : "Modrinth")
                                     : "No results found. Try fewer filters or a different search.");
        searchPrevious->setEnabled(searchOffset > 0);
        searchNext->setEnabled(searchOffset + records.size() < (cf ? qMin(total, 10000) : total));
    });
    const auto path = root, text = query->text().trimmed(), key = settings.value("curseKey").toString();
    watcher->setFuture(QtConcurrent::run([path, text, key, cf, options, cancelled, loadVersions] {
        try {
            Engine engine(path);
            engine.http.cancellation = cancelled;
            auto page = engine.searchPage(text, cf, key, options);
            if (loadVersions) {
                try {
                    page["gameVersions"] = engine.http.array("https://api.modrinth.com/v2/tag/game_version");
                } catch (const std::exception &) {
                    engine.http.checkCancelled();
                }
            }
            return Response{page, {}};
        } catch (const std::exception &error) {
            return Response{{}, QString::fromUtf8(error.what())};
        }
    }));
}
void Window::showSearchResults(const QJsonArray &records, bool curse) {
    results->clear();
    searchSummary->setText(records.isEmpty() ? "No results found. Try fewer filters or a different search."
                                             : QString("%1 results on this page").arg(records.size()));
    for (const auto &value : records) {
        auto project = value.toObject();
        auto *item = new QListWidgetItem((curse ? project["name"] : project["title"]).toString());
        item->setData(Qt::UserRole, project);
        item->setData(Qt::UserRole + 1, curse);
        item->setToolTip((curse ? project["summary"] : project["description"]).toString());
        results->addItem(item);
        fetchProjectArtwork(results->model()->index(results->row(item), 0),
                            curse ? project["logo"].toObject()["url"].toString()
                                  : project["icon_url"].toString());
    }
    if (!records.isEmpty())
        results->setCurrentRow(0);
    else
        log("No results found. Try another search.");
}
void Window::fetchProjectArtwork(const QModelIndex &index, const QString &address) {
    const QUrl url(address);
    if (url.scheme() != "https" || url.host().isEmpty())
        return;
    const auto cache =
        root + "/cache/project-icons/" +
        QString::fromLatin1(QCryptographicHash::hash(address.toUtf8(), QCryptographicHash::Sha1).toHex()) +
        ".png";
    QPixmap existing(cache);
    if (!existing.isNull()) {
        results->model()->setData(index, existing, Qt::UserRole + 2);
        return;
    }
    if ((QCoreApplication::arguments().contains("--ui-test") ||
         QCoreApplication::arguments().contains("--smoke-test")) &&
        !QCoreApplication::arguments().contains("--network"))
        return;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "NekoLauncher/0.1");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);
    auto *reply = artworkNetwork->get(request);
    QPersistentModelIndex target(index);
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, bytes, target] {
        bytes->append(reply->readAll());
        if (bytes->size() > 4 * 1024 * 1024 || !target.isValid())
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, target, bytes, cache] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError || !target.isValid())
            return;
        bytes->append(reply->readAll());
        if (bytes->size() > 4 * 1024 * 1024)
            return;
        QBuffer buffer(bytes.get());
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const auto size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > 16000000)
            return;
        reader.setScaledSize(size.scaled(128, 128, Qt::KeepAspectRatio));
        const auto image = reader.read();
        if (image.isNull())
            return;
        const auto pixmap = QPixmap::fromImage(image);
        results->model()->setData(target, pixmap, Qt::UserRole + 2);
        if (QDir().mkpath(QFileInfo(cache).absolutePath()))
            pixmap.save(cache, "PNG");
    });
}
