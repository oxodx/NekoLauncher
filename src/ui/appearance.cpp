#include "appearance.hpp"
#include "cards.hpp"
#include "ui/widgets.hpp"
#include "window.hpp"
using namespace neko;
using namespace neko::ui;

void Window::applyTheme() {
    const auto name = settings.value("theme", "Shadcn Dark").toString();
    const auto colors = appearance(name);
    const bool light = colors.light;
    const auto bg = colors.base, panel = colors.mantle,
               border = blend(QColor(colors.base), QColor(colors.overlay), .6).name(), text = colors.text,
               muted = colors.subtext, accent = colors.accent;
    auto size = settings.value("fontSize", 14).toInt();
    QFont font;
    font.setFamilies({"Inter", "Segoe UI", "SF Pro Text", "Noto Sans", "sans-serif"});
    font.setStyleHint(QFont::SansSerif);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setPixelSize(size);
    qApp->setFont(font);
    setFont(font);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(bg));
    palette.setColor(QPalette::Base, blend(QColor(colors.base), QColor(colors.surface), .34));
    palette.setColor(QPalette::AlternateBase, blend(QColor(colors.base), QColor(colors.surface), .55));
    palette.setColor(QPalette::Text, QColor(text));
    palette.setColor(QPalette::WindowText, QColor(text));
    palette.setColor(QPalette::Highlight, QColor(accent));
    palette.setColor(QPalette::Mid, QColor(border));
    palette.setColor(QPalette::PlaceholderText, QColor(muted));
    palette.setColor(QPalette::Button, QColor(panel));
    palette.setColor(QPalette::ButtonText, QColor(text));
    palette.setColor(QPalette::Highlight, QColor(accent));
    palette.setColor(QPalette::HighlightedText, QColor(colors.accentText));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(muted));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(muted));
    qApp->setPalette(palette);
    const auto card = blend(QColor(colors.base), QColor(colors.surface), .34).name();
    const auto iconDirectory = root + "/cache/interface-v3/" + accent.mid(1);
    QDir().mkpath(iconDirectory);
    auto iconUrl = [&](const QString &name, Glyph id, const QColor &ink) {
        auto file = iconDirectory + "/" + name + ".png";
        if (!QFile::exists(file))
            glyph(id, ink).pixmap(16, 16).save(file);
        return QString("url(\"%1\")").arg(file.replace("\\", "/").replace("\"", "\\\""));
    };
    const auto down = iconUrl("down", Glyph::ChevronDown, QColor(muted));
    const auto up = iconUrl("up", Glyph::ChevronUp, QColor(muted));
    const auto check = iconUrl("check", Glyph::Check, QColor(colors.accentText));
    const auto controls =
        QString("QComboBox::down-arrow{image:%1;width:14px;height:14px;} "
                "QSpinBox::up-arrow{image:%2;width:12px;height:12px;} "
                "QSpinBox::down-arrow{image:%1;width:12px;height:12px;} "
                "QCheckBox::indicator{background:%4;border:1px solid %5;border-radius:4px;} "
                "QCheckBox::indicator:checked{background:%6;border-color:%6;image:%3;}")
            .arg(down, up, check, panel, border, accent);

    setStyleSheet(
        QString(R"QSS(
QMainWindow { background:%1; }
QWidget { color:%2; font-family:Inter; font-size:%8px; }
QLabel { background:transparent; }
#workspace { background:%1; }
#pages, #libraryPage, QScrollArea, QScrollArea > QWidget > QWidget { background:transparent; }
#sidebar { background:%3; border-right:1px solid %4; }
#sidebarBrand { background:transparent; }
#sidebarWordmark { font-size:22px; font-weight:650; letter-spacing:-.5px; }
#navSection { font-size:9px; font-weight:600; letter-spacing:1.5px; color:%5; padding:15px 12px 7px; }
#navVersion { font-size:9px; color:%5; padding:8px 12px; }
#topbar { background:transparent; border-bottom:1px solid %4; }
#wordmark { color:%5; font-size:12px; }
#editionBadge { color:%5; font-size:11px; }
#eyebrow { font-size:10px; color:%5; letter-spacing:1px; }
#libraryTitle, #title { font-size:28px; font-weight:650; letter-spacing:-.6px; }
#sectionTitle { font-size:17px; font-weight:600; }
#muted { color:%5; }
#micro { color:%5; font-size:10px; }
#heroText, #heroActions, #card { background:transparent; border:0; }
#heroText #hero { font-size:34px; font-weight:700; color:#f5f4ff; letter-spacing:-.6px; }
#heroKicker { color:#cba6f7; font-size:10px; letter-spacing:2px; font-weight:600; }
#heroText #muted { color:#bac2de; font-size:13px; }
#heroActions QPushButton { font-size:13px; }
QPushButton { background:transparent; border:1px solid %4; border-radius:6px; padding:9px 13px; font-weight:500; }
QPushButton:hover { background:%4; border-color:%5; }
QPushButton:pressed { border-color:%6; }
QPushButton:disabled { color:%5; }
QPushButton#primary { background:%6; color:%7; border:1px solid %6; padding:10px 17px; font-weight:650; }
QPushButton#primary:disabled { background:%9; border-color:%4; color:%5; }
QPushButton#primary:hover { background:%6; border-color:%5; }
QPushButton#secondaryAccent { background:%9; border-color:%4; color:%6; }
QPushButton#secondaryAccent:hover { border-color:%6; }
QPushButton#heroIcon { background:rgba(255,255,255,8); border:1px solid rgba(255,255,255,24); padding:8px; }
QPushButton#heroIcon:hover { background:rgba(255,255,255,20); border-color:rgba(255,255,255,65); }
QPushButton#heroIcon::menu-indicator { image:none; }
QPushButton#accountButton { background:%9; border:1px solid %4; border-radius:6px; padding:7px 12px; font-size:12px; }
QPushButton#nav { background:transparent; border:0; padding:0; }
QPushButton#nav:checked { background:transparent; }
QLineEdit, QComboBox, QSpinBox, QPlainTextEdit { background:%1; border:1px solid %4; border-radius:6px; padding:10px; selection-background-color:%6; selection-color:%7; }
QPlainTextEdit { font-family:"JetBrains Mono", "DejaVu Sans Mono", monospace; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QPlainTextEdit:focus { border-color:%6; }
QLineEdit[searchField="true"] { padding:11px 12px; background:%9; }
QComboBox { padding-right:26px; }
QComboBox::drop-down { border:0; width:24px; }
QComboBox QAbstractItemView { background:%9; color:%2; selection-background-color:%4; selection-color:%2; border:1px solid %4; padding:5px; }
QListWidget { background:transparent; border:0; outline:0; }
QListWidget::item { background:%9; border:1px solid %4; border-radius:8px; padding:14px; }
QListWidget::item:selected { background:%9; border-color:%5; color:%2; }
QListWidget#paletteResults::item { padding:0; }
QGroupBox { background:%9; border:1px solid %4; border-radius:8px; padding:24px 20px 20px; margin-top:12px; }
QGroupBox::title { subcontrol-origin:margin; left:20px; padding:0 8px; font-weight:600; }
#accountName { font-size:23px; font-weight:650; }
#profileAvatar { background:%4; border:1px solid %4; border-radius:20px; }
QTabWidget::pane { border:1px solid %4; border-radius:12px; background:%1; }
QTabBar::tab { background:transparent; color:%5; padding:13px 16px; border-bottom:2px solid transparent; }
QTabBar::tab:selected { color:%6; border-bottom:2px solid %6; }
QTabBar::tab:hover { color:%2; }
QTabBar#contentTabs { background:%9; border:1px solid %4; border-radius:7px; padding:4px; }
QTabBar#contentTabs::tab { border:0; border-radius:4px; padding:7px 18px; margin:0 2px; color:%5; }
QTabBar#contentTabs::tab:selected { background:%4; color:%2; }
QComboBox#modTarget { border-color:%5; padding:10px; }

QCheckBox { spacing:8px; }
QCheckBox::indicator { width:16px; height:16px; }
QScrollBar:vertical { background:transparent; width:6px; margin:3px 0; }
QScrollBar::handle:vertical { background:%4; border-radius:3px; min-height:32px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:none; }
QMenu { background:%9; border:1px solid %4; padding:6px; border-radius:8px; }
QMenu::item { padding:10px 18px; border-radius:5px; }
QMenu::item:selected { background:%4; color:%6; }
QProgressBar { border:0; background:%4; border-radius:3px; height:5px; }
QProgressBar::chunk { background:%6; border-radius:3px; }
#footer { background:transparent; border-top:1px solid %4; }
#footer QLabel { font-size:11px; color:%5; }
#logsLink { background:transparent; border:0; padding:3px 6px; font-size:11px; color:%5; }
#logsLink:hover { color:%6; }
#commandButton { background:transparent; border:1px solid %4; border-radius:8px; padding:7px 10px; font-size:11px; color:%5; }
#commandButton:hover { color:%6; border-color:%6; }
#paletteSearch { padding:14px; font-size:16px; }
QTreeWidget { background:%9; border:1px solid %4; border-radius:8px; padding:6px; }
QHeaderView::section { background:%3; color:%5; padding:9px; border:0; font-size:12px; font-weight:600; }
QTreeWidget::item { padding:8px; }

QToolTip { background:%9; color:%2; border:1px solid %4; padding:6px; }
)QSS")
            .arg(bg, text, panel, border, muted, accent, colors.accentText, QString::number(size), card) +
        controls + settings.value("customStyle").toString());
    for (auto *button : navigation->buttons())
        button->setProperty("reduceMotion", settings.value("reduceMotion", false).toBool());
    for (auto *b : findChildren<QPushButton *>())
        if (b->property("glyph").isValid()) {
            const auto ink = b->objectName() == "primary"    ? QColor(colors.accentText)
                             : b->objectName() == "heroIcon" ? QColor("#e8efff")
                                                             : QColor(text);
            b->setIcon(glyph(Glyph(b->property("glyph").toInt()), ink));
            b->setIconSize(QSize(18, 18));
        }
    if (auto *mark = sidebar->findChild<QLabel *>("brandMark")) {
        QPixmap logo(40, 40);
        logo.fill(Qt::transparent);
        QPainter p(&logo);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(accent));
        p.drawRoundedRect(QRectF(0, 0, 40, 40), 8, 8);
        glyph(Glyph::Cat, QColor(colors.accentText)).paint(&p, QRect(9, 9, 22, 22));
        mark->setPixmap(logo);
    }
    if (auto *avatar = findChild<QLabel *>("profileAvatar"))
        avatar->setPixmap(glyph(Glyph::Account, QColor(accent)).pixmap(30, 30));
    for (auto *field : findChildren<QLineEdit *>())
        if (field->property("searchField").toBool()) {
            auto actions = field->actions();
            if (actions.isEmpty())
                field->addAction(glyph(Glyph::Search, QColor(muted)), QLineEdit::LeadingPosition);
            else
                actions.first()->setIcon(glyph(Glyph::Search, QColor(muted)));
        }
    if (accountButton)
        accountButton->setIcon(glyph(Glyph::Account, QColor(muted)));
    QResizeEvent layoutEvent(this->size(), this->size());
    resizeEvent(&layoutEvent);
}
