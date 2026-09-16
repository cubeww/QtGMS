#include "roomoverview.h"
#include "roomcanvas.h"

#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <cmath>

RoomOverview::RoomOverview(RoomCanvas *canvas, QWidget *parent)
    : QWidget(parent), m_canvas(canvas), m_refreshTimer(new QTimer(this))
{
    setMinimumHeight(32);
    setCursor(Qt::OpenHandCursor);
    setToolTip(tr("Click or drag to navigate the room."));
    setAccessibleName(tr("Room overview"));
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(40);
    connect(m_refreshTimer, &QTimer::timeout, this, [this] {
        if (isVisible()) rebuildPreview();
        else m_preview = QPixmap();
        update();
    });
    connect(canvas, &RoomCanvas::previewChanged, this, &RoomOverview::invalidatePreview);
    // Also follow live instance dragging and resizing before the document is committed.
    connect(canvas->scene(), &QGraphicsScene::changed, this, &RoomOverview::invalidatePreview);
    connect(canvas, &RoomCanvas::viewChanged, this, [this] { update(); });
}

QRectF RoomOverview::previewRect() const
{
    if (!m_canvas || m_canvas->roomRect().isEmpty()) return QRectF();
    const QRectF available = QRectF(contentsRect()).adjusted(3, 3, -3, -3);
    if (available.isEmpty()) return QRectF();
    QSizeF size = m_canvas->roomRect().size();
    size.scale(available.size(), Qt::KeepAspectRatio);
    return QRectF(QPointF(available.center().x() - size.width() / 2, available.top()), size);
}

void RoomOverview::invalidatePreview()
{
    if (!m_refreshTimer->isActive()) m_refreshTimer->start();
}

void RoomOverview::rebuildPreview()
{
    m_refreshTimer->stop();
    const QRectF target = previewRect();
    if (target.isEmpty()) { m_preview = QPixmap(); return; }
    const qreal ratio = devicePixelRatioF();
    // Allocate only the on-screen thumbnail, never a room-sized intermediate image.
    m_preview = QPixmap(qMax(1, int(std::ceil(target.width() * ratio))),
                        qMax(1, int(std::ceil(target.height() * ratio))));
    if (m_preview.isNull()) return;
    m_preview.setDevicePixelRatio(ratio);
    m_preview.fill(Qt::transparent);
    QPainter painter(&m_preview);
    const QRectF room = m_canvas->roomRect();
    painter.scale(target.width() / room.width(), target.height() / room.height());
    painter.translate(-room.topLeft());
    m_canvas->renderPreview(&painter, room);
}

void RoomOverview::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(56, 56, 56));
    const QRectF target = previewRect();
    if (target.isEmpty()) return;
    const qreal ratio = devicePixelRatioF();
    const QSize size(qMax(1, int(std::ceil(target.width() * ratio))),
                     qMax(1, int(std::ceil(target.height() * ratio))));
    if (m_preview.size() != size || !qFuzzyCompare(m_preview.devicePixelRatio(), ratio)) rebuildPreview();
    painter.setClipRect(target);
    painter.drawPixmap(target, m_preview, QRectF(m_preview.rect()));
    const QRectF room = m_canvas->roomRect();
    const qreal scale = target.width() / room.width();
    const QRectF visible = m_canvas->visibleRoomRect().intersected(room);
    if (!visible.isEmpty()) {
        QRectF outline(target.topLeft() + (visible.topLeft() - room.topLeft()) * scale, visible.size() * scale);
        // Keep the entire viewport outline visible at the edges of the room.
        outline = outline.intersected(target.adjusted(1, 1, -1, -1));
        if (!outline.isEmpty()) {
            painter.setPen(QPen(QColor(180, 30, 205), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(outline);
        }
    }
}

QPointF RoomOverview::roomPosition(const QPointF &position) const
{
    const QRectF target = previewRect(), room = m_canvas->roomRect();
    return room.topLeft() + (position - target.topLeft()) * (room.width() / target.width());
}

void RoomOverview::navigate(const QPointF &position)
{
    if (!m_canvas || previewRect().isEmpty()) return;
    QPointF center = roomPosition(position) + m_dragOffset;
    const QRectF room = m_canvas->roomRect(), visible = m_canvas->visibleRoomRect();
    const qreal halfWidth = qMin(room.width(), visible.width()) / 2;
    const qreal halfHeight = qMin(room.height(), visible.height()) / 2;
    center.setX(qBound(room.left() + halfWidth, center.x(), room.right() - halfWidth));
    center.setY(qBound(room.top() + halfHeight, center.y(), room.bottom() - halfHeight));
    m_canvas->centerViewportOn(center);
}

void RoomOverview::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_canvas || !previewRect().contains(event->localPos())) {
        QWidget::mousePressEvent(event); return;
    }
    m_canvas->finishInteraction();
    const QPointF point = roomPosition(event->localPos());
    const QRectF visible = m_canvas->visibleRoomRect();
    m_dragOffset = visible.contains(point) ? visible.center() - point : QPointF();
    m_dragging = true;
    setCursor(Qt::ClosedHandCursor);
    navigate(event->localPos());
    event->accept();
}

void RoomOverview::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        navigate(event->localPos()); event->accept();
    } else QWidget::mouseMoveEvent(event);
}

void RoomOverview::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        navigate(event->localPos());
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
    } else QWidget::mouseReleaseEvent(event);
}
