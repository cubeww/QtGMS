#include "imagecanvas.h"
#include "imageoperations.h"
#include <QApplication>
#include <QBitArray>
#include <QClipboard>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QtMath>

class ImageEditCommand : public QUndoCommand
{
public:
    ImageEditCommand(ImageCanvas *canvas, const QImage &before, const QImage &after,
        const QRegion &oldSelection, const QRegion &newSelection, const QString &text)
        : QUndoCommand(text), m_canvas(canvas), m_before(before), m_after(after),
          m_oldSelection(oldSelection), m_newSelection(newSelection) {}
    void undo() override { m_canvas->applyImage(m_before, m_oldSelection); }
    void redo() override { m_canvas->applyImage(m_after, m_newSelection); }
private:
    ImageCanvas *m_canvas;
    QImage m_before, m_after;
    QRegion m_oldSelection, m_newSelection;
};

ImageCanvas::ImageCanvas(QWidget *parent) : QWidget(parent), m_undoStack(this)
{
    setMouseTracking(true); setFocusPolicy(Qt::StrongFocus); setCursor(Qt::CrossCursor);
    m_undoStack.setUndoLimit(100);
    m_sprayTimer.setInterval(40);
    connect(&m_sprayTimer, &QTimer::timeout, this, [this] {
        if (m_drawing && !m_movingSelection) { paintStroke(m_last); update(); emit imageChanged(); }
    });
}
void ImageCanvas::applyImage(const QImage &image, const QRegion &selection)
{
    m_image = image; m_selection = selection.intersected(QRegion(m_image.rect()));
    m_floatingBase = QImage(); m_floatingImage = QImage();
    m_hasFloatingText = false;
    setFixedSize(qMax(1, qCeil(m_image.width() * m_zoom)), qMax(1, qCeil(m_image.height() * m_zoom)));
    update(); emit imageChanged();
}
void ImageCanvas::setImage(const QImage &image)
{
    cancelGesture(); m_history->clear();
    applyImage(image.convertToFormat(QImage::Format_ARGB32), QRegion());
}
void ImageCanvas::replaceImage(const QImage &image, const QString &description)
{
    finishGesture();
    const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
    if (!converted.isNull() && converted != m_image)
        m_history->push(new ImageEditCommand(this, m_image, converted, m_selection, QRegion(), description));
}
QRegion ImageCanvas::editRegion() const { return m_selection.isEmpty() ? QRegion(m_image.rect()) : m_selection; }
QImage ImageCanvas::selectionImage(const QImage &image, const QRegion &region) const
{
    const QRect bounds = region.boundingRect();
    if (bounds.isEmpty()) return QImage();
    QImage result(bounds.size(), QImage::Format_ARGB32);
    if (result.isNull()) return result;
    result.fill(Qt::transparent);
    QPainter painter(&result); painter.translate(-bounds.topLeft()); painter.setClipRegion(region); painter.drawImage(0, 0, image);
    return result;
}
QImage ImageCanvas::editingImage() const { return selectionImage(m_image, editRegion()); }
void ImageCanvas::replaceSelection(const QImage &image, const QString &description)
{
    finishGesture();
    if (image.isNull()) return;
    if (m_selection.isEmpty()) { replaceImage(image, description); return; }
    QImage result = m_image;
    QPainter painter(&result); painter.setClipRegion(m_selection);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(m_selection.boundingRect().topLeft(), image); painter.end();
    if (result != m_image) m_history->push(new ImageEditCommand(this, m_image, result, m_selection, m_selection, description));
}
void ImageCanvas::setEditable(bool editable) { m_editable = editable; }
void ImageCanvas::setTool(Tool tool)
{
    finishGesture(); m_tool = tool; emit toolChanged();
}
void ImageCanvas::setZoom(qreal zoom)
{
    m_zoom = qBound(0.125, zoom, 32.0);
    setFixedSize(qMax(1, qCeil(m_image.width() * m_zoom)), qMax(1, qCeil(m_image.height() * m_zoom)));
    update(); emit zoomChanged();
}
void ImageCanvas::setGridVisible(bool visible) { m_gridVisible = visible; update(); }
void ImageCanvas::setGrid(const QSize &size, const QPoint &offset, const QColor &color)
{ m_gridSize = size.expandedTo(QSize(1, 1)); m_gridOffset = offset; m_gridColor = color; update(); }
void ImageCanvas::setTransparencyBackground(const QColor &first, const QColor &second, int size)
{ m_checkerFirst = first; m_checkerSecond = second; m_checkerSize = qMax(1, size); update(); }
void ImageCanvas::setCrosshair(const QPoint &position, bool visible)
{ m_crosshair = position; m_crosshairVisible = visible; update(); }
void ImageCanvas::setOverlay(const QImage &image) { m_overlay = image; update(); }
void ImageCanvas::setOnionImages(const QList<QImage> &images, int opacity)
{ m_onionImages = images; m_onionOpacity = opacity; update(); }
QPoint ImageCanvas::imagePosition(const QPoint &position) const
{ return QPoint(qFloor(position.x() / m_zoom), qFloor(position.y() / m_zoom)); }
bool ImageCanvas::selectionTool() const
{ return m_tool == Tool::Selection || m_tool == Tool::Wand || m_tool == Tool::BrushSelection; }

void ImageCanvas::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    QPixmap checker(m_checkerSize * 2, m_checkerSize * 2); checker.fill(m_checkerFirst);
    { QPainter tile(&checker); tile.fillRect(0, 0, m_checkerSize, m_checkerSize, m_checkerSecond);
      tile.fillRect(m_checkerSize, m_checkerSize, m_checkerSize, m_checkerSize, m_checkerSecond); }
    painter.fillRect(event->rect(), QBrush(checker));
    painter.save(); painter.scale(m_zoom, m_zoom); painter.setOpacity(m_onionOpacity / 255.0);
    for (const QImage &image : m_onionImages) painter.drawImage(0, 0, image);
    painter.setOpacity(1); painter.drawImage(0, 0, m_image);
    if (!m_overlay.isNull()) painter.drawImage(m_image.rect(), m_overlay);
    painter.restore();
    if (m_gridVisible && m_zoom >= 4) {
        painter.setPen(m_gridColor);
        const QRect visible = event->rect();
        const int firstX = qMax(0, qFloor((visible.left() / m_zoom - m_gridOffset.x()) / m_gridSize.width()));
        const int firstY = qMax(0, qFloor((visible.top() / m_zoom - m_gridOffset.y()) / m_gridSize.height()));
        for (int x = firstX * m_gridSize.width() + m_gridOffset.x(); x <= qMin(m_image.width(), qCeil(visible.right() / m_zoom)); x += m_gridSize.width())
            painter.drawLine(QPointF(x * m_zoom, visible.top()), QPointF(x * m_zoom, visible.bottom()));
        for (int y = firstY * m_gridSize.height() + m_gridOffset.y(); y <= qMin(m_image.height(), qCeil(visible.bottom() / m_zoom)); y += m_gridSize.height())
            painter.drawLine(QPointF(visible.left(), y * m_zoom), QPointF(visible.right(), y * m_zoom));
    }
    if (!m_selection.isEmpty()) {
        QPainterPath boundary; boundary.addRegion(m_selection);
        QTransform transform; transform.scale(m_zoom, m_zoom); boundary = transform.map(boundary);
        painter.setPen(QPen(Qt::white, 1)); painter.drawPath(boundary);
        painter.setPen(QPen(Qt::black, 1, Qt::DashLine)); painter.drawPath(boundary);
    }
    if (m_crosshairVisible) {
        const QPointF center(m_crosshair.x() * m_zoom + 0.5, m_crosshair.y() * m_zoom + 0.5);
        for (int width : {3, 1}) {
            painter.setPen(QPen(width == 3 ? Qt::black : Qt::white, width));
            painter.drawLine(center - QPointF(6, 0), center + QPointF(6, 0));
            painter.drawLine(center - QPointF(0, 6), center + QPointF(0, 6));
        }
    }
}
QPoint ImageCanvas::constrainedPosition(QPoint point) const
{
    if (!(m_modifiers & Qt::ShiftModifier) || selectionTool()) return point;
    const QPoint origin = m_tool == Tool::Polygon && !m_polygon.isEmpty() ? m_polygon.last() : m_start;
    const QPoint delta = point - origin;
    if (m_tool == Tool::Rectangle || m_tool == Tool::Ellipse || m_tool == Tool::RoundedRectangle) {
        const int size = qMax(qAbs(delta.x()), qAbs(delta.y()));
        return origin + QPoint(delta.x() < 0 ? -size : size, delta.y() < 0 ? -size : size);
    }
    if (m_tool == Tool::Line || m_tool == Tool::Polygon) {
        if (qAbs(delta.x()) > qAbs(delta.y()) * 2) point.setY(origin.y());
        else if (qAbs(delta.y()) > qAbs(delta.x()) * 2) point.setX(origin.x());
        else { const int distance = qMax(qAbs(delta.x()), qAbs(delta.y())); point = origin + QPoint(delta.x() < 0 ? -distance : distance, delta.y() < 0 ? -distance : distance); }
    } else if (qAbs(delta.x()) >= qAbs(delta.y())) point.setY(origin.y());
    else point.setX(origin.x());
    return point;
}
void ImageCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) { m_panning = true; m_panPosition = event->globalPos(); setCursor(Qt::ClosedHandCursor); return; }
    const QPoint position = imagePosition(event->pos());
    if (!m_image.rect().contains(position) || (event->button() != Qt::LeftButton && event->button() != Qt::RightButton)) return;
    setFocus();
    if (!m_editable) { emit positionPicked(position); return; }
    if (m_tool == Tool::Picker || (!selectionTool() && m_tool != Tool::Text && (event->modifiers() & Qt::ControlModifier))) {
        emit colorPicked(QColor::fromRgba(m_image.pixel(position)), event->button() == Qt::RightButton); return;
    }
    if (m_hasFloatingText && m_selection.contains(position) && event->button() == Qt::RightButton) {
        emit textRequested(m_floatingPosition, false, true); return;
    }
    if (m_tool == Tool::Text) { emit textRequested(position, event->button() == Qt::RightButton, false); return; }
    m_modifiers = event->modifiers();
    if (m_tool == Tool::Polygon && !m_polygon.isEmpty()) {
        m_polygon.append(constrainedPosition(position)); drawPolygon(m_polygon.last()); update(); return;
    }
    m_before = m_image; m_selectionBefore = m_selection; m_gestureSelection = QRegion();
    m_start = m_last = position; m_drawButton = event->button();
    m_drawColor = event->button() == Qt::RightButton ? m_background : m_foreground;
    m_drawColor.setAlpha(m_drawColor.alpha() * m_opacity / 255);
    m_strokeErase = m_tool == Tool::Eraser || (m_rightErase && event->button() == Qt::RightButton);
    m_drawing = true;
    m_copySelection = event->button() == Qt::RightButton;
    m_movingSelection = selectionTool() && m_selection.contains(position)
        && !(m_modifiers & (Qt::ShiftModifier | Qt::ControlModifier));
    if (m_movingSelection) {
        if (m_floatingBase.isNull() || m_copySelection) {
            m_floatingImage = selectionImage(m_before, m_selection);
            m_floatingBase = m_before;
            m_floatingPosition = m_selection.boundingRect().topLeft();
            m_floatingRegion = m_selection;
            if (!m_copySelection) {
                QPainter painter(&m_floatingBase); painter.setClipRegion(m_selection);
                painter.setCompositionMode(QPainter::CompositionMode_Clear); painter.fillRect(m_image.rect(), Qt::transparent);
            }
        }
    } else {
        m_floatingBase = QImage(); m_floatingImage = QImage();
        m_hasFloatingText = false;
        if (m_tool == Tool::Polygon) { m_polygon.append(position); drawPolygon(position); }
        else if (m_tool == Tool::Fill || m_tool == Tool::ReplaceColor || m_tool == Tool::Wand) {
            const QRegion region = matchingRegion(position, m_tool != Tool::ReplaceColor);
            if (m_tool == Tool::Wand) changeSelection(region);
            else paintRegion(region, m_drawColor);
            commitGesture(m_tool == Tool::Wand ? tr("Select by color") : tr("Fill"));
        } else drawTo(position);
        if (m_tool == Tool::Airbrush) m_sprayTimer.start();
    }
    update();
}
void ImageCanvas::changeSelection(const QRegion &region)
{
    if (m_modifiers & Qt::ControlModifier) m_selection = m_selectionBefore.subtracted(region);
    else if (m_modifiers & Qt::ShiftModifier) m_selection = m_selectionBefore.united(region);
    else m_selection = region;
    m_selection &= QRegion(m_image.rect());
}
void ImageCanvas::paintStroke(const QPoint &position)
{
    QPainter painter(&m_image); painter.setClipRegion(editRegion());
    painter.setCompositionMode(m_strokeErase ? QPainter::CompositionMode_DestinationOut
        : m_blend ? QPainter::CompositionMode_SourceOver : QPainter::CompositionMode_Source);
    const QPoint delta = position - m_last;
    const int steps = qMax(1, qMax(qAbs(delta.x()), qAbs(delta.y())));
    const bool soft = m_tool == Tool::Airbrush || m_strokeErase;
    const qreal radius = m_brushSize / 2.0;
    for (int i = delta.isNull() ? 0 : 1; i <= (delta.isNull() ? 0 : steps); ++i) {
        const QPoint point(m_last.x() + qRound(delta.x() * double(i) / steps), m_last.y() + qRound(delta.y() * double(i) / steps));
        const QPointF center(point.x() + 0.5, point.y() + 0.5);
        QColor color = m_drawColor;
        if (m_strokeErase) color = QColor(255, 255, 255, m_opacity);
        painter.setPen(Qt::NoPen);
        if (soft && m_brushSize > 1 && m_hardness < 100) {
            QRadialGradient brush(center, radius);
            brush.setColorAt(0, color); brush.setColorAt(qMin(0.99, m_hardness / 100.0), color);
            color.setAlpha(0); brush.setColorAt(1, color); painter.setBrush(brush);
        } else painter.setBrush(color);
        if (m_brushSize == 1) painter.fillRect(QRect(point, QSize(1, 1)), color);
        else painter.drawEllipse(QRectF(center.x() - radius, center.y() - radius, m_brushSize, m_brushSize));
    }
}
static void arrowHead(QPainter &painter, const QPointF &tip, const QPointF &tail, int width)
{
    const QLineF line(tail, tip);
    if (line.length() == 0) return;
    const QPointF direction = (tip - tail) / line.length();
    const QPointF normal(-direction.y(), direction.x());
    const double size = qMax(5, width * 3);
    painter.drawLine(tip, tip - direction * size + normal * size / 2);
    painter.drawLine(tip, tip - direction * size - normal * size / 2);
}
void ImageCanvas::drawPolygon(const QPoint &position)
{
    m_image = m_before;
    QPainter painter(&m_image); painter.setClipRegion(editRegion());
    painter.setRenderHint(QPainter::Antialiasing, m_antialiasing);
    painter.setCompositionMode(m_strokeErase ? QPainter::CompositionMode_DestinationOut
        : m_blend ? QPainter::CompositionMode_SourceOver : QPainter::CompositionMode_Source);
    painter.setPen(m_shapeStyle == ShapeStyle::Filled ? QPen(Qt::NoPen) : QPen(m_drawColor, m_brushSize));
    QColor fill = m_drawColor;
    if (m_shapeStyle == ShapeStyle::OutlineAndFill) {
        fill = m_drawButton == Qt::RightButton ? m_foreground : m_background;
        fill.setAlpha(fill.alpha() * m_opacity / 255);
    }
    painter.setBrush(m_shapeStyle == ShapeStyle::Outline ? QBrush(Qt::NoBrush) : QBrush(fill));
    QPolygon polygon = m_polygon; if (polygon.isEmpty() || polygon.last() != position) polygon.append(position);
    painter.drawPolygon(polygon);
}
void ImageCanvas::drawTo(QPoint position)
{
    position = constrainedPosition(position);
    if (m_tool == Tool::Polygon) { drawPolygon(position); return; }
    if (selectionTool()) {
        if (m_movingSelection) {
            m_image = m_floatingBase;
            const QPoint destination = m_floatingPosition + position - m_start;
            QPainter painter(&m_image); painter.drawImage(destination, m_floatingImage);
            m_selection = m_floatingRegion.translated(position - m_start).intersected(QRegion(m_image.rect()));
        } else if (m_tool == Tool::Selection) changeSelection(QRegion(QRect(m_start, position).normalized()));
        else if (m_tool == Tool::BrushSelection) {
            const QPoint delta = position - m_last;
            const int steps = qMax(1, qMax(qAbs(delta.x()), qAbs(delta.y())));
            for (int i = 0; i <= steps; ++i) {
                const QPoint point = m_last + QPoint(qRound(delta.x() * double(i) / steps), qRound(delta.y() * double(i) / steps));
                m_gestureSelection |= QRegion(QRect(point - QPoint(m_brushSize / 2, m_brushSize / 2), QSize(m_brushSize, m_brushSize)), QRegion::Ellipse);
            }
            changeSelection(m_gestureSelection);
        }
        m_last = position; return;
    }
    const bool shape = m_tool == Tool::Line || m_tool == Tool::Rectangle || m_tool == Tool::Ellipse || m_tool == Tool::RoundedRectangle;
    if (!shape) { paintStroke(position); m_last = position; return; }
    m_image = m_before;
    QPainter painter(&m_image); painter.setClipRegion(editRegion());
    painter.setRenderHint(QPainter::Antialiasing, m_antialiasing);
    painter.setCompositionMode(m_strokeErase ? QPainter::CompositionMode_DestinationOut
        : m_blend ? QPainter::CompositionMode_SourceOver : QPainter::CompositionMode_Source);
    painter.setPen(m_shapeStyle == ShapeStyle::Filled && m_tool != Tool::Line ? QPen(Qt::NoPen)
        : QPen(m_drawColor, m_brushSize, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    QColor fill = m_drawColor;
    if (m_shapeStyle == ShapeStyle::OutlineAndFill) { fill = m_drawButton == Qt::RightButton ? m_foreground : m_background; fill.setAlpha(fill.alpha() * m_opacity / 255); }
    painter.setBrush(m_shapeStyle == ShapeStyle::Outline ? QBrush(Qt::NoBrush) : QBrush(fill));
    const QRect box = QRect(m_start, position).normalized();
    if (m_tool == Tool::Rectangle) painter.drawRect(box.adjusted(0, 0, -1, -1));
    else if (m_tool == Tool::RoundedRectangle) painter.drawRoundedRect(box, qMin(8, box.width() / 4), qMin(8, box.height() / 4));
    else if (m_tool == Tool::Ellipse) painter.drawEllipse(box);
    else {
        painter.drawLine(m_start, position);
        if (m_arrowMode == 1 || m_arrowMode == 3) arrowHead(painter, position, m_start, m_brushSize);
        if (m_arrowMode == 2 || m_arrowMode == 3) arrowHead(painter, m_start, position, m_brushSize);
    }
    m_last = position;
}
QRegion ImageCanvas::matchingRegion(const QPoint &position, bool contiguous) const
{
    const QRegion allowed = m_tool == Tool::Wand ? QRegion(m_image.rect()) : editRegion();
    if (!allowed.contains(position)) return QRegion();
    const QRect bounds = allowed.boundingRect();
    const QRgb target = m_image.pixel(position);
    QVector<QRect> spans;
    auto matches = [&](int x, int y) {
        return allowed.contains(QPoint(x, y)) && ImageOperations::matches(m_image.pixel(x, y), target, m_tolerance, m_colorOnly);
    };
    if (!contiguous) {
        for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
            int start = -1;
            for (int x = bounds.left(); x <= bounds.right() + 1; ++x) {
                if (x <= bounds.right() && matches(x, y)) { if (start < 0) start = x; }
                else if (start >= 0) { spans.append(QRect(start, y, x - start, 1)); start = -1; }
            }
        }
    } else {
        QBitArray visited(m_image.width() * m_image.height());
        QVector<QPoint> pending; pending.append(position);
        auto available = [&](int x, int y) { return !visited.testBit(y * m_image.width() + x) && matches(x, y); };
        while (!pending.isEmpty()) {
            const QPoint seed = pending.takeLast();
            if (!available(seed.x(), seed.y())) continue;
            int left = seed.x(), right = seed.x();
            while (left > bounds.left() && available(left - 1, seed.y())) --left;
            while (right < bounds.right() && available(right + 1, seed.y())) ++right;
            for (int x = left; x <= right; ++x) visited.setBit(seed.y() * m_image.width() + x);
            spans.append(QRect(left, seed.y(), right - left + 1, 1));
            for (int y : {seed.y() - 1, seed.y() + 1}) {
                if (y < bounds.top() || y > bounds.bottom()) continue;
                bool inSpan = false;
                for (int x = left; x <= right; ++x) {
                    const bool match = available(x, y);
                    if (match && !inSpan) pending.append(QPoint(x, y));
                    inSpan = match;
                }
            }
        }
    }
    QRegion result;
    for (const QRect &span : spans) result |= QRegion(span);
    return result;
}
void ImageCanvas::paintRegion(const QRegion &region, const QColor &color)
{
    QPainter painter(&m_image); painter.setClipRegion(region);
    painter.setCompositionMode(m_strokeErase ? QPainter::CompositionMode_DestinationOut
        : m_blend ? QPainter::CompositionMode_SourceOver : QPainter::CompositionMode_Source);
    painter.fillRect(region.boundingRect(), color);
}
void ImageCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) { const QPoint delta = event->globalPos() - m_panPosition; m_panPosition = event->globalPos(); emit panRequested(delta); return; }
    const QPoint position = imagePosition(event->pos()); emit positionChanged(position);
    if (!m_editable && (event->buttons() & Qt::LeftButton)) {
        if (m_image.rect().contains(position)) emit positionPicked(position);
    } else if (m_drawing) {
        if (m_tool == Tool::Line || m_tool == Tool::Polygon || m_tool == Tool::Rectangle || m_tool == Tool::Ellipse || m_tool == Tool::RoundedRectangle)
            m_modifiers = event->modifiers();
        drawTo(position); update(); emit imageChanged();
    }
}
void ImageCanvas::commitGesture(const QString &description)
{
    if (!m_drawing) return;
    m_drawing = false; m_sprayTimer.stop();
    // Floating pixels keep their destination's original background when moved again.
    const QImage floatingBase = m_floatingBase, floatingImage = m_floatingImage;
    const bool hasFloatingText = m_hasFloatingText;
    const QPoint floatingPosition = m_floatingPosition + m_last - m_start;
    const QRegion floatingRegion = m_floatingRegion.translated(m_last - m_start);
    if (m_image != m_before || m_selection != m_selectionBefore)
        m_history->push(new ImageEditCommand(this, m_before, m_image, m_selectionBefore, m_selection, description));
    if (m_movingSelection) {
        m_floatingBase = floatingBase; m_floatingImage = floatingImage;
        m_floatingPosition = floatingPosition; m_floatingRegion = floatingRegion;
        m_hasFloatingText = hasFloatingText;
    }
    m_movingSelection = false;
    m_before = QImage(); update(); emit imageChanged();
}
void ImageCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) { m_panning = false; setCursor(Qt::CrossCursor); return; }
    if (!m_drawing || event->button() != m_drawButton) return;
    const QPoint position = constrainedPosition(imagePosition(event->pos()));
    if (m_tool == Tool::Polygon) {
        if (!m_polygon.isEmpty() && position != m_polygon.last()) m_polygon.append(position);
        return;
    }
    if (position != m_last) drawTo(position);
    commitGesture(selectionTool() ? tr("Edit selection") : m_strokeErase ? tr("Erase") : tr("Draw"));
}
void ImageCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_tool == Tool::Polygon && m_editable) finishPolygon();
    else emit doubleClicked();
    event->accept();
}
void ImageCanvas::finishPolygon()
{
    if (m_polygon.isEmpty()) return;
    drawPolygon(m_polygon.last()); m_polygon.clear(); commitGesture(tr("Draw polygon"));
}
void ImageCanvas::finishGesture()
{ if (!m_polygon.isEmpty()) finishPolygon(); else if (m_drawing) commitGesture(tr("Draw")); }
void ImageCanvas::cancelGesture()
{
    m_sprayTimer.stop(); m_polygon.clear();
    if (!m_drawing) return;
    m_drawing = false; m_image = m_before; m_selection = m_selectionBefore; m_before = QImage();
    m_floatingBase = QImage(); m_floatingImage = QImage(); m_movingSelection = false;
    m_hasFloatingText = false;
    update(); emit imageChanged();
}
void ImageCanvas::undo() { cancelGesture(); m_history->undo(); }
void ImageCanvas::redo() { cancelGesture(); m_history->redo(); }
void ImageCanvas::copy() { finishGesture(); QApplication::clipboard()->setImage(editingImage()); }
void ImageCanvas::cut() { copy(); eraseSelection(); }
void ImageCanvas::eraseSelection()
{
    if (!m_editable || m_image.isNull()) return;
    finishGesture(); QImage result = m_image;
    QPainter painter(&result); painter.setClipRegion(editRegion());
    painter.setCompositionMode(QPainter::CompositionMode_Clear); painter.fillRect(m_image.rect(), Qt::transparent); painter.end();
    if (result != m_image) m_history->push(new ImageEditCommand(this, m_image, result, m_selection, m_selection, tr("Erase selection")));
}
void ImageCanvas::fillSelection()
{
    if (!m_editable || m_image.isNull()) return;
    finishGesture(); const QImage before = m_image; QColor color = m_foreground; color.setAlpha(color.alpha() * m_opacity / 255);
    m_strokeErase = false;
    paintRegion(editRegion(), color);
    if (m_image != before) m_history->push(new ImageEditCommand(this, before, m_image, m_selection, m_selection, tr("Erase to left color")));
}
void ImageCanvas::paste()
{ pasteImage(QApplication::clipboard()->image(), m_selection.isEmpty() ? QPoint() : m_selection.boundingRect().topLeft(), tr("Paste")); }
void ImageCanvas::pasteImage(const QImage &image, const QPoint &position, const QString &description)
{
    if (!m_editable || m_image.isNull() || image.isNull()) return;
    finishGesture(); const QImage before = m_image;
    QImage result = m_image; QPainter painter(&result); painter.drawImage(position, image); painter.end();
    m_history->push(new ImageEditCommand(this, m_image, result, m_selection, QRegion(QRect(position, image.size())).intersected(QRegion(result.rect())), description));
    m_floatingBase = before; m_floatingImage = image; m_floatingPosition = position;
    m_floatingRegion = QRegion(QRect(position, image.size()));
    m_tool = Tool::Selection; emit toolChanged();
}
void ImageCanvas::pasteText(const QImage &image, const QPoint &position, const ImageText &text, bool replace)
{
    finishGesture();
    if (!replace || !m_hasFloatingText) pasteImage(image, position, tr("Draw text"));
    else {
        const QImage base = m_floatingBase;
        QImage result = base; QPainter painter(&result); painter.drawImage(position, image); painter.end();
        m_history->push(new ImageEditCommand(this, m_image, result, m_selection,
            QRegion(QRect(position, image.size())).intersected(QRegion(result.rect())), tr("Edit text")));
        m_floatingBase = base; m_floatingImage = image; m_floatingPosition = position;
        m_floatingRegion = QRegion(QRect(position, image.size()));
    }
    m_floatingText = text; m_hasFloatingText = true;
}
void ImageCanvas::selectAll() { clearSelection(); m_selection = QRegion(m_image.rect()); update(); emit imageChanged(); }
void ImageCanvas::clearSelection() { finishGesture(); m_selection = QRegion(); m_floatingBase = QImage(); m_floatingImage = QImage(); m_hasFloatingText = false; update(); emit imageChanged(); }
void ImageCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (!m_polygon.isEmpty()) finishPolygon(); else { cancelGesture(); clearSelection(); }
        event->accept();
    } else if (event->key() == Qt::Key_Return && !m_polygon.isEmpty()) { finishPolygon(); event->accept(); }
    else QWidget::keyPressEvent(event);
}
void ImageCanvas::wheelEvent(QWheelEvent *event)
{
    if (m_editable || (event->modifiers() & Qt::ControlModifier)) {
        if (event->angleDelta().y()) emit zoomRequested(event->angleDelta().y() > 0 ? 1 : -1);
        event->accept();
    } else event->ignore();
}
