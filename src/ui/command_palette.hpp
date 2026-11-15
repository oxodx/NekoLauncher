#pragma once
#include <QtWidgets>
#include <functional>
namespace neko::ui {
struct PaletteCommand {
    QString title;
    QString subtitle;
    QString keywords;
    std::function<void()> execute;
    int priority = 0;
};
class CommandPalette final : public QDialog {
  public:
    CommandPalette(QList<PaletteCommand> commands, QWidget *parent = nullptr);
    std::function<void()> selectedAction() const;

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    QList<PaletteCommand> commands;
    QLineEdit *query;
    QListWidget *results;
    int chosen = -1;
    void refresh();
    void choose();
};
} // namespace neko::ui
