#include "resourceitemdelegate.h"

#include <QCursor>
#include <QMouseEvent>
#include <QStyleOptionViewItem>
#include <QTreeWidget>

ResourceItemDelegate::ResourceItemDelegate(QTreeWidget *tree)
    : QStyledItemDelegate(tree), m_tree(tree)
{
    m_tree->viewport()->installEventFilter(this);
}

QModelIndex ResourceItemDelegate::linkIndexAt(const QPoint &position) const
{
    if (!m_tree->viewport()->rect().contains(position))
        return QModelIndex();
    const QModelIndex index = m_tree->indexAt(position);
    if (!index.isValid())
        return QModelIndex();

    QStyleOptionViewItem option;
    option.initFrom(m_tree);
    option.widget = m_tree;
    option.rect = m_tree->visualRect(index);
    option.decorationSize = m_tree->iconSize();
    QStyledItemDelegate::initStyleOption(&option, index);
    option.showDecorationSelected = false;

    // Use the style's icon/text placement, but trim the text rectangle to
    // the label's width instead of accepting the whole tree column.
    QStyle *style = m_tree->style();
    const QRect iconRect = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, m_tree);
    const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &option, m_tree);
    const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, &option, m_tree);
    const int textWidth = qMin(textRect.width(), option.fontMetrics.width(option.text) + 2 * margin);
    const QRect labelRect = QStyle::alignedRect(option.direction, option.displayAlignment,
        QSize(textWidth, textRect.height()), textRect);
    const QRect linkRect = iconRect.united(labelRect).intersected(option.rect);
    return linkRect.contains(position) ? index : QModelIndex();
}

void ResourceItemDelegate::setHoveredIndex(const QModelIndex &index)
{
    if (index.isValid())
        m_tree->viewport()->setCursor(Qt::PointingHandCursor);
    else
        m_tree->viewport()->unsetCursor();
    if (m_hoveredIndex == index)
        return;
    const QRect previousRect = m_tree->visualRect(m_hoveredIndex);
    m_hoveredIndex = index;
    m_tree->viewport()->update(previousRect);
    m_tree->viewport()->update(m_tree->visualRect(index));
}

bool ResourceItemDelegate::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tree->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            setHoveredIndex(linkIndexAt(static_cast<QMouseEvent *>(event)->pos()));
        } else if (event->type() == QEvent::Leave) {
            setHoveredIndex(QModelIndex());
        } else if (event->type() == QEvent::Paint) {
            // Re-evaluate after scrolling, expanding or loading a project,
            // even if the pointer has not moved. All positions are logical.
            setHoveredIndex(m_tree->viewport()->underMouse()
                ? linkIndexAt(m_tree->viewport()->mapFromGlobal(QCursor::pos())) : QModelIndex());
        }
    }
    return QStyledItemDelegate::eventFilter(watched, event);
}

void ResourceItemDelegate::initStyleOption(QStyleOptionViewItem *option,
                                         const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);

    // QTreeView sets this independently of the stylesheet's row-selection
    // hint. Let the Windows style size the highlight and focus to the label.
    option->showDecorationSelected = false;

    option->state &= ~QStyle::State_MouseOver;
    if (index == m_hoveredIndex) {
        option->state |= QStyle::State_MouseOver;
        option->font.setUnderline(true);
        option->palette.setColor(QPalette::Text, QColor(51, 153, 255));
    }

    // The Windows style otherwise generates a blue-tinted selected icon,
    // even when selection is restricted to the text.
    if (option->state & QStyle::State_Selected) {
        for (QIcon::State state : {QIcon::Off, QIcon::On}) {
            option->icon.addPixmap(option->icon.pixmap(option->decorationSize,
                                                      QIcon::Normal, state),
                                   QIcon::Selected, state);
        }
    }
}
