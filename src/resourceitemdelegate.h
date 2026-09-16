#ifndef QTGMS_RESOURCEITEMDELEGATE_H
#define QTGMS_RESOURCEITEMDELEGATE_H

#include <QStyledItemDelegate>
#include <QPersistentModelIndex>

class QTreeWidget;

class ResourceItemDelegate : public QStyledItemDelegate
{
public:
    explicit ResourceItemDelegate(QTreeWidget *tree);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void initStyleOption(QStyleOptionViewItem *option,
                         const QModelIndex &index) const override;

private:
    QModelIndex linkIndexAt(const QPoint &position) const;
    void setHoveredIndex(const QModelIndex &index);

    QTreeWidget *m_tree;
    QPersistentModelIndex m_hoveredIndex;
};

#endif
