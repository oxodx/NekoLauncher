#include "design.hpp"
namespace neko {
QIcon glyph(Glyph id, QColor color) {
    QIcon icon;
    for (int size : {20, 24, 32, 48, 64}) {
        QPixmap pix(size, size);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 24., size / 24.);
        p.setPen(QPen(color, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        switch (id) {
        case Glyph::ChevronDown:
            path.moveTo(6, 9);
            path.lineTo(12, 15);
            path.lineTo(18, 9);
            break;
        case Glyph::ChevronUp:
            path.moveTo(6, 15);
            path.lineTo(12, 9);
            path.lineTo(18, 15);
            break;
        case Glyph::Check:
            path.moveTo(5, 12);
            path.lineTo(10, 17);
            path.lineTo(19, 7);
            break;
        case Glyph::Home:
            path.moveTo(3, 11);
            path.lineTo(12, 3);
            path.lineTo(21, 11);
            path.moveTo(5, 10);
            path.lineTo(5, 21);
            path.lineTo(10, 21);
            path.lineTo(10, 14);
            path.lineTo(14, 14);
            path.lineTo(14, 21);
            path.lineTo(19, 21);
            path.lineTo(19, 10);
            break;
        case Glyph::Explore:
            p.drawEllipse(QRectF(3, 3, 18, 18));
            path.moveTo(15.5, 8.5);
            path.lineTo(13.5, 13.5);
            path.lineTo(8.5, 15.5);
            path.lineTo(10.5, 10.5);
            path.closeSubpath();
            break;
        case Glyph::Mods:
            path.moveTo(12, 2.5);
            path.lineTo(21, 7.5);
            path.lineTo(21, 17);
            path.lineTo(12, 22);
            path.lineTo(3, 17);
            path.lineTo(3, 7.5);
            path.closeSubpath();
            path.moveTo(3, 7.5);
            path.lineTo(12, 12.5);
            path.lineTo(21, 7.5);
            path.moveTo(12, 12.5);
            path.lineTo(12, 22);
            path.moveTo(7.5, 5);
            path.lineTo(16.5, 10);
            break;
        case Glyph::Account:
            p.drawEllipse(QRectF(8, 3, 8, 8));
            path.moveTo(4, 21);
            path.cubicTo(4, 12, 20, 12, 20, 21);
            break;
        case Glyph::Settings:
            p.drawEllipse(QRectF(8, 8, 8, 8));
            for (int i = 0; i < 8; ++i) {
                p.save();
                p.translate(12, 12);
                p.rotate(i * 45);
                p.drawLine(QPointF(0, 8), QPointF(0, 10));
                p.restore();
            }
            p.drawEllipse(QRectF(4.5, 4.5, 15, 15));
            break;
        case Glyph::Console:
            p.drawRoundedRect(QRectF(2.5, 4, 19, 16), 3, 3);
            path.moveTo(6, 8);
            path.lineTo(10, 12);
            path.lineTo(6, 16);
            path.moveTo(13, 16);
            path.lineTo(17, 16);
            break;
        case Glyph::Play:
            path.moveTo(7, 4);
            path.lineTo(20, 12);
            path.lineTo(7, 20);
            path.closeSubpath();
            p.setBrush(color);
            break;
        case Glyph::Add:
            path.moveTo(12, 5);
            path.lineTo(12, 19);
            path.moveTo(5, 12);
            path.lineTo(19, 12);
            break;
        case Glyph::Import:
            path.moveTo(12, 3);
            path.lineTo(12, 15);
            path.moveTo(7, 10);
            path.lineTo(12, 15);
            path.lineTo(17, 10);
            path.moveTo(4, 15);
            path.lineTo(4, 21);
            path.lineTo(20, 21);
            path.lineTo(20, 15);
            break;
        case Glyph::Folder:
            path.moveTo(3, 6);
            path.lineTo(10, 6);
            path.lineTo(12, 9);
            path.lineTo(21, 9);
            path.lineTo(20, 20);
            path.lineTo(3, 20);
            path.closeSubpath();
            break;
        case Glyph::Edit:
            path.moveTo(5, 16);
            path.lineTo(16, 5);
            path.lineTo(20, 9);
            path.lineTo(9, 20);
            path.lineTo(4, 21);
            path.closeSubpath();
            path.moveTo(14, 7);
            path.lineTo(18, 11);
            break;
        case Glyph::More:
            for (int x : {5, 12, 19}) {
                p.setBrush(color);
                p.drawEllipse(QPointF(x, 12), 1.5, 1.5);
            }
            break;
        case Glyph::Search:
            p.drawEllipse(QRectF(3, 3, 12, 12));
            p.drawLine(QPointF(13, 13), QPointF(21, 21));
            break;
        case Glyph::Cat:
            path.moveTo(4, 18);
            path.lineTo(3, 4);
            path.lineTo(9, 8);
            path.quadTo(12, 6, 15, 8);
            path.lineTo(21, 4);
            path.lineTo(20, 18);
            path.quadTo(12, 24, 4, 18);
            p.drawPoint(8, 14);
            p.drawPoint(16, 14);
            path.moveTo(10, 18);
            path.quadTo(12, 20, 14, 18);
            break;
        }
        p.drawPath(path);
        p.end();
        icon.addPixmap(pix);
    }
    return icon;
}
void drawCover(QPainter &p, const QRectF &target, const QPixmap &image, qreal focus) {
    if (image.isNull())
        return;
    const qreal scale = qMax(target.width() / image.width(), target.height() / image.height());
    QSizeF source(target.width() / scale, target.height() / scale);
    QRectF crop((image.width() - source.width()) * focus, (image.height() - source.height()) * .5,
                source.width(), source.height());
    p.drawPixmap(target, image, crop);
}
void BackdropWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window));
    p.setRenderHint(QPainter::Antialiasing);
    auto accent = palette().color(QPalette::Highlight);
    QRadialGradient upper(QPointF(width() * .78, -height() * .08), width() * .72);
    auto glow = accent;
    glow.setAlpha(palette().color(QPalette::Window).lightness() > 128 ? 13 : 20);
    upper.setColorAt(0, glow);
    upper.setColorAt(.52, QColor(glow.red(), glow.green(), glow.blue(), glow.alpha() / 3));
    upper.setColorAt(1, Qt::transparent);
    p.fillRect(rect(), upper);
    QRadialGradient lower(QPointF(width() * .12, height() * 1.08), width() * .55);
    const QColor cyan("#7dd3fc");
    auto secondary = QColor::fromRgbF(accent.redF() * .6 + cyan.redF() * .4,
                                      accent.greenF() * .6 + cyan.greenF() * .4,
                                      accent.blueF() * .6 + cyan.blueF() * .4);
    secondary.setAlpha(palette().color(QPalette::Window).lightness() > 128 ? 8 : 12);
    lower.setColorAt(0, secondary);
    lower.setColorAt(1, Qt::transparent);
    p.fillRect(rect(), lower);
}
NavigationButton::NavigationButton(QString title, Glyph id, QWidget *parent)
    : QPushButton(title, parent), symbol(id) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(46);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip(title);
    animation.setDuration(140);
    connect(&animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        hover = v.toReal();
        update();
    });
}
void NavigationButton::enterEvent(QEnterEvent *e) {
    animation.stop();
    if (property("reduceMotion").toBool()) {
        hover = 1.;
        update();
    } else {
        animation.setStartValue(hover);
        animation.setEndValue(1.);
        animation.start();
    }
    QPushButton::enterEvent(e);
}
void NavigationButton::leaveEvent(QEvent *e) {
    animation.stop();
    if (property("reduceMotion").toBool()) {
        hover = 0.;
        update();
    } else {
        animation.setStartValue(hover);
        animation.setEndValue(0.);
        animation.start();
    }
    QPushButton::leaveEvent(e);
}
void NavigationButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto pal = qApp->palette();
    auto accent = pal.color(QPalette::Highlight),
         text = pal.color(isChecked() ? QPalette::Text : QPalette::PlaceholderText);
    auto wash = accent;
    wash.setAlpha(isChecked() ? 31 : int(hover * 17));
    p.setPen(Qt::NoPen);
    p.setBrush(wash);
    p.drawRoundedRect(QRectF(rect()).adjusted(0, 0, -1, -1), 11, 11);
    const bool expanded = width() > 100;
    if (isChecked()) {
        p.setBrush(accent);
        p.drawRoundedRect(QRectF(0, 10, 3, height() - 20), 1.5, 1.5);
        auto iconWash = accent;
        iconWash.setAlpha(24);
        p.setBrush(iconWash);
        p.drawRoundedRect(expanded ? QRectF(8, 7, 28, 28)
                                   : QRectF((width() - 28) / 2., 2, 28, 28),
                          8, 8);
    }
    glyph(symbol, isChecked() ? accent : text)
        .paint(&p, expanded ? QRect(12, 10, 20, 20) : QRect((width() - 20) / 2, 6, 20, 20));
    auto font = qApp->font();
    font.setPixelSize(expanded ? qMax(13, QFontInfo(qApp->font()).pixelSize()) : 9);
    font.setWeight(isChecked() ? QFont::DemiBold : QFont::Normal);
    p.setFont(font);
    p.setPen(text);
    p.drawText(expanded ? QRect(46, 0, width() - 52, height()) : QRect(0, 29, width(), 16),
               expanded ? Qt::AlignVCenter | Qt::AlignLeft : Qt::AlignCenter, this->text());
    if (hasFocus()) {
        auto c = accent;
        c.setAlpha(120);
        p.setPen(QPen(c, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(2, 2, -3, -3), 10, 10);
    }
}
} // namespace neko
