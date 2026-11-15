#include "ui/widgets.hpp"
#include "window.hpp"
using namespace neko;
using namespace neko::ui;

void Window::newInstance() {
    if (busy)
        return;
    auto open = [this] {
        QDialog dialog(this);
        dialog.setWindowTitle("Create an instance");
        auto *f = new QFormLayout(&dialog);
        auto *name = entry("My world");
        f->addRow("Name", name);
        auto *version = new QComboBox;
        version->setEditable(true);
        for (const auto &v : catalog) {
            auto j = v.toObject();
            version->addItem(j["id"].toString() + "  (" + j["type"].toString() + ")", j["id"].toString());
        }
        f->addRow("Minecraft version", version);
        auto *loader = new QComboBox;
        loader->addItems({"Vanilla", "Fabric", "Quilt", "Forge", "NeoForge"});
        f->addRow("Mod loader", loader);
        f->addRow(label("All versions published in Mojang’s catalog are listed. Older builds may need Java 8 "
                        "and platform-specific native libraries.",
                        "muted"));
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        f->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        auto v = version->currentData().toString();
        if (v.isEmpty())
            v = version->currentText().trimmed();
        if (name->text().trimmed().isEmpty() || v.isEmpty())
            return;
        Json j{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
               {"name", name->text().trimmed()},
               {"version", v},
               {"loader", loader->currentText()},
               {"java", settings.value("java", "java").toString()},
               {"jvmArgs", settings.value("jvmArgs").toString()},
               {"memory", 4096},
               {"minMemory", 512},
               {"width", 1280},
               {"height", 720}};
        try {
            saveInstance(j);
            reload();
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Save failed", e.what());
        }
    };
    if (!catalog.isEmpty()) {
        open();
        return;
    }
    task(
        [](Engine &e) {
            try {
                return QVariant(e.manifest()["versions"].toArray());
            } catch (...) {
                return QVariant(readJson(e.root + "/cache/manifest.json")["versions"].toArray());
            }
        },
        [this, open](QVariant v) {
            catalog = v.toJsonArray();
            open();
        });
}
void Window::editInstance() {
    auto j = selected();
    if (j.isEmpty() || busy || (game && game->state() != QProcess::NotRunning))
        return;
    QDialog d(this);
    d.setWindowTitle("Instance settings");
    d.setObjectName("instanceEditor");
    d.resize(840, 660);
    d.setMinimumSize(600, 440);
    auto *outer = new QVBoxLayout(&d);
    auto *tabs = new QTabWidget;
    outer->addWidget(tabs, 1);
    auto *formWidget = new QWidget;
    auto *f = new QFormLayout(formWidget);
    f->setRowWrapPolicy(QFormLayout::WrapLongRows);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *runtimeScroll = new QScrollArea;
    runtimeScroll->setWidgetResizable(true);
    runtimeScroll->setWidget(formWidget);
    tabs->addTab(runtimeScroll, "Runtime");
    auto *componentWidget = new QWidget;
    auto *components = new QFormLayout(componentWidget);
    components->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto *componentScroll = new QScrollArea;
    componentScroll->setWidgetResizable(true);
    componentScroll->setWidget(componentWidget);
    tabs->insertTab(0, componentScroll, "Overview");
    tabs->setCurrentIndex(0);

    auto *name = entry(j["name"].toString());
    components->addRow("Name", name);
    auto *mcVersion = entry(j["version"].toString());
    components->addRow("Minecraft version", mcVersion);
    auto *loader = new QComboBox;
    loader->addItems({"Vanilla", "Fabric", "Quilt", "Forge", "NeoForge"});
    loader->setCurrentText(j["loader"].toString("Vanilla"));
    components->addRow("Mod loader", loader);
    auto *group = entry(j["group"].toString("Ungrouped"));
    components->addRow("Group", group);
    auto *favorite = new QCheckBox("Show in Favorites");
    favorite->setChecked(j["favorite"].toBool());
    components->addRow(favorite);
    auto *notes = new QPlainTextEdit(j["notes"].toString());
    notes->setMinimumHeight(120);
    components->addRow("Notes", notes);
    components->addRow(label("Changing Minecraft or the loader keeps your files. Use compatible mods, or "
                             "duplicate the instance before changing versions.",
                             "muted"));

    auto *java = entry(j["java"].toString("java"));
    f->addRow("Preferred Java executable", java);
    auto *javaAuto = new QCheckBox("Automatically select a matching installed Java runtime");
    javaAuto->setChecked(j["javaAuto"].toBool(true));
    f->addRow(javaAuto);
    auto *runtimeStatus =
        label(j["lastJava"].toString().isEmpty() ? "Java is checked automatically before launching."
                                                 : "Last used: " + j["lastJava"].toString(),
              "muted");
    f->addRow(runtimeStatus);
    button("Detect compatible Java", f, [this, j, java, runtimeStatus] {
        QPointer<QLineEdit> field(java);
        QPointer<QLabel> message(runtimeStatus);
        auto preferred = java->text();
        task(
            [j, preferred](Engine &e) {
                auto required =
                    e.version(j["version"].toString())["javaVersion"].toObject()["majorVersion"].toInt(8);
                auto runtime = selectJava(preferred, required, true, {e.root + "/runtimes"});
                return QVariant(runtime.executable);
            },
            [field, message](QVariant value) {
                if (field)
                    field->setText(value.toString());
                if (message)
                    message->setText("Compatible runtime found. Save to use this preference.");
            });
    });
    button("Browse Java…", f, [this, java] {
        auto p = QFileDialog::getOpenFileName(this, "Select Java executable");
        if (!p.isEmpty())
            java->setText(p);
    });
    auto spin = [&](const QString &title, const char *key, int def, int min, int max) {
        auto *s = new QSpinBox;
        s->setRange(min, max);
        s->setValue(j[key].toInt(def));
        f->addRow(title, s);
        return s;
    };
    auto *memory = spin("Maximum RAM (MiB)", "memory", 4096, 512, 262144);
    auto *minimum = spin("Initial RAM (MiB)", "minMemory", 512, 128, 262144);
    auto *width = spin("Window width", "width", 1280, 320, 16384);
    auto *height = spin("Window height", "height", 720, 240, 16384);
    auto *jvm = entry(j["jvmArgs"].toString());
    f->addRow("JVM arguments", jvm);
    auto *args = entry(j["gameArgs"].toString());
    f->addRow("Game arguments", args);
    auto *lv = entry(j["loaderVersion"].toString(), "Latest compatible loader on first launch");
    f->addRow("Loader version", lv);
    f->addRow(label("Use Java 8 for most older releases, Java 17 for 1.18–1.20.4, and Java 21 for 1.20.5+. "
                    "Check newer versions’ runtime requirements in Console. Arguments are split into process "
                    "arguments; shell scripts are not evaluated.",
                    "muted"));
    auto *displayBackend = new QComboBox;
    displayBackend->addItem("Automatic", "auto");
    displayBackend->addItem("X11 / XWayland", "x11");
    displayBackend->addItem("Wayland", "wayland");
    displayBackend->setCurrentIndex(qMax(0, displayBackend->findData(j["linuxDisplay"].toString("auto"))));
    f->addRow("Linux display backend", displayBackend);
    displayBackend->setToolTip("Try X11/XWayland when SDL reports EGL_BAD_DISPLAY on Wayland/NVIDIA. The "
                               "selected protocol must be available in your desktop session.");
    auto *audioBackend = new QComboBox;
    audioBackend->addItem("Automatic", "auto");
    audioBackend->addItem("PipeWire", "pipewire");
    audioBackend->addItem("PulseAudio", "pulse");
    audioBackend->addItem("ALSA", "alsa");
    audioBackend->setCurrentIndex(qMax(0, audioBackend->findData(j["linuxAudio"].toString("auto"))));
    audioBackend->setToolTip("Automatic preserves OpenAL’s normal selection. Use a specific backend only "
                             "when troubleshooting audio; restart Minecraft after changing it.");
    f->addRow("Linux audio backend", audioBackend);
    auto *nativePaths =
        entry(j["nativePaths"].toString(), "Extra native-library directories, separated by ':' on Linux");
    f->addRow("Native library paths", nativePaths);
    auto *environment =
        new QPlainTextEdit(QString::fromUtf8(QJsonDocument(j["environment"].toObject()).toJson()));
    environment->setMinimumHeight(100);
    f->addRow("Environment (JSON object)", environment);
    const auto instanceDir = root + "/instances/" + j["id"].toString();
    auto browseContent = [this, j](const QString &kind, const QString &title,
                                   const std::function<void()> &refresh) {
        auto *dialog = new QDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowModality(Qt::WindowModal);
        dialog->setWindowTitle("Download " + title);
        dialog->resize(720, 520);
        auto *layout = new QVBoxLayout(dialog);
        layout->addWidget(label("Download " + title + " for " + j["name"].toString(), "sectionTitle"));
        layout->addWidget(label(kind == "world"
                                    ? "World projects are available through CurseForge. Downloads are "
                                      "extracted into a new save without replacing existing worlds."
                                    : "Search by the instance's Minecraft version. Downloads are verified "
                                      "when the provider publishes a SHA-1 hash.",
                                "muted"));
        auto *toolbar = new QHBoxLayout;
        layout->addLayout(toolbar);
        auto *provider = new QComboBox;
        if (kind == "world")
            provider->addItem("CurseForge", true);
        else {
            provider->addItem("Modrinth", false);
            provider->addItem("CurseForge", true);
        }
        toolbar->addWidget(provider);
        auto *query = entry({}, "Search " + title.toLower() + "…");
        toolbar->addWidget(query, 1);
        auto *search = button("Search", toolbar, [] {});
        auto *summary = label("Search to find compatible projects.", "muted");
        layout->addWidget(summary);
        auto *results = new QListWidget;
        results->setTextElideMode(Qt::ElideRight);
        layout->addWidget(results, 1);
        auto *actions = new QHBoxLayout;
        layout->addLayout(actions);
        auto *project = button("Project page ↗", actions, [] {});
        actions->addStretch();
        auto *install = button("Download", actions, [] {});
        install->setObjectName("primary");
        project->setEnabled(false);
        install->setEnabled(false);
        auto performSearch = [=, this] {
            const bool curse = provider->currentData().toBool();
            const auto key = settings.value("curseKey").toString();
            Json options{{"type", kind},
                         {"version", j["version"]},
                         {"sort", "downloads"},
                         {"limit", 40}};
            QPointer<QDialog> alive(dialog);
            task(
                [text = query->text().trimmed(), curse, key, options](Engine &engine) {
                    return QVariant(engine.searchPage(text, curse, key, options));
                },
                [=](QVariant value) {
                    if (!alive)
                        return;
                    results->clear();
                    const auto hits = value.toJsonObject()["hits"].toArray();
                    for (const auto &entry : hits) {
                        const auto item = entry.toObject();
                        auto *row = new QListWidgetItem(
                            (curse ? item["name"] : item["title"]).toString(), results);
                        row->setData(Qt::UserRole, item);
                        row->setData(Qt::UserRole + 1, curse);
                        row->setToolTip((curse ? item["summary"] : item["description"]).toString());
                    }
                    summary->setText(hits.isEmpty() ? "No compatible projects found."
                                                    : QString("%1 project(s) found.").arg(hits.size()));
                    if (!hits.isEmpty())
                        results->setCurrentRow(0);
                });
        };
        disconnect(search, nullptr, nullptr, nullptr);
        connect(search, &QPushButton::clicked, dialog, performSearch);
        connect(query, &QLineEdit::returnPressed, dialog, performSearch);
        connect(results, &QListWidget::currentItemChanged, dialog, [=](QListWidgetItem *item) {
            project->setEnabled(item);
            install->setEnabled(item);
        });
        disconnect(project, nullptr, nullptr, nullptr);
        connect(project, &QPushButton::clicked, dialog, [=] {
            auto *item = results->currentItem();
            if (!item)
                return;
            const auto metadata = item->data(Qt::UserRole).toJsonObject();
            const bool curse = item->data(Qt::UserRole + 1).toBool();
            const auto url = curse ? metadata["links"].toObject()["websiteUrl"].toString()
                                   : "https://modrinth.com/" + kind + "/" + metadata["slug"].toString();
            const QUrl page(url);
            if (page.scheme() == "https")
                QDesktopServices::openUrl(page);
        });
        disconnect(install, nullptr, nullptr, nullptr);
        connect(install, &QPushButton::clicked, dialog, [=, this] {
            auto *item = results->currentItem();
            if (!item)
                return;
            const auto metadata = item->data(Qt::UserRole).toJsonObject();
            const bool curse = item->data(Qt::UserRole + 1).toBool();
            const auto key = settings.value("curseKey").toString();
            QPointer<QDialog> alive(dialog);
            task(
                [j, metadata, kind, curse, key](Engine &engine) {
                    return QVariant(engine.installContent(j, metadata, kind, curse, key));
                },
                [alive, refresh](QVariant) {
                    refresh();
                    if (alive)
                        alive->accept();
                });
        });
        dialog->show();
    };
    auto filesTab = [&](const QString &title, const QString &child, const QString &patterns, bool toggle) {
        auto *panel = new QWidget;
        auto *layout = new QVBoxLayout(panel);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->addWidget(label(title + " for " + j["name"].toString(), "hero"));
        auto *list = new QListWidget;
        list->setMinimumHeight(160);
        layout->addWidget(list, 1);
        auto dir = instanceDir + "/" + child;
        list->setIconSize(QSize(144, 81));
        auto refresh = [list, dir, child, toggle] {
            list->clear();
            QDir folder(dir);
            for (const auto &file :
                 folder.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                      QDir::DirsFirst | QDir::Name)) {
                auto *item =
                    new QListWidgetItem((toggle ? (file.fileName().endsWith(".disabled") ? "○  " : "●  ")
                                         : file.isDir() ? "▣  "
                                                        : "≡  ") +
                                        file.fileName());
                item->setToolTip(file.fileName());
                item->setData(Qt::UserRole, file.absoluteFilePath());
                if (child == "screenshots" && file.isFile()) {
                    QPixmap preview(file.absoluteFilePath());
                    if (!preview.isNull())
                        item->setIcon(QIcon(preview.scaled(144, 81, Qt::KeepAspectRatio,
                                                          Qt::SmoothTransformation)));
                    item->setText(file.completeBaseName() + "  ·  " +
                                  QLocale().formattedDataSize(file.size()) + "  ·  " +
                                  file.lastModified().toString("yyyy-MM-dd HH:mm"));
                } else if (child == "saves" && file.isDir()) {
                    const QFileInfo level(file.absoluteFilePath() + "/level.dat");
                    item->setText("▣  " + file.fileName() +
                                  (level.exists() ? "  ·  played " +
                                                        level.lastModified().toString("yyyy-MM-dd HH:mm")
                                                  : "  ·  incomplete world"));
                }
                list->addItem(item);
            }
        };
        refresh();
        connect(list, &QListWidget::itemDoubleClicked, panel, [this, refresh, toggle](QListWidgetItem *item) {
            auto path = item->data(Qt::UserRole).toString();
            if (toggle && QFileInfo(path).isFile()) {
                auto target = path.endsWith(".disabled") ? path.left(path.size() - 9) : path + ".disabled";
                if (!QFile::rename(path, target))
                    QMessageBox::warning(this, "Cannot toggle file", "The file cannot be renamed.");
                refresh();
            } else
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
        auto *buttons = new QHBoxLayout;
        layout->addLayout(buttons);
        if (toggle)
            button("Add files…", buttons, [this, dir, patterns, refresh] {
                auto paths = QFileDialog::getOpenFileNames(this, "Add files", {}, patterns);
                QDir().mkpath(dir);
                for (const auto &path : paths)
                    if (!QFile::copy(path, dir + "/" + QFileInfo(path).fileName()))
                        QMessageBox::warning(this, "Cannot add file",
                                             QFileInfo(path).fileName() +
                                                 " already exists or could not be copied.");
                refresh();
            });
        if (child == "saves") {
            button("Import world ZIP…", buttons, [this, j, refresh] {
                const auto archive = QFileDialog::getOpenFileName(this, "Import Java Edition world", {},
                                                                  "World archives (*.zip)");
                if (!archive.isEmpty())
                    task([j, archive](Engine &engine) { return QVariant(engine.importWorld(j, archive)); },
                         [refresh](QVariant) { refresh(); });
            });
            button("Browse CurseForge…", buttons,
                   [browseContent, refresh] { browseContent("world", "worlds", refresh); });
        } else if (child == "resourcepacks") {
            button("Browse online…", buttons,
                   [browseContent, refresh] { browseContent("resourcepack", "resource packs", refresh); });
        } else if (child == "shaderpacks") {
            button("Browse online…", buttons,
                   [browseContent, refresh] { browseContent("shader", "shaders", refresh); });
        } else if (child == "screenshots") {
            button("Export selected…", buttons, [this, list] {
                if (!list->currentItem())
                    return;
                const auto source = list->currentItem()->data(Qt::UserRole).toString();
                const auto target = QFileDialog::getSaveFileName(this, "Export screenshot",
                                                                 QFileInfo(source).fileName(),
                                                                 "Images (*.png *.jpg *.jpeg)");
                if (!target.isEmpty() && !QFile::copy(source, target))
                    QMessageBox::warning(this, "Export failed", "The screenshot could not be copied.");
            });
            button("Delete selected", buttons, [this, list, refresh] {
                if (!list->currentItem())
                    return;
                const auto path = list->currentItem()->data(Qt::UserRole).toString();
                if (QMessageBox::question(this, "Delete screenshot",
                                          "Delete " + QFileInfo(path).fileName() + " permanently?") ==
                        QMessageBox::Yes &&
                    !QFile::remove(path))
                    QMessageBox::warning(this, "Delete failed", "The screenshot could not be removed.");
                refresh();
            });
        }
        button("Open folder", buttons, [dir] {
            QDir().mkpath(dir);
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        });
        button("Refresh", buttons, refresh);
        if (toggle)
            layout->addWidget(label(
                "Double-click a file to enable or disable it. Dependencies are managed manually.", "muted"));
        tabs->addTab(panel, title);
    };
    filesTab("Mods", "mods", "Java mods (*.jar)", true);
    filesTab("Resource packs", "resourcepacks", "Resource packs (*.zip)", true);
    filesTab("Shaders", "shaderpacks", "Shader packs (*.zip)", true);
    filesTab("Worlds", "saves", {}, false);
    filesTab("Screenshots", "screenshots", "Images (*.png *.jpg *.jpeg)", false);
    filesTab("Logs", "logs", {}, false);
    filesTab("Crash reports", "crash-reports", {}, false);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, [&] {
        QJsonParseError error;
        const auto parsed = QJsonDocument::fromJson(environment->toPlainText().toUtf8(), &error);
        bool strings = true;
        for (const auto &value : parsed.object())
            strings &= value.isString();
        if (name->text().trimmed().isEmpty() || java->text().trimmed().isEmpty() ||
            mcVersion->text().trimmed().isEmpty() || minimum->value() > memory->value() ||
            error.error != QJsonParseError::NoError || !parsed.isObject() || !strings) {
            QMessageBox::warning(
                &d, "Invalid instance settings",
                "Name, Minecraft version and Java are required. Initial memory must not exceed maximum "
                "memory. Environment must be a JSON object with string values.");
            return;
        }
        d.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return;
    if (name->text().trimmed().isEmpty() || java->text().trimmed().isEmpty() ||
        minimum->value() > memory->value()) {
        QMessageBox::warning(this, "Invalid settings",
                             "Name and Java are required; initial RAM cannot exceed maximum RAM.");
        return;
    }
    QJsonParseError envError;
    auto env = QJsonDocument::fromJson(environment->toPlainText().toUtf8(), &envError);
    if (envError.error != QJsonParseError::NoError || !env.isObject() ||
        mcVersion->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Invalid settings",
                             "Provide a Minecraft version and a JSON object for environment variables.");
        return;
    }
    for (const auto &v : env.object())
        if (!v.isString()) {
            QMessageBox::warning(this, "Invalid environment", "Environment values must be strings.");
            return;
        }
    if (j["version"].toString() != mcVersion->text().trimmed() ||
        j["loader"].toString() != loader->currentText()) {
        QFile::remove(instanceDir + "/loader.json");
        j.remove("profile");
    }
    j["version"] = mcVersion->text().trimmed();
    j["loader"] = loader->currentText();
    j["group"] = group->text().trimmed().isEmpty() ? "Ungrouped" : group->text().trimmed();
    j["favorite"] = favorite->isChecked();
    j["notes"] = notes->toPlainText();
    j["nativePaths"] = nativePaths->text();
    j["linuxDisplay"] = displayBackend->currentData().toString();
    j["linuxAudio"] = audioBackend->currentData().toString();
    j["environment"] = env.object();
    j["name"] = name->text().trimmed();
    j["java"] = java->text().trimmed();
    j["javaAuto"] = javaAuto->isChecked();
    j["memory"] = memory->value();
    j["minMemory"] = minimum->value();
    j["width"] = width->value();
    j["height"] = height->value();
    j["jvmArgs"] = jvm->text();
    j["gameArgs"] = args->text();
    if (j["loaderVersion"].toString() != lv->text())
        QFile::remove(root + "/instances/" + j["id"].toString() + "/loader.json");
    j["loaderVersion"] = lv->text();
    try {
        saveInstance(j);
        reload();
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "Save failed", e.what());
    }
}
