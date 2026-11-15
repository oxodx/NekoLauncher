#pragma once
#include <QtWidgets>
namespace neko {
enum class Glyph {
    Home,
    Explore,
    Mods,
    Account,
    Settings,
    Console,
    Play,
    Add,
    Import,
    Folder,
    Edit,
    More,
    Search,
    Cat,
    ChevronDown,
    ChevronUp,
    Check
};
QIcon glyph(Glyph id, QColor color);
void drawCover(QPainter &painter, const QRectF &target, const QPixmap &image, qreal focus = .5);
class NavigationButton final : public QPushButton {
    Glyph symbol;
    QVariantAnimation animation;
    qreal hover = 0;

  public:
    NavigationButton(QString title, Glyph symbol, QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
};
} // namespace neko
