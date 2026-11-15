#pragma once
#include "design.hpp"
#include <QtWidgets>
namespace neko::ui {
class ElidedLabel final : public QLabel {
  public:
    using QLabel::QLabel;

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setFont(font());
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(contentsRect(), Qt::AlignVCenter | Qt::AlignLeft,
                         fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};
inline QPushButton *button(const QString &text, QLayout *layout, const std::function<void()> &action) {
    auto *b = new QPushButton(text);
    b->setCursor(Qt::PointingHandCursor);
    if (text == "+ New instance") {
        b->setText("New instance");
        b->setProperty("glyph", int(Glyph::Add));
    }
    if (text == "Import pack")
        b->setProperty("glyph", int(Glyph::Import));
    if (text == "Launch Minecraft")
        b->setProperty("glyph", int(Glyph::Play));

    layout->addWidget(b);
    QObject::connect(b, &QPushButton::clicked, b, action);
    return b;
}
inline QLabel *label(const QString &text, const char *name = "") {
    auto *l = new QLabel(text);
    l->setObjectName(name);
    l->setWordWrap(true);
    l->setMinimumWidth(0);
    l->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    return l;
}
inline QWidget *page(QVBoxLayout *&layout, const QString &title, const QString &subtitle) {
    auto *w = new QWidget;
    layout = new QVBoxLayout(w);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(16);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->addWidget(label(title, "title"));
    layout->addWidget(label(subtitle, "muted"));
    return w;
}
inline QLineEdit *entry(const QString &value, const QString &placeholder = {}) {
    auto *e = new QLineEdit(value);
    e->setPlaceholderText(placeholder);
    return e;
}
} // namespace neko::ui
