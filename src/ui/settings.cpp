#include "ui/widgets.hpp"
#include "window.hpp"
using namespace neko;
using namespace neko::ui;

QWidget *Window::settingsPage() {
    QVBoxLayout *l;
    auto *w = page(l, "Make it yours",
                   "Defaults for new instances, service connections and a look that fits your desktop.");
    auto section = [l](const QString &title) {
        auto *card = new QGroupBox(title);
        auto *form = new QFormLayout(card);
        form->setHorizontalSpacing(28);
        form->setVerticalSpacing(14);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        l->addWidget(card);
        return form;
    };
    auto *f = section("Appearance");
    theme = new QComboBox;
    theme->addItems({"Shadcn Dark", "Shadcn Light", "Catppuccin Mocha", "Catppuccin Macchiato",
                     "Catppuccin Frappé", "Catppuccin Latte", "Midnight", "Forest", "Rose", "Light"});
    theme->setCurrentText(settings.value("theme", "Shadcn Dark").toString());
    f->addRow("Color palette", theme);
    fontSize = new QComboBox;
    fontSize->addItems({"12", "13", "14", "15", "16", "18"});
    fontSize->setCurrentText(settings.value("fontSize", "14").toString());
    f->addRow("Interface text size", fontSize);
    connect(theme, &QComboBox::currentTextChanged, this, [this](const QString &s) {
        settings.setValue("theme", s);
        applyTheme();
    });
    connect(fontSize, &QComboBox::currentTextChanged, this, [this](const QString &s) {
        settings.setValue("fontSize", s);
        applyTheme();
    });
    f = section("Runtime defaults");
    javaDefault = entry(settings.value("java", "java").toString());
    f->addRow("Default Java executable", javaDefault);
    jvmDefault = entry(settings.value("jvmArgs").toString(), "Optional JVM flags");
    f->addRow("Default JVM arguments", jvmDefault);
    f = section("Connections");
    auto configuredClientId = settings.value("clientId").toString().trimmed();
    clientId = entry(configuredClientId.isEmpty() ? QString::fromLatin1(DefaultMicrosoftClientId)
                                                  : configuredClientId,
                     "Leave blank to use the built-in application ID");
    f->addRow("Microsoft application (client) ID", clientId);
    clientId->setToolTip("Application/client ID from your public/mobile Entra app registration. This is not "
                         "an access token or a client secret.");
    curseKey = entry(settings.value("curseKey").toString(), "Required for CurseForge requests");
    curseKey->setEchoMode(QLineEdit::Password);
    f->addRow("CurseForge API key", curseKey);
    f->addRow(label(
        "The Microsoft app registration must support personal Microsoft accounts. In Authentication, enable "
        "Allow public client flows (mobile/desktop app), save, and allow public client/device flows "
        "and Minecraft services access. Neko includes its public Microsoft application ID. You may override "
        "it here. "
        "The CurseForge API key is stored in your local Qt settings.",
        "muted"));
    button("Save defaults & connections", f, [this] {
        settings.setValue("java", javaDefault->text().trimmed());
        settings.setValue("jvmArgs", jvmDefault->text());
        settings.setValue("clientId", clientId->text().trimmed());
        settings.setValue("curseKey", curseKey->text().trimmed());
        settings.sync();
        log("Settings saved.");
    });
    f = section("Storage");
    f->addRow("Data directory", label(root, "muted"));
    button("Open data directory", f, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(root)); });
    f = section("Advanced appearance");
    auto *reduceMotion = new QCheckBox("Reduce interface animations");
    reduceMotion->setChecked(settings.value("reduceMotion", false).toBool());
    f->addRow(reduceMotion);
    connect(reduceMotion, &QCheckBox::toggled, this, [this](bool enabled) {
        settings.setValue("reduceMotion", enabled);
        for (auto *button : navigation->buttons())
            button->setProperty("reduceMotion", enabled);
        if (enabled && pageTransition) {
            pageTransition->stop();
            if (auto *effect = qobject_cast<QGraphicsOpacityEffect *>(pages->graphicsEffect()))
                effect->setOpacity(1);
        }
    });
    auto *customStyle = new QPlainTextEdit(settings.value("customStyle").toString());
    customStyle->setPlaceholderText(
        "/* Optional Qt stylesheet overrides */\nQPushButton#primary { border-radius: 18px; }");
    customStyle->setMinimumHeight(120);
    f->addRow("Custom stylesheet (QSS)", customStyle);
    button("Apply custom style", f, [this, customStyle] {
        settings.setValue("customStyle", customStyle->toPlainText());
        applyTheme();
    });
    button("Reset appearance", f, [this, customStyle] {
        settings.remove("customStyle");
        customStyle->clear();
        theme->setCurrentText("Shadcn Dark");
        fontSize->setCurrentText("14");
        applyTheme();
    });
    stop = button("Stop running game", f, [this] {
        if (game && game->state() != QProcess::NotRunning)
            stopGame();
    });
    auto *storage = section("Mod storage");
    auto *storageNote = label("Identical mod JARs share read-only files across instances. Worlds, settings "
                              "and accounts stay separate. Optimize existing mods to reclaim space; "
                              "filesystems without hard-link support keep private copies.",
                              "muted");
    storage->addRow(storageNote);
    auto *storageButtons = new QVBoxLayout;
    button("Optimize existing mods", storageButtons, [this] { storageAction(0); })->setObjectName("primary");
    button("Storage usage", storageButtons, [this] { storageAction(1); });
    button("Clean unused shared files", storageButtons, [this] { storageAction(2); });
    button("Make selected instance’s mods private", storageButtons, [this] { storageAction(3); });
    button("Share selected instance’s mods", storageButtons, [this] { storageAction(4); });
    storage->addRow(storageButtons);
    stop->setEnabled(false);
    return w;
}
