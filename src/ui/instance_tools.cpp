#include "ui/instance_tools.hpp"
#include "instance_services.hpp"
#include "ui/command_palette.hpp"
#include "ui/widgets.hpp"
#include "window.hpp"
using namespace neko;
using namespace neko::ui;

void Window::commandPalette() {
    QList<PaletteCommand> commands;
    const auto current = selected();
    const bool running = game && game->state() != QProcess::NotRunning;
    const bool openOnly = running || busy;
    for (int row = 0; row < instances->count(); ++row) {
        const auto instance = instances->item(row)->data(Qt::UserRole).toJsonObject();
        const auto id = instance["id"].toString();
        commands.append(
            {(openOnly ? "Open " : "Play ") + instance["name"].toString(),
             "Minecraft " + instance["version"].toString() + " · " + instance["loader"].toString("Vanilla"),
             instance["group"].toString() + " launch instance",
             [this, id, openOnly] {
                 instanceSearch->clear();
                 favoritesOnly->setChecked(false);
                 groupFilter->setCurrentIndex(0);
                 for (int i = 0; i < instances->count(); ++i)
                     if (instances->item(i)->data(Qt::UserRole).toJsonObject()["id"] == id) {
                         instances->setCurrentRow(i);
                         break;
                     }
                 showPage(0);
                 if (!openOnly)
                     launch();
             },
             id == current["id"].toString() ? 60 : 20});
    }
    commands.append({"New instance", "Start a separate Minecraft setup", "create vanilla fabric forge",
                     [this] { newInstance(); }});
    commands.append({"Import a modpack", "Modrinth, CurseForge or a Neko backup", "zip mrpack backup",
                     [this] { importPack(); }});
    const QStringList pages{"Library",      "Discover modpacks", "Manage mods",
                            "Your account", "Settings",          "Live console"};
    for (int i = 0; i < pages.size(); ++i)
        commands.append({pages[i], "Open page", "navigate", [this, i] { showPage(i); }});
    if (!current.isEmpty()) {
        commands.append({"Check instance health", current["name"].toString(),
                         "doctor java memory storage missing downloads", [this] { checkInstance(); }});
        commands.append({"World snapshots", "Protect and recover local worlds", "saves backup restore safety",
                         [this] { worldSnapshots(); }});
        commands.append({"Session history", "Playtime and recent exit results", "activity crash sessions",
                         [this] { showHistory(); }});
        commands.append({"Edit selected instance", current["name"].toString(), "java memory configure",
                         [this] { editInstance(); }});
        commands.append({"Open instance folder", current["name"].toString(), "files directory mods saves",
                         [this] { openInstanceFolder(); }});
    }
    if (running)
        commands.append(
            {"Stop Minecraft", "End the running game process", "quit stop", [this] { stopGame(); }});
    CommandPalette palette(std::move(commands), this);
    if (palette.exec() == QDialog::Accepted)
        if (auto action = palette.selectedAction())
            action();
}
void Window::checkInstance() {
    const auto instance = selected();
    if (instance.isEmpty() || busy)
        return;
    auto report = std::make_shared<QList<InstanceCheck>>();
    task(
        [instance, report](Engine &engine) {
            *report = inspectInstance(engine, instance);
            return QVariant{};
        },
        [this, report, instance](QVariant) { showHealthReport(this, instance, *report); });
}
void Window::worldSnapshots() {
    const auto instance = selected();
    if (instance.isEmpty() || busy)
        return;
    try {
        WorldSnapshots snapshots(root);
        const auto records = snapshots.list(instance["id"].toString());
        QDialog dialog(this);
        dialog.setObjectName("worldSnapshots");
        dialog.setWindowTitle("World snapshots");
        dialog.resize(740, 500);
        auto *layout = new QVBoxLayout(&dialog);
        layout->setSpacing(14);
        layout->addWidget(label("Keep your worlds close", "sectionTitle"));
        layout->addWidget(
            label("Snapshots contain this instance’s saves. Close Minecraft before creating or restoring "
                  "one. A restore first makes a recovery snapshot of your current worlds.",
                  "muted"));
        auto *automatic = new QCheckBox("Snapshot worlds before launch");
        automatic->setChecked(instance["snapshotBeforeLaunch"].toBool());
        layout->addWidget(automatic);
        auto *retention = new QSpinBox;
        retention->setRange(1, 50);
        retention->setValue(qBound(1, instance["snapshotKeep"].toInt(5), 50));
        auto *retentionRow = new QFormLayout;
        retentionRow->addRow("Automatic snapshots to keep", retention);
        layout->addLayout(retentionRow);
        auto savePreferences = [this, automatic, retention, id = instance["id"].toString()] {
            try {
                auto fresh = readJson(safePath(root, "instances/" + id + "/instance.json"));
                fresh["snapshotBeforeLaunch"] = automatic->isChecked();
                fresh["snapshotKeep"] = retention->value();
                saveInstance(fresh);
                reload();
            } catch (const std::exception &error) {
                log(error.what());
            }
        };
        connect(automatic, &QCheckBox::toggled, this, [savePreferences](bool) { savePreferences(); });
        connect(retention, qOverload<int>(&QSpinBox::valueChanged), this,
                [savePreferences](int) { savePreferences(); });
        auto *list = new QTreeWidget;
        list->setHeaderLabels({"Created", "Type", "Minecraft", "Size"});
        list->setRootIsDecorated(false);
        layout->addWidget(list, 1);
        for (const auto &value : records) {
            const auto record = value.toObject();
            auto *item = new QTreeWidgetItem(
                list, {QDateTime::fromSecsSinceEpoch(qint64(record["createdAt"].toDouble()))
                           .toString("d MMM yyyy · HH:mm:ss"),
                       record["reason"].toString(record["automatic"].toBool() ? "Automatic" : "Manual"),
                       record["version"].toString(),
                       QString("%1 MiB").arg(record["bytes"].toDouble() / 1048576., 0, 'f', 1)});
            item->setData(0, Qt::UserRole, record);
        }
        list->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        if (list->topLevelItemCount())
            list->setCurrentItem(list->topLevelItem(0));
        const bool running = game && game->state() != QProcess::NotRunning;
        enum class Choice { None, Create, Restore };
        Choice choice = Choice::None;
        Json chosen;
        auto *create = button("Create snapshot now", layout, [&] {
            choice = Choice::Create;
            dialog.accept();
        });
        create->setEnabled(!running);
        create->setObjectName("primary");
        auto *restore = button("Restore selected worlds…", layout, [&] {
            if (!list->currentItem())
                return;
            chosen = list->currentItem()->data(0, Qt::UserRole).toJsonObject();
            if (QMessageBox::question(&dialog, "Restore these worlds?",
                                      "Replace the current saves with the selected snapshot? A recovery "
                                      "snapshot of your current worlds is created first.") !=
                QMessageBox::Yes)
                return;
            choice = Choice::Restore;
            dialog.accept();
        });
        restore->setEnabled(!running && !records.isEmpty());
        button("Close", layout, [&] { dialog.reject(); });
        if (dialog.exec() != QDialog::Accepted || choice == Choice::None)
            return;
        task(
            [instance, chosen, choice](Engine &engine) {
                WorldSnapshots service(engine.root, [&engine] { engine.http.checkCancelled(); });
                if (choice == Choice::Create)
                    service.create(instance["id"].toString());
                else
                    service.restore(instance["id"].toString(), chosen["id"].toString());
                return QVariant{};
            },
            [this, id = instance["id"].toString()](QVariant) {
                reload();
                if (selected()["id"].toString() == id)
                    worldSnapshots();
            });
    } catch (const std::exception &error) {
        QMessageBox::warning(this, "World snapshots", error.what());
    }
}
void Window::showHistory() {
    const auto instance = selected();
    if (instance.isEmpty())
        return;
    try {
        const auto sessions = sessionHistory(root, instance["id"].toString());
        const auto statistics = sessionStatistics(root, instance["id"].toString());
        QDialog dialog(this);
        dialog.setObjectName("sessionHistory");
        dialog.setWindowTitle("Session history");
        dialog.resize(740, 450);
        auto *layout = new QVBoxLayout(&dialog);
        layout->setSpacing(14);
        layout->addWidget(label(instance["name"].toString(), "sectionTitle"));
        layout->addWidget(label(formatDuration(qint64(statistics["totalSeconds"].toDouble())) + " played · " +
                                    QString::number(statistics["sessionCount"].toInt()) +
                                    " completed sessions",
                                "muted"));
        auto *list = new QTreeWidget;
        list->setRootIsDecorated(false);
        list->setHeaderLabels({"Started", "Duration", "Result", "Exit code"});
        layout->addWidget(list, 1);
        for (const auto &value : sessions) {
            const auto session = value.toObject();
            auto *item = new QTreeWidgetItem(
                list, {QDateTime::fromSecsSinceEpoch(qint64(session["startedAt"].toDouble()))
                           .toString("d MMM yyyy · HH:mm"),
                       formatDuration(qint64(session["durationSeconds"].toDouble())),
                       session["stopped"].toBool()   ? "Stopped"
                       : session["crashed"].toBool() ? "Crashed"
                       : session["exitCode"].toInt() ? "Exited with error"
                                                     : "Finished",
                       QString::number(session["exitCode"].toInt())});
            item->setToolTip(0, "Java: " + session["java"].toString());
        }
        list->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        if (sessions.isEmpty())
            layout->addWidget(label("Completed game sessions appear here after Minecraft exits.", "muted"));
        button("Close", layout, [&] { dialog.accept(); });
        dialog.exec();
    } catch (const std::exception &error) {
        QMessageBox::warning(this, "Session history", error.what());
    }
}

void neko::ui::showHealthReport(QWidget *parent, const Json &instance, const QList<InstanceCheck> &report) {
    QDialog dialog(parent);
    dialog.setObjectName("instanceHealth");
    dialog.setWindowTitle("Instance health");
    dialog.resize(720, 500);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(14);
    auto *title = label("Health check · " + instance["name"].toString(), "sectionTitle");
    layout->addWidget(title);
    auto *list = new QListWidget;
    list->setWordWrap(true);
    list->setSpacing(6);
    layout->addWidget(list, 1);
    QString plain = instance["name"].toString() + "\n";
    for (const auto &check : report) {
        const auto state = check.state == CheckState::Pass      ? "OK"
                           : check.state == CheckState::Warning ? "NOTE"
                                                                : "FIX";
        auto *item = new QListWidgetItem(QString(state) + " · " + check.title + "\n" + check.detail, list);
        item->setToolTip(check.detail);
        plain += QString(state) + " · " + check.title + "\n" + check.detail + "\n\n";
    }
    button("Copy report", layout, [plain] { QApplication::clipboard()->setText(plain); });
    button("Close", layout, [&] { dialog.accept(); });
    dialog.exec();
}
