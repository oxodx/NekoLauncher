#include "ui/command_palette.hpp"
#include "instance_services.hpp"
#include "ui/widgets.hpp"
#include <algorithm>
namespace neko::ui {
CommandPalette::CommandPalette(QList<PaletteCommand> entries, QWidget *parent)
    : QDialog(parent), commands(std::move(entries)) {
    setObjectName("commandPalette");
    setWindowTitle("Quick switcher");
    resize(620, 480);
    setMinimumSize(420, 340);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    layout->addWidget(label("Where would you like to go?", "sectionTitle"));
    query = entry({}, "Search instances, pages and actions…");
    query->setObjectName("paletteSearch");
    query->installEventFilter(this);
    layout->addWidget(query);
    results = new QListWidget;
    results->setObjectName("paletteResults");
    results->setTextElideMode(Qt::ElideRight);
    results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    results->setSpacing(4);
    layout->addWidget(results, 1);
    layout->addWidget(label("↑ ↓ to move    ·    Enter to run    ·    Esc to close", "micro"));
    connect(query, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(query, &QLineEdit::returnPressed, this, &CommandPalette::choose);
    connect(results, &QListWidget::itemActivated, this, [this] { choose(); });
    refresh();
    query->setFocus();
}
void CommandPalette::refresh() {
    results->clear();
    QList<QPair<int, int>> scores;
    for (int i = 0; i < commands.size(); ++i) {
        int score = commandScore(query->text(), commands[i].title, commands[i].keywords);
        if (score >= 0)
            scores.append({score + commands[i].priority, i});
    }
    std::stable_sort(scores.begin(), scores.end(), [](auto a, auto b) { return a.first > b.first; });
    for (const auto &entry : scores) {
        const auto &command = commands[entry.second];
        auto *item = new QListWidgetItem(results);
        item->setData(Qt::UserRole, entry.second);
        item->setToolTip(command.title + "\n" + command.subtitle);
        item->setSizeHint(QSize(0, qMax(66, QFontMetrics(font()).height() * 2 + 24)));
        auto *row = new QWidget;
        row->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *layout = new QVBoxLayout(row);
        layout->setContentsMargins(14, 8, 14, 8);
        layout->setSpacing(2);
        auto *title = new ElidedLabel(command.title);
        auto *subtitle = new ElidedLabel(command.subtitle);
        subtitle->setObjectName("micro");
        layout->addWidget(title);
        layout->addWidget(subtitle);
        results->setItemWidget(item, row);
    }
    if (results->count())
        results->setCurrentRow(0);
}
bool CommandPalette::eventFilter(QObject *object, QEvent *event) {
    if (object == query && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up) {
            const auto delta = key->key() == Qt::Key_Down ? 1 : -1;
            if (results->count())
                results->setCurrentRow((results->currentRow() + delta + results->count()) % results->count());
            return true;
        }
    }
    return QDialog::eventFilter(object, event);
}
void CommandPalette::choose() {
    if (!results->currentItem())
        return;
    chosen = results->currentItem()->data(Qt::UserRole).toInt();
    accept();
}
std::function<void()> CommandPalette::selectedAction() const {
    return chosen >= 0 && chosen < commands.size() ? commands[chosen].execute : std::function<void()>{};
}
} // namespace neko::ui
