#include "pathcanvas.h"
#include "roomcanvas.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <cmath>

PathCanvas::PathCanvas(PathDocument *document, QWidget *parent) : QWidget(parent), m_document(document)
{
    setMinimumSize(120, 120); setFocusPolicy(Qt::StrongFocus); setMouseTracking(true); setAttribute(Qt::WA_OpaquePaintEvent);
    connect(document, &PathDocument::changed, this, &PathCanvas::synchronize);
    connect(document, &PathDocument::selectionChanged, this, [this] { update(); }); synchronize();
}
void PathCanvas::setRoomPreview(RoomCanvas *room) { m_room = room; update(); }
QPointF PathCanvas::toWorld(const QPointF &pixel) const { return m_origin + pixel / m_zoom; }
QRectF PathCanvas::visibleArea() const { return QRectF(m_origin, QSizeF(width() / m_zoom, height() / m_zoom)); }
void PathCanvas::panBy(const QPointF &distance) { m_origin += distance / m_zoom; update(); emit viewChanged(); }
void PathCanvas::centerPath()
{
    finishInteraction(); QPointF center;
    if (!m_points.isEmpty()) {
        qreal left = m_points.first().position.x(), right = left, top = m_points.first().position.y(), bottom = top;
        for (const auto &point : m_points) { left = qMin(left, point.position.x()); right = qMax(right, point.position.x()); top = qMin(top, point.position.y()); bottom = qMax(bottom, point.position.y()); }
        center = QPointF((left + right) / 2, (top + bottom) / 2);
    }
    m_origin = center - QPointF(width(), height()) / (2 * m_zoom); update(); emit viewChanged();
}
QPointF PathCanvas::snapped(const QPointF &point, Qt::KeyboardModifiers modifiers) const
{
    QPointF result = point;
    if (!(modifiers & Qt::AltModifier)) { const auto &state = m_document->state(); result = QPointF(std::floor(point.x() / state.snapX + 0.5) * state.snapX, std::floor(point.y() / state.snapY + 0.5) * state.snapY); }
    return QPointF(qBound(-1e9, result.x(), 1e9), qBound(-1e9, result.y(), 1e9));
}
int PathCanvas::hitPoint(const QPointF &pixel) const
{
    int found = -1; qreal distance = 64;
    for (int i = 0; i < m_points.size(); ++i) { const QPointF delta = (m_points.at(i).position - m_origin) * m_zoom - pixel; const qreal d = QPointF::dotProduct(delta, delta); if (d <= distance) { distance = d; found = i; } }
    return found;
}
void PathCanvas::synchronize() { m_dragIndex = -1; m_newPoint = false; m_points = m_document->state().points; rebuildCurve(); update(); }
static void appendCurvePiece(QPainterPath &curve, bool &started, const QPointF &a, const QPointF &control, const QPointF &b, int depth)
{
    if (!depth) return;
    const QPointF middle = (a + control * 2 + b) / 4;
    const QPointF left = control - a, right = control - b;
    if (QPointF::dotProduct(left, left) > 16) appendCurvePiece(curve, started, a, (a + control) / 2, middle, depth - 1);
    if (started) curve.lineTo(middle); else { curve.moveTo(middle); started = true; }
    if (QPointF::dotProduct(right, right) > 16) appendCurvePiece(curve, started, middle, (control + b) / 2, b, depth - 1);
}
void PathCanvas::rebuildCurve()
{
    m_curve = QPainterPath(); if (m_points.isEmpty()) return;
    const auto &state = m_document->state(); const int count = m_points.size();
    if (!state.smooth) {
        m_curve.moveTo(m_points.first().position); for (int i = 1; i < count; ++i) m_curve.lineTo(m_points.at(i).position);
        if (state.closed && count > 1) m_curve.closeSubpath(); return;
    }
    // Match GameMaker's midpoint subdivision, including its four-pixel
    // threshold and the distinct starting point of a closed smooth path.
    bool started = !state.closed;
    if (started) m_curve.moveTo(m_points.first().position);
    const int pieces = state.closed ? count : count - 2;
    for (int i = 0; i < pieces; ++i) {
        const QPointF a = m_points.at(i).position, b = m_points.at((i + 1) % count).position, c = m_points.at((i + 2) % count).position;
        appendCurvePiece(m_curve, started, (a + b) / 2, b, (b + c) / 2, state.precision);
    }
    if (state.closed) m_curve.closeSubpath(); else m_curve.lineTo(m_points.last().position);
}
void PathCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this); painter.fillRect(rect(), QColor(120, 120, 120)); const QRectF area = visibleArea();
    painter.save(); painter.scale(m_zoom, m_zoom); painter.translate(-m_origin);
    if (m_room) m_room->renderPreview(&painter, area);
    QPen gridPen(QColor(49, 49, 49)); gridPen.setCosmetic(true); painter.setPen(gridPen);
    if (m_grid) {
        const auto &state = m_document->state(); const qreal dx = state.snapX * qMax(1.0, std::ceil(6 / (state.snapX * m_zoom))), dy = state.snapY * qMax(1.0, std::ceil(6 / (state.snapY * m_zoom)));
        // Integer loop counters avoid stalled floating additions far from zero.
        const qreal left = std::ceil(area.left() / dx) * dx, top = std::ceil(area.top() / dy) * dy;
        for (int i = 0, count = int(area.width() / dx) + 1; i <= count; ++i) painter.drawLine(QPointF(left + i * dx, area.top()), QPointF(left + i * dx, area.bottom()));
        for (int i = 0, count = int(area.height() / dy) + 1; i <= count; ++i) painter.drawLine(QPointF(area.left(), top + i * dy), QPointF(area.right(), top + i * dy));
    }
    QPen pathPen(QColor(255, 240, 0)); pathPen.setCosmetic(true); painter.setPen(pathPen); painter.setBrush(Qt::NoBrush); painter.drawPath(m_curve); painter.restore();
    painter.setPen(Qt::black);
    const int selected = m_dragIndex >= 0 ? m_dragIndex : m_document->selectedPoint();
    for (int i = 0; i < m_points.size(); ++i) { const QPointF pixel = (m_points.at(i).position - m_origin) * m_zoom; if (!QRectF(rect()).adjusted(-5, -5, 5, 5).contains(pixel)) continue; painter.setBrush(i == selected ? Qt::red : Qt::blue); painter.drawEllipse(pixel, 3, 3); }
    if (!m_points.isEmpty()) { const QPointF start = (m_curve.pointAtPercent(0) - m_origin) * m_zoom; painter.setBrush(QColor(100, 210, 0)); painter.drawRect(QRectF(start - QPointF(3, 3), QSizeF(6, 6))); }
}
void PathCanvas::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); emit viewChanged(); }
void PathCanvas::mousePressEvent(QMouseEvent *event)
{
    setFocus(); finishInteraction(); const QPointF world = toWorld(event->localPos());
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_space)) { m_panButton = event->button(); m_panAnchor = world; setCursor(Qt::ClosedHandCursor); return; }
    const int point = hitPoint(event->localPos());
    if (event->button() == Qt::RightButton) { if (point >= 0) { m_document->selectPoint(point); deleteSelected(); } return; }
    if (event->button() != Qt::LeftButton) return;
    if (point >= 0) { m_document->selectPoint(point); m_dragIndex = point; m_dragStart = m_points.at(point).position; m_dragOffset = m_dragStart - world; }
    else { PathPoint added; added.position = snapped(world, event->modifiers()); if (!m_points.isEmpty()) added.speed = m_points.last().speed; m_points.append(added); m_dragIndex = m_points.size() - 1; m_newPoint = true; m_dragStart = added.position; m_dragOffset = QPointF(); rebuildCurve(); }
    update();
}
void PathCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panButton != Qt::NoButton) {
        if (event->buttons() & m_panButton) { m_origin = m_panAnchor - event->localPos() / m_zoom; update(); emit viewChanged(); }
        else finishInteraction();
    } else if (m_dragIndex >= 0) {
        if (event->buttons() & Qt::LeftButton) { m_points[m_dragIndex].position = snapped(toWorld(event->localPos()) + m_dragOffset, event->modifiers()); rebuildCurve(); update(); }
        else finishInteraction();
    } else setCursor(hitPoint(event->localPos()) >= 0 ? Qt::SizeAllCursor : Qt::CrossCursor);
    emit cursorMoved(toWorld(event->localPos()));
}
void PathCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == m_panButton || (event->button() == Qt::LeftButton && m_dragIndex >= 0)) finishInteraction();
}
void PathCanvas::finishInteraction(bool commit)
{
    m_panButton = Qt::NoButton; unsetCursor(); if (m_dragIndex < 0) return;
    const int selected = m_dragIndex; const bool added = m_newPoint; m_dragIndex = -1; m_newPoint = false;
    if (commit) { auto state = m_document->state(); state.points = m_points; m_document->edit(state, added ? tr("Add path point") : tr("Move path point"), selected); }
    else synchronize();
}
void PathCanvas::wheelEvent(QWheelEvent *event)
{
    finishInteraction(); const QPointF anchor = toWorld(event->posF()); m_zoom = qBound(0.125, m_zoom * std::pow(1.2, event->angleDelta().y() / 120.0), 8.0);
    m_origin = anchor - event->posF() / m_zoom; update(); emit viewChanged(); event->accept();
}
void PathCanvas::deleteSelected()
{
    finishInteraction(); const int selected = m_document->selectedPoint(); if (selected < 0) return;
    auto state = m_document->state(); state.points.removeAt(selected); m_document->edit(state, tr("Delete path point"), qMin(selected, state.points.size() - 1));
}
void PathCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space) { m_space = true; event->accept(); return; }
    if (event->key() == Qt::Key_Escape) { finishInteraction(false); event->accept(); return; }
    if (event->key() == Qt::Key_Delete) { deleteSelected(); event->accept(); return; }
    QWidget::keyPressEvent(event);
}
void PathCanvas::keyReleaseEvent(QKeyEvent *event) { if (event->key() == Qt::Key_Space) { m_space = false; event->accept(); return; } QWidget::keyReleaseEvent(event); }
void PathCanvas::focusOutEvent(QFocusEvent *event) { finishInteraction(); m_space = false; QWidget::focusOutEvent(event); }
