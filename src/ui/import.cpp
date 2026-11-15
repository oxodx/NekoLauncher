#include "ui/widgets.hpp"
#include "window.hpp"
using namespace neko;
using namespace neko::ui;

void Window::missingDownloads(const Json &instance) {
    if (busy || instance["pendingDownloads"].toArray().isEmpty())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle("Complete modpack downloads");
    dialog.setObjectName("missingDownloads");
    dialog.resize(680, 430);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(22, 22, 22, 22);
    layout->setSpacing(14);
    layout->addWidget(label("A few files need a browser download", "sectionTitle"));
    layout->addWidget(
        label("Your pack has been saved. Download these exact versions from their official CurseForge pages, "
              "then select the files below. Neko checks their hashes before adding them to the pack.",
              "muted"));
    auto *list = new QListWidget;
    list->setTextElideMode(Qt::ElideMiddle);
    const auto pending = instance["pendingDownloads"].toArray();
    for (const auto &value : pending) {
        const auto file = value.toObject();
        auto *item = new QListWidgetItem(file["fileName"].toString(), list);
        item->setToolTip(file["fileName"].toString() + "\n" + file["pageUrl"].toString());
        item->setData(Qt::UserRole, file["pageUrl"].toString());
    }
    list->setCurrentRow(0);
    layout->addWidget(list, 1);
    button("Open selected file’s CurseForge page ↗", layout, [list] {
        if (!list->currentItem())
            return;
        const auto url = QUrl(list->currentItem()->data(Qt::UserRole).toString());
        if (url.scheme() == "https" && (url.host() == "www.curseforge.com" || url.host() == "curseforge.com"))
            QDesktopServices::openUrl(url);
    });
    QStringList files;
    button("Choose downloaded files…", layout, [&, this] {
        files =
            QFileDialog::getOpenFileNames(&dialog, "Select the downloaded mod files",
                                          QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                                          "Mod files (*.jar *.zip);;All files (*)");
        if (!files.isEmpty())
            dialog.accept();
    })->setObjectName("primary");
    button("Check Downloads folder", layout, [&] {
        const auto downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (!downloads.isEmpty()) {
            for (const auto &value : pending) {
                const auto expected = QFileInfo(value.toObject()["fileName"].toString()).completeBaseName();
                for (const auto &name : QDir(downloads).entryList(QDir::Files))
                    if (QFileInfo(name).completeBaseName().startsWith(expected))
                        files << QDir(downloads).filePath(name);
            }
        }
        files.removeDuplicates();
        // Already copied files in the instance are checked as well.
        dialog.accept();
    });
    button("Finish later", layout, [&] { dialog.reject(); });
    if (dialog.exec() != QDialog::Accepted)
        return;
    task([instance, files](Engine &e) { return QVariant(e.completeDownloads(instance, files)); },
         [this](QVariant result) {
             auto updated = result.toJsonObject();
             reload();
             if (updated["pendingDownloads"].toArray().isEmpty())
                 log("All missing files verified. The pack is ready to launch.");
             else {
                 log(QString("%1 file(s) still missing. Select the exact CurseForge file versions shown; "
                             "mismatched files were not installed.")
                         .arg(updated["pendingDownloads"].toArray().size()));
                 missingDownloads(updated);
             }
         });
}

void Window::importPack(QString path) {
    if (busy)
        return;
    if (path.isEmpty())
        path = QFileDialog::getOpenFileName(this, "Import a modpack", {}, "Modpacks (*.mrpack *.zip)");
    if (path.isEmpty())
        return;
    auto key = settings.value("curseKey").toString();
    task(
        [path, key](Engine &e) {
            auto instance = e.importPack(path, key);
            if (path.endsWith(".mrpack", Qt::CaseInsensitive)) {
                try {
                    QFile archive(path);
                    require(archive.open(QIODevice::ReadOnly), "Cannot identify imported pack");
                    QCryptographicHash hash(QCryptographicHash::Sha1);
                    require(hash.addData(&archive), "Cannot hash imported pack");
                    auto version =
                        e.http.json("https://api.modrinth.com/v2/version_file/" +
                                    QString::fromLatin1(hash.result().toHex()) + "?algorithm=sha1");
                    instance =
                        e.officialArtwork(instance, Json{{"project_id", version["project_id"]}}, false);
                } catch (const std::exception &) {
                    if (e.progress)
                        e.progress("Pack imported. Use the instance menu to link official artwork if it "
                                   "could not be identified.");
                }
            }
            return QVariant(instance);
        },
        [this](QVariant result) {
            reload();
            showPage(0);
            missingDownloads(result.toJsonObject());
        });
}
