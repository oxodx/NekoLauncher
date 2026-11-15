#pragma once
#include "core.hpp"
#include "design.hpp"
namespace neko {
QColor blend(QColor a, QColor b, qreal amount);
class InstanceList final : public QListWidget {
  public:
    using QListWidget::QListWidget;

  protected:
    void paintEvent(QPaintEvent *) override;
};
class LandscapeCard final : public QWidget {
  public:
    using QWidget::QWidget;

  protected:
    void paintEvent(QPaintEvent *) override;
};
class InstanceDelegate final : public QStyledItemDelegate {
    std::function<void(QModelIndex)> launch;

  public:
    InstanceDelegate(QObject *parent, std::function<void(QModelIndex)> action);
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;
    void paint(QPainter *, const QStyleOptionViewItem &, const QModelIndex &) const override;
    bool editorEvent(QEvent *, QAbstractItemModel *, const QStyleOptionViewItem &,
                     const QModelIndex &) override;
};
class DiscoverDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;
    void paint(QPainter *, const QStyleOptionViewItem &, const QModelIndex &) const override;
};
} // namespace neko
