#include "cards.hpp"
#include "instance_services.hpp"
namespace neko {
QColor blend(QColor a, QColor b, qreal amount) {
    return QColor::fromRgbF(a.redF() * (1 - amount) + b.redF() * amount,
                            a.greenF() * (1 - amount) + b.greenF() * amount,
                            a.blueF() * (1 - amount) + b.blueF() * amount);
}
namespace {
void text(QPainter &p, QRectF rect, QString value, QColor ink, int size, bool bold = false) {
    value.replace('\n', ' ').replace('\r', ' ');
    auto font = qApp->font();
    font.setPixelSize(size);
    font.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    p.setFont(font);
    p.setPen(ink);
    p.drawText(rect, Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(font).elidedText(value, Qt::ElideRight, qMax(0, int(rect.width()))));
}
void pill(QPainter &p, QRectF rect, QString value, QColor accent, QColor base, int size = 10) {
    p.setPen(Qt::NoPen);
    p.setBrush(blend(base, accent, .13));
    p.drawRoundedRect(rect, 6, 6);
    text(p, rect.adjusted(8, 0, -6, 0), value, accent, size, true);
}
QString played(const Json &data) {
    if (data["playtimeSeconds"].toDouble() > 0)
        return formatDuration(qint64(data["playtimeSeconds"].toDouble())) + " played";
    if (!data["lastPlayed"].toDouble())
        return "Not played yet";
    auto date = QDateTime::fromSecsSinceEpoch(qint64(data["lastPlayed"].toDouble())).date();
    const auto days = date.daysTo(QDate::currentDate());
    return days == 0 ? "Played today" : days == 1 ? "Played yesterday" : "Played " + date.toString("d MMM");
}
QRectF playTarget(QRectF r, bool grid) {
    return grid ? QRectF(r.right() - 46, r.bottom() - 43, 30, 30)
                : QRectF(r.right() - 52, r.center().y() - 17, 34, 34);
}
void playButton(QPainter &p, QRectF r, QColor accent, QColor base, bool active) {
    p.setPen(Qt::NoPen);
    p.setBrush(active ? accent : blend(base, accent, .14));
    p.drawEllipse(r);
    glyph(Glyph::Play, active ? base : accent).paint(&p, r.adjusted(9, 9, -8, -9).toRect());
}
} // namespace
void InstanceList::paintEvent(QPaintEvent *event) {
    QListWidget::paintEvent(event);
    for (int i = 0; i < count(); ++i)
        if (!item(i)->isHidden())
            return;
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);
    auto r = viewport()->rect().adjusted(12, 12, -12, -12);
    auto muted = palette().color(QPalette::PlaceholderText), accent = palette().color(QPalette::Highlight);
    const auto center = r.center();
    p.setPen(Qt::NoPen);
    p.setBrush(blend(palette().color(QPalette::Window), accent, .07));
    p.drawRoundedRect(QRectF(center.x() - 28, center.y() - 68, 56, 56), 18, 18);
    glyph(count() ? Glyph::Search : Glyph::Mods, accent)
        .paint(&p, QRect(center.x() - 13, center.y() - 53, 26, 26));
    auto font = qApp->font();
    font.setPixelSize(16);
    font.setBold(true);
    p.setFont(font);
    p.setPen(palette().color(QPalette::Text));
    p.drawText(QRect(r.x(), center.y() + 2, r.width(), 28), Qt::AlignCenter,
               count() ? "No matching instances" : "Your next adventure starts here");
    font.setPixelSize(12);
    font.setBold(false);
    p.setFont(font);
    p.setPen(muted);
    p.drawText(QRect(r.x(), center.y() + 34, r.width(), 28), Qt::AlignCenter,
               count() ? "Try a different search or clear your filters."
                       : "Create an instance or bring your favorite modpack.");
}
void LandscapeCard::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath mask;
    mask.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 18, 18);
    p.setClipPath(mask);
    const auto pal = qApp->palette();
    const auto accent = pal.color(QPalette::Highlight);
    const auto base =
        pal.color(QPalette::Window).lightness() > 128 ? QColor("#18181b") : pal.color(QPalette::Base);
    p.fillRect(rect(), base);
    const auto cover = property("cover").value<QPixmap>();
    if (!cover.isNull())
        drawCover(p, rect(), cover);
    else {
        QLinearGradient backdrop(0, height(), width(), 0);
        backdrop.setColorAt(0, QColor("#15151e"));
        backdrop.setColorAt(1, blend(QColor("#181822"), accent, .23));
        p.fillRect(rect(), backdrop);
        QRadialGradient glow(QPointF(width() * .88, height() * .12), width() * .65);
        auto tint = accent;
        tint.setAlpha(38);
        glow.setColorAt(0, tint);
        glow.setColorAt(1, Qt::transparent);
        p.fillRect(rect(), glow);
        p.setPen(QPen(QColor(255, 255, 255, 9), 1));
        p.setBrush(Qt::NoBrush);
        for (int radius : {100, 165, 230})
            p.drawEllipse(QPointF(width() - 20, height() / 2.), radius, radius);
        glyph(Glyph::Mods, blend(base, accent, .45))
            .paint(&p, QRect(width() - 118, height() / 2 - 27, 54, 54));
    }
    QLinearGradient scrim(0, 0, width(), 0);
    scrim.setColorAt(0, QColor(18, 18, 29, 255));
    scrim.setColorAt(.28, QColor(18, 18, 29, 245));
    scrim.setColorAt(.6, QColor(18, 18, 29, 135));
    scrim.setColorAt(1, QColor(18, 18, 29, 8));
    if (!cover.isNull())
        p.fillRect(rect(), scrim);
    QLinearGradient floor(0, 0, 0, height());
    floor.setColorAt(0, Qt::transparent);
    floor.setColorAt(1, QColor(12, 12, 20, 110));
    if (!cover.isNull())
        p.fillRect(rect(), floor);
    QLinearGradient border(0, 0, width(), height());
    border.setColorAt(0, QColor(accent.red(), accent.green(), accent.blue(), 100));
    border.setColorAt(1, QColor(255, 255, 255, 16));
    p.setPen(QPen(QBrush(border), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 18, 18);
}
InstanceDelegate::InstanceDelegate(QObject *parent, std::function<void(QModelIndex)> action)
    : QStyledItemDelegate(parent), launch(std::move(action)) {}
QSize InstanceDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const {
    auto *list = qobject_cast<QListWidget *>(parent());
    return list && list->viewMode() == QListView::IconMode
               ? QSize(232, qMax(230, option.fontMetrics.height() * 3 + 156))
               : QSize(240, 92);
}
bool InstanceDelegate::editorEvent(QEvent *event, QAbstractItemModel *, const QStyleOptionViewItem &option,
                                   const QModelIndex &index) {
    if (event->type() != QEvent::MouseButtonRelease)
        return false;
    auto *mouse = static_cast<QMouseEvent *>(event);
    auto *list = qobject_cast<QListWidget *>(parent());
    if (mouse->button() == Qt::LeftButton && playTarget(QRectF(option.rect).adjusted(2, 2, -2, -2),
                                                        list && list->viewMode() == QListView::IconMode)
                                                 .contains(mouse->position())) {
        if (launch)
            launch(index);
        return true;
    }
    return false;
}
void InstanceDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const {
    auto &p = *painter;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const auto r = QRectF(option.rect).adjusted(2, 2, -2, -2);
    const auto pal = qApp->palette();
    auto accent = pal.color(QPalette::Highlight), base = pal.color(QPalette::Base);
    const auto selected = option.state.testFlag(QStyle::State_Selected),
               hover = option.state.testFlag(QStyle::State_MouseOver);
    const auto data = index.data(Qt::UserRole).toJsonObject();
    const auto custom = index.data(Qt::UserRole + 2).value<QPixmap>();
    const QColor chosen(data["color"].toString());
    auto artAccent = chosen.isValid() ? chosen : accent;
    p.setPen(QPen(selected ? blend(base, accent, .8)
                  : hover  ? blend(base, accent, .4)
                           : pal.color(QPalette::Mid),
                  1));
    p.setBrush(selected ? blend(base, accent, .055) : hover ? blend(base, accent, .025) : base);
    p.drawRoundedRect(r, 8, 8);
    auto *list = qobject_cast<QListWidget *>(parent());
    const bool grid = list && list->viewMode() == QListView::IconMode;
    const int fontSize = qMax(14, QFontInfo(option.font).pixelSize());
    if (grid) {
        QRectF art(r.x() + 8, r.y() + 8, r.width() - 16, 126);
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(art, 9, 9);
        p.setClipPath(clip);
        if (!custom.isNull())
            drawCover(p, art, custom);
        else {
            QLinearGradient fill(art.topLeft(), art.bottomRight());
            fill.setColorAt(0, blend(base, artAccent, .13));
            fill.setColorAt(1, blend(base, artAccent, .035));
            p.fillRect(art, fill);
            p.setPen(QPen(QColor(artAccent.red(), artAccent.green(), artAccent.blue(), 16), 1));
            for (int x = int(art.x()) - 100; x < art.right(); x += 28)
                p.drawLine(QPointF(x, art.top()), QPointF(x + 126, art.bottom()));
            glyph(data["loader"].toString() == "Vanilla" ? Glyph::Home : Glyph::Mods, artAccent)
                .paint(&p, QRect(int(art.center().x()) - 22, int(art.center().y()) - 22, 44, 44));
        }
        QLinearGradient shade(art.topLeft(), art.bottomLeft());
        shade.setColorAt(0, Qt::transparent);
        shade.setColorAt(1, QColor(0, 0, 0, 60));
        p.fillRect(art, shade);
        if (hover) {
            p.fillRect(art, QColor(10, 10, 18, 25));
        }
        p.restore();
        if (data["favorite"].toBool()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(20, 20, 30, 200));
            p.drawRoundedRect(QRectF(art.right() - 29, art.top() + 7, 22, 22), 7, 7);
            auto starFont = qApp->font();
            starFont.setPixelSize(13);
            p.setFont(starFont);
            p.setPen(QColor("#f9e2af"));
            p.drawText(QRectF(art.right() - 29, art.top() + 7, 22, 22), Qt::AlignCenter, "★");
        }
        text(p, QRectF(r.x() + 14, art.bottom() + 9, r.width() - 28, 26), data["name"].toString(),
             pal.color(QPalette::Text), fontSize, true);
        auto loader = data["loader"].toString("Vanilla");
        auto font = qApp->font();
        font.setPixelSize(10);
        const auto chipWidth = QFontMetrics(font).horizontalAdvance(loader) + 18;
        pill(p, QRectF(r.x() + 14, art.bottom() + 41, chipWidth, 20), loader, pal.color(QPalette::Text),
             base);
        text(p, QRectF(r.x() + chipWidth + 22, art.bottom() + 40, r.width() - chipWidth - 36, 22),
             data["version"].toString(), pal.color(QPalette::PlaceholderText), 11);
        text(p, QRectF(r.x() + 14, r.bottom() - 35, r.width() - 65, 23),
             data["pendingDownloads"].toArray().isEmpty() ? played(data) : "Downloads needed",
             pal.color(QPalette::PlaceholderText), 10);
    } else {
        QRectF icon(r.x() + 14, r.y() + 14, 64, 64);
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(icon, 12, 12);
        p.setClipPath(clip);
        p.fillRect(icon, blend(base, artAccent, .13));
        if (!custom.isNull())
            drawCover(p, icon, custom);
        else
            glyph(Glyph::Mods, artAccent).paint(&p, icon.adjusted(17, 17, -17, -17).toRect());
        p.restore();
        text(p, QRectF(icon.right() + 16, r.y() + 15, r.width() - 158, 28), data["name"].toString(),
             pal.color(QPalette::Text), fontSize, true);
        text(p, QRectF(icon.right() + 16, r.y() + 46, r.width() - 158, 24),
             data["version"].toString() + "  ·  " + data["loader"].toString("Vanilla"),
             pal.color(QPalette::PlaceholderText), qMax(12, fontSize - 2));
    }
    playButton(p, playTarget(r, grid), accent, base, hover || selected);
    p.restore();
}
QSize DiscoverDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const {
    return {240, qMax(174, option.fontMetrics.height() * 4 + 68)};
}
void DiscoverDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const {
    auto &p = *painter;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    auto r = QRectF(option.rect).adjusted(2, 2, -2, -2);
    const auto pal = qApp->palette();
    const auto selected = option.state.testFlag(QStyle::State_Selected),
               hover = option.state.testFlag(QStyle::State_MouseOver);
    const auto base = pal.color(QPalette::Base), accent = pal.color(QPalette::Highlight);
    p.setPen(QPen(selected ? accent : pal.color(QPalette::Mid), 1));
    p.setBrush(hover || selected ? blend(base, accent, .05) : base);
    p.drawRoundedRect(r, 14, 14);
    if (selected) {
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawRoundedRect(QRectF(r.left(), r.top() + 18, 3, r.height() - 36), 1.5, 1.5);
    }
    const auto data = index.data(Qt::UserRole).toJsonObject();
    const bool cf = index.data(Qt::UserRole + 1).toBool();
    const auto image = index.data(Qt::UserRole + 2).value<QPixmap>();
    QRectF icon(r.x() + 20, r.center().y() - 38, 76, 76);
    p.save();
    QPainterPath clip;
    clip.addRoundedRect(icon, 18, 18);
    p.setClipPath(clip);
    p.fillRect(icon, blend(base, accent, .09));
    if (!image.isNull())
        drawCover(p, icon, image);
    else
        glyph(Glyph::Mods, accent).paint(&p, icon.adjusted(21, 21, -21, -21).toRect());
    p.restore();
    const auto x = icon.right() + 20, width = qMax(0., r.right() - x - 20);
    const int fs = qMax(14, QFontInfo(option.font).pixelSize());
    const int line = qMax(23, option.fontMetrics.height() + 3);
    text(p, QRectF(x, r.top() + 12, width, line), (cf ? data["name"] : data["title"]).toString(),
         pal.color(QPalette::Text), fs + 1, true);
    QString author = data["author"].toString();
    if (cf) {
        QStringList authors;
        for (const auto &entry : data["authors"].toArray())
            authors << entry.toObject()["name"].toString();
        author = authors.join(", ");
    }
    text(p, QRectF(x, r.top() + 12 + line, width, line), "by " + author, pal.color(QPalette::PlaceholderText),
         qMax(11, fs - 2));
    text(p, QRectF(x, r.top() + 12 + line * 2, width, line),
         (cf ? data["summary"] : data["description"]).toString(), pal.color(QPalette::Text),
         qMax(12, fs - 1));
    auto downloads = (cf ? data["downloadCount"] : data["downloads"]).toDouble();
    QString count = downloads >= 1000000 ? QString::number(downloads / 1000000, 'f', 1) + "M"
                    : downloads >= 1000  ? QString::number(downloads / 1000, 'f', 1) + "K"
                                         : QString::number(qint64(downloads));
    QStringList tags;
    for (const auto &entry : data["categories"].toArray()) {
        const auto tag = cf ? entry.toObject()["name"].toString() : entry.toString();
        if (!tag.isEmpty() && tags.size() < 3)
            tags << tag;
    }
    text(p, QRectF(x, r.bottom() - line - 10, width, line),
         count + " downloads" + (tags.isEmpty() ? QString{} : "   ·   " + tags.join(" · ")), accent,
         qMax(11, fs - 2));
    p.restore();
}
} // namespace neko
