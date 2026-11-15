#include "ui/mod_browser.hpp"
#include "ui/widgets.hpp"
#include "window.hpp"
using namespace neko;
using namespace neko::ui;

namespace {
QString compactCount(double value) {
    return value >= 1000000 ? QString::number(value / 1000000., 'f', 1) + "M"
           : value >= 1000  ? QString::number(value / 1000., 'f', 1) + "K"
                            : QString::number(qint64(value));
}
QStringList stringValues(const QJsonArray &values) {
    QStringList result;
    for (const auto &value : values) {
        const auto text = value.toString();
        if (!text.isEmpty() && !result.contains(text, Qt::CaseInsensitive))
            result << text;
    }
    return result;
}
void loadProjectImage(QNetworkAccessManager *network, QLabel *target, const QString &address,
                      const QSize &size, bool cover = false) {
    const QUrl url(address);
    if (url.scheme() != "https" || url.host().isEmpty())
        return;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "NekoLauncher/0.1");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);
    auto *reply = network->get(request);
    auto bytes = std::make_shared<QByteArray>();
    QObject::connect(reply, &QIODevice::readyRead, reply, [reply, bytes] {
        bytes->append(reply->readAll());
        if (bytes->size() > 8 * 1024 * 1024)
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, target, [reply, bytes, target, size, cover] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        bytes->append(reply->readAll());
        QImage image;
        if (bytes->size() > 8 * 1024 * 1024 || !image.loadFromData(*bytes) ||
            qint64(image.width()) * image.height() > 24000000)
            return;
        auto pixmap = QPixmap::fromImage(image);
        pixmap = pixmap.scaled(size, cover ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
        if (cover && (pixmap.width() > size.width() || pixmap.height() > size.height()))
            pixmap = pixmap.copy((pixmap.width() - size.width()) / 2,
                                 (pixmap.height() - size.height()) / 2, size.width(), size.height());
        target->setPixmap(pixmap);
    });
}
int showProjectDialog(QWidget *parent, const Json &details, bool curse, int mode) {
    QDialog dialog(parent);
    dialog.setObjectName("projectDetails");
    dialog.setWindowTitle("Project details");
    dialog.resize(960, 720);
    dialog.setMinimumSize(700, 520);
    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *network = new QNetworkAccessManager(&dialog);

    const auto title = (curse ? details["name"] : details["title"]).toString("Untitled project");
    const auto summary = (curse ? details["summary"] : details["description"]).toString();
    const auto iconUrl = curse ? details["logo"].toObject()["url"].toString()
                               : details["icon_url"].toString();
    QStringList authors;
    if (curse)
        for (const auto &value : details["authors"].toArray())
            authors << value.toObject()["name"].toString();
    else
        authors = stringValues(details["_authors"].toArray());

    auto *hero = new QWidget;
    hero->setObjectName("projectHero");
    auto *heroRow = new QHBoxLayout(hero);
    heroRow->setContentsMargins(34, 30, 34, 28);
    heroRow->setSpacing(24);
    auto *icon = new QLabel;
    icon->setObjectName("projectIcon");
    icon->setAlignment(Qt::AlignCenter);
    icon->setFixedSize(112, 112);
    icon->setPixmap(glyph(Glyph::Mods, dialog.palette().color(QPalette::Highlight)).pixmap(46, 46));
    heroRow->addWidget(icon);
    loadProjectImage(network, icon, iconUrl, icon->size(), true);
    auto *identity = new QVBoxLayout;
    identity->setSpacing(7);
    auto *kind = label(QStringList{"MODPACK", "MOD", "RESOURCE PACK", "SHADER"}.value(mode, "PROJECT") +
                           "  ·  " + (curse ? "CURSEFORGE" : "MODRINTH"),
                       "projectKicker");
    identity->addWidget(kind);
    auto *name = label(title, "projectTitle");
    name->setWordWrap(true);
    identity->addWidget(name);
    auto *tagline = label(summary, "projectSummary");
    tagline->setWordWrap(true);
    tagline->setMaximumHeight(48);
    identity->addWidget(tagline);
    QStringList byline;
    if (!authors.isEmpty())
        byline << "by " + authors.join(", ");
    const auto downloads = (curse ? details["downloadCount"] : details["downloads"]).toDouble();
    byline << compactCount(downloads) + " downloads";
    if (!curse && details["followers"].toDouble() > 0)
        byline << compactCount(details["followers"].toDouble()) + " followers";
    identity->addWidget(label(byline.join("   ·   "), "projectMeta"));
    identity->addStretch();
    heroRow->addLayout(identity, 1);
    outer->addWidget(hero);

    auto *content = new QSplitter;
    content->setObjectName("projectContent");
    content->setChildrenCollapsible(false);
    auto *descriptionPanel = new QWidget;
    auto *descriptionLayout = new QVBoxLayout(descriptionPanel);
    descriptionLayout->setContentsMargins(32, 26, 20, 24);
    descriptionLayout->setSpacing(14);
    QString galleryUrl;
    const auto gallery = curse ? details["screenshots"].toArray() : details["gallery"].toArray();
    if (!gallery.isEmpty())
        galleryUrl = gallery.first().toObject()["url"].toString();
    if (!galleryUrl.isEmpty()) {
        auto *feature = new QLabel;
        feature->setObjectName("projectFeature");
        feature->setAlignment(Qt::AlignCenter);
        feature->setMinimumHeight(180);
        feature->setMaximumHeight(240);
        feature->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        descriptionLayout->addWidget(feature);
        loadProjectImage(network, feature, galleryUrl, QSize(720, 240), true);
    }
    descriptionLayout->addWidget(label("About this project", "sectionTitle"));
    auto *description = new QTextBrowser;
    description->setObjectName("projectDescription");
    description->setOpenExternalLinks(false);
    description->setOpenLinks(false);
    description->setFrameShape(QFrame::NoFrame);
    if (curse) {
        const auto plain = QTextDocumentFragment::fromHtml(details["_descriptionHtml"].toString()).toPlainText();
        description->setPlainText(plain.isEmpty() ? summary : plain);
    } else {
        description->setMarkdown(details["body"].toString(summary));
    }
    descriptionLayout->addWidget(description, 1);
    content->addWidget(descriptionPanel);

    auto *facts = new QWidget;
    facts->setObjectName("projectFacts");
    facts->setMinimumWidth(245);
    facts->setMaximumWidth(310);
    auto *factsLayout = new QVBoxLayout(facts);
    factsLayout->setContentsMargins(22, 26, 26, 24);
    factsLayout->setSpacing(9);
    factsLayout->addWidget(label("Project details", "sectionTitle"));
    auto addFact = [&](const QString &caption, const QString &value) {
        if (value.trimmed().isEmpty())
            return;
        factsLayout->addWidget(label(caption.toUpper(), "projectFactLabel"));
        auto *text = label(value, "projectFactValue");
        text->setWordWrap(true);
        factsLayout->addWidget(text);
        factsLayout->addSpacing(5);
    };
    QStringList categories;
    for (const auto &value : details["categories"].toArray())
        categories << (curse ? value.toObject()["name"].toString() : value.toString());
    auto versions = stringValues(details[curse ? "gameVersions" : "game_versions"].toArray());
    if (curse && versions.isEmpty())
        for (const auto &value : details["latestFilesIndexes"].toArray())
            versions << value.toObject()["gameVersion"].toString();
    versions.removeDuplicates();
    addFact("Compatibility", versions.mid(0, 8).join(" · "));
    addFact("Loaders", stringValues(details["loaders"].toArray()).join(" · "));
    addFact("Categories", categories.mid(0, 8).join(" · "));
    if (!curse) {
        const auto license = details["license"].toObject();
        addFact("License", license["name"].toString(license["id"].toString()));
        addFact("Published", QDateTime::fromString(details["published"].toString(), Qt::ISODate)
                                 .toLocalTime()
                                 .date()
                                 .toString("d MMM yyyy"));
        addFact("Updated", QDateTime::fromString(details["updated"].toString(), Qt::ISODate)
                               .toLocalTime()
                               .date()
                               .toString("d MMM yyyy"));
    } else {
        addFact("Updated", QDateTime::fromString(details["dateModified"].toString(), Qt::ISODate)
                               .toLocalTime()
                               .date()
                               .toString("d MMM yyyy"));
    }
    factsLayout->addStretch();
    content->addWidget(facts);
    content->setStretchFactor(0, 1);
    outer->addWidget(content, 1);

    auto *footer = new QWidget;
    footer->setObjectName("projectFooter");
    auto *buttons = new QHBoxLayout(footer);
    buttons->setContentsMargins(28, 14, 28, 14);
    auto website = curse ? details["links"].toObject()["websiteUrl"].toString()
                         : "https://modrinth.com/" + details["project_type"].toString("project") + "/" +
                               details["slug"].toString();
    auto *websiteButton = button("Open project ↗", buttons, [website] {
        const QUrl url(website);
        if (url.scheme() == "https")
            QDesktopServices::openUrl(url);
    });
    websiteButton->setEnabled(QUrl(website).scheme() == "https");
    buttons->addStretch();
    button("Close", buttons, [&] { dialog.reject(); });
    auto *install = button(mode == 0 ? "Install modpack" : mode == 1 ? "Install mod"
                                                       : mode == 2   ? "Install resource pack"
                                                                     : "Install shader",
                           buttons, [&] { dialog.accept(); });
    install->setObjectName("primary");
    outer->addWidget(footer);
    return dialog.exec();
}
} // namespace

int neko::ui::showProjectDetails(QWidget *parent, const Json &details, bool curse, int mode) {
    return showProjectDialog(parent, details, curse, mode);
}

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
void Window::openSelectedProject() {
    if (busy)
        return;
    auto *item = results->currentItem();
    if (!item)
        return;
    const auto project = item->data(Qt::UserRole).toJsonObject();
    const bool curse = item->data(Qt::UserRole + 1).toBool();
    const int mode = searchType->currentIndex();
    const auto key = settings.value("curseKey").toString();
    task([=](Engine &engine) { return QVariant(engine.projectDetails(project, curse, key)); },
         [this, curse, mode](QVariant value) {
             if (showProjectDetails(this, value.toJsonObject(), curse, mode) == QDialog::Accepted)
                 searchInstall->click();
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
