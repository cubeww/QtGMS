#include "editorstyle.h"

#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QStyleFactory>
#include <QStyleOption>

EditorStyle::EditorStyle()
    : QProxyStyle(QStyleFactory::create(QStringLiteral("Windows")))
{
}

void EditorStyle::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);
    if (qobject_cast<QPushButton *>(widget) || qobject_cast<QRadioButton *>(widget) || qobject_cast<QScrollBar *>(widget))
        widget->setAttribute(Qt::WA_Hover);
}

int EditorStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    if (metric == PM_ExclusiveIndicatorWidth || metric == PM_ExclusiveIndicatorHeight) return 12;
    if (metric == PM_RadioButtonLabelSpacing) return 3;
    if (metric == PM_ToolBarSeparatorExtent) return 8;
    if (metric == PM_ScrollBarExtent) return 17;
    if (metric == PM_ScrollBarSliderMin) return 20;
    return QProxyStyle::pixelMetric(metric, option, widget);
}

QRect EditorStyle::subElementRect(SubElement element, const QStyleOption *option, const QWidget *widget) const
{
    if (element == SE_RadioButtonIndicator || element == SE_RadioButtonContents) {
        // Use one vertical center for the indicator and text, including compact toolbars.
        const QRect bounds = option->rect;
        const QRect logical = element == SE_RadioButtonIndicator
            ? QRect(bounds.left(), bounds.top() + (bounds.height() - 12) / 2, 12, 12)
            : bounds.adjusted(15, 0, 0, 0);
        return visualRect(option->direction, bounds, logical);
    }
    return QProxyStyle::subElementRect(element, option, widget);
}

void EditorStyle::drawControl(ControlElement element, const QStyleOption *option,
                              QPainter *painter, const QWidget *widget) const
{
    const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
    if (button && element == CE_PushButtonBevel) {
        const bool enabled = button->state & State_Enabled;
        const bool pressed = button->state & (State_Sunken | State_On);
        const bool highlighted = enabled && (button->state & (State_MouseOver | State_HasFocus));
        if ((button->features & QStyleOptionButton::Flat) && !pressed && !highlighted)
            return;
        const QRect outer = button->rect;
        const QRect inner = outer.adjusted(1, 1, -1, -1);
        const QColor face = button->palette.color(QPalette::Button);
        const QColor light(112, 112, 108);
        const QColor dark(48, 48, 46);
        painter->save();
        painter->fillRect(outer, highlighted ? QColor(98, 149, 43) : QColor(20, 20, 20));
        painter->fillRect(inner, pressed ? face.darker(115) : face);
        painter->setPen(pressed ? dark : light);
        painter->drawLine(inner.topLeft(), inner.topRight());
        painter->drawLine(inner.topLeft(), inner.bottomLeft());
        painter->setPen(pressed ? light : dark);
        painter->drawLine(inner.bottomLeft(), inner.bottomRight());
        painter->drawLine(inner.topRight(), inner.bottomRight());
        painter->restore();
        return;
    }
    if (button && element == CE_PushButtonLabel && widget && widget->property("leftAligned").toBool()) {
        QStyleOptionButton label(*button);
        const int iconWidth = label.icon.isNull() ? 0 : label.iconSize.width() + 4;
        const int textWidth = label.fontMetrics.size(Qt::TextShowMnemonic, label.text).width();
        label.rect.setWidth(qMin(label.rect.width(), iconWidth + textWidth));
        QProxyStyle::drawControl(element, &label, painter, widget);
        return;
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

void EditorStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                                     QPainter *painter, const QWidget *widget) const
{
    const auto *scroll = qstyleoption_cast<const QStyleOptionSlider *>(option);
    if (control != CC_ScrollBar || !scroll) {
        QProxyStyle::drawComplexControl(control, option, painter, widget);
        return;
    }

    // Keep Qt's range, thumb geometry and hit testing; only replace painting.
    const bool vertical = scroll->orientation == Qt::Vertical;
    const bool enabled = (scroll->state & State_Enabled) && scroll->maximum > scroll->minimum;
    const QColor track(33, 33, 33), green(98, 149, 43), inactive(80, 80, 76);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setClipRect(scroll->rect, Qt::IntersectClip);
    painter->fillRect(scroll->rect, track);

    for (SubControl part : {SC_ScrollBarSubPage, SC_ScrollBarAddPage}) {
        if ((scroll->subControls & part) && (scroll->activeSubControls & part) && (scroll->state & State_Sunken))
            painter->fillRect(subControlRect(control, scroll, part, widget), QColor(80, 80, 80));
    }
    for (SubControl part : {SC_ScrollBarSubLine, SC_ScrollBarAddLine}) {
        if (!(scroll->subControls & part)) continue;
        const QRect rect = subControlRect(control, scroll, part, widget);
        if (rect.isEmpty()) continue;
        const bool active = enabled && (scroll->activeSubControls & part);
        if (active && (scroll->state & State_Sunken)) painter->fillRect(rect, QColor(56, 56, 56));
        const QPoint center = rect.center();
        bool backwards = part == SC_ScrollBarSubLine;
        if (!vertical && scroll->direction == Qt::RightToLeft) backwards = !backwards;
        const int direction = backwards ? -1 : 1;
        QPolygon arrow;
        if (vertical)
            arrow << center + QPoint(-5, -direction * 2) << center + QPoint(5, -direction * 2) << center + QPoint(0, direction * 3);
        else
            arrow << center + QPoint(-direction * 2, -5) << center + QPoint(-direction * 2, 5) << center + QPoint(direction * 3, 0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(enabled ? green : inactive);
        painter->drawPolygon(arrow);
    }
    if (scroll->subControls & SC_ScrollBarSlider) {
        const QRect thumb = subControlRect(control, scroll, SC_ScrollBarSlider, widget);
        if (!thumb.isEmpty()) {
            const bool active = enabled && (scroll->activeSubControls & SC_ScrollBarSlider)
                && (scroll->state & (State_MouseOver | State_Sunken));
            painter->fillRect(thumb, track);
            painter->setBrush(Qt::NoBrush);
            painter->setPen(active ? green : inactive);
            painter->drawRect(thumb.adjusted(0, 0, -1, -1));
            if ((vertical ? thumb.height() : thumb.width()) >= 16) {
                const QPoint center = thumb.center();
                painter->setPen(enabled ? green : inactive);
                for (int offset = -3; offset <= 3; offset += 2) {
                    if (vertical) painter->drawLine(center + QPoint(-2, offset), center + QPoint(3, offset));
                    else painter->drawLine(center + QPoint(offset, -2), center + QPoint(offset, 3));
                }
            }
        }
    }
    painter->restore();
}

void EditorStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                                QPainter *painter, const QWidget *widget) const
{
    if (element == PE_IndicatorToolBarSeparator) {
        const QRect bounds = option->rect;
        const bool horizontal = option->state & State_Horizontal;
        const int center = horizontal ? bounds.center().x() : bounds.center().y();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        // Adjacent one-pixel shadow/highlight, with three-pixel end insets.
        for (int offset = 0; offset < 2; ++offset) {
            painter->setPen(offset == 0 ? QColor(32, 32, 32) : QColor(117, 117, 117));
            if (horizontal)
                painter->drawLine(center + offset, bounds.top() + 3, center + offset, bounds.bottom() - 3);
            else
                painter->drawLine(bounds.left() + 3, center + offset, bounds.right() - 3, center + offset);
        }
        painter->restore();
        return;
    }
    if (element == PE_PanelScrollAreaCorner) {
        painter->fillRect(option->rect, QColor(33, 33, 33));
        return;
    }
    if (element == PE_IndicatorRadioButton) {
        const bool enabled = option->state & State_Enabled;
        const bool hovered = enabled && (option->state & State_MouseOver);
        painter->save();
        painter->translate(option->rect.topLeft());
        painter->scale(option->rect.width() / 12.0, option->rect.height() / 12.0);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(enabled ? QColor(hovered ? 145 : 112, hovered ? 145 : 112, hovered ? 137 : 104) : QColor(65, 65, 65), 1.0));
        painter->setBrush(QColor(20, 20, 20));
        painter->drawEllipse(QRectF(0.5, 0.5, 11.0, 11.0));
        if (option->state & State_On) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(enabled ? QColor(98, 149, 43) : QColor(112, 112, 112));
            painter->drawEllipse(QRectF(2.5, 2.5, 7.0, 7.0));
        }
        painter->restore();
        return;
    }
    if (element != PE_IndicatorCheckBox) {
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;
    }

    const bool enabled = option->state & State_Enabled;
    const bool hovered = enabled && (option->state & State_MouseOver);
    const bool pressed = enabled && (option->state & State_Sunken);
    const QColor border = !enabled ? QColor(65, 65, 65)
        : hovered ? QColor(119, 119, 111) : QColor(85, 85, 81);
    const QColor mark = enabled ? QColor(98, 149, 43) : QColor(112, 112, 112);

    // Paint both the box and mark in logical coordinates so Qt's device
    // transform scales them together, including fractional scale factors.
    painter->save();
    painter->translate(option->rect.topLeft());
    painter->scale(option->rect.width() / 13.0, option->rect.height() / 13.0);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(border, 1.0));
    painter->setBrush(pressed ? QColor(33, 33, 33) : QColor(20, 20, 20));
    painter->drawRoundedRect(QRectF(0.5, 0.5, 12.0, 12.0), 1.0, 1.0);

    if (option->state & State_NoChange) {
        painter->fillRect(QRectF(3.0, 5.0, 7.0, 3.0), mark);
    } else if (option->state & State_On) {
        QPainterPath tick;
        tick.moveTo(2.5, 6.5);
        tick.lineTo(5.0, 9.0);
        tick.lineTo(10.5, 3.0);
        painter->setPen(QPen(mark, 2.0, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(tick);
    }
    painter->restore();
}
