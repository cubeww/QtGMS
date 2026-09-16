#include "roomcanvas.h"
#include "roomassets.h"
#include "actionxml.h"
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QWheelEvent>
#include <QFocusEvent>
#include <QCursor>
#include <cmath>
#include <algorithm>

static qreal roomNumber(QDomElement xml, const QString &key, qreal defaultValue = 0)
{
    bool ok; const qreal number = xml.attribute(key).toDouble(&ok);
    return ok && std::isfinite(number) ? qBound(-100000000.0, number, 100000000.0) : defaultValue;
}
class RoomGraphicsItem : public QGraphicsItem
{
public:
    QString id;
    bool tile = false, locked = false;
    int depth = 0;
    QPixmap image;
    QRectF source, bounds;
    QPointF scaleFactors;
    qreal angle = 0;
    QRectF boundingRect() const override { return bounds; }
    void resizeVisual(qreal xScale, qreal yScale)
    {
        scaleFactors = QPointF(xScale, yScale);
        // Scale in the sprite's axes, then rotate around its declared origin.
        setTransform(QTransform().rotate(angle).scale(xScale, yScale));
    }
    void configure(const RoomEntity &record, RoomAssets *assets)
    {
        prepareGeometryChange(); id = record.id; tile = record.tile;
        const QDomElement xml = record.xml;
        depth = xml.attribute(QStringLiteral("depth")).toInt();
        const RoomVisual visual = tile ? assets->background(xml.attribute(QStringLiteral("bgName"))) : assets->object(xml.attribute(QStringLiteral("objName")));
        quint32 color = xml.attribute(QStringLiteral("colour"), QStringLiteral("4294967295")).toUInt();
        image = assets->colored(visual.image, color);
        if (image.isNull()) image = QPixmap(QStringLiteral(":/images/object.png"));
        source = tile ? QRectF(roomNumber(xml, QStringLiteral("xo")), roomNumber(xml, QStringLiteral("yo")),
                              qMax(1.0, roomNumber(xml, QStringLiteral("w"), 16)), qMax(1.0, roomNumber(xml, QStringLiteral("h"), 16))) : QRectF(image.rect());
        bounds = QRectF(tile ? QPointF() : -QPointF(visual.origin), source.size());
        locked = xml.attribute(QStringLiteral("locked")).toInt() != 0;
        setFlags(ItemIsSelectable); setOpacity((color >> 24) / 255.0);
        angle = tile ? 0 : -roomNumber(xml, QStringLiteral("rotation"));
        resizeVisual(roomNumber(xml, QStringLiteral("scaleX"), 1), roomNumber(xml, QStringLiteral("scaleY"), 1));
        setPos(roomNumber(xml, QStringLiteral("x")), roomNumber(xml, QStringLiteral("y")));
        setZValue(-(tile ? roomNumber(xml, QStringLiteral("depth")) : visual.depth) + record.order * 0.000000001);
        setToolTip(xml.attribute(tile ? QStringLiteral("bgName") : QStringLiteral("objName")) + QLatin1Char('\n') + id.mid(2));
        update();
    }
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->drawPixmap(bounds, image, source);
        if (isSelected()) { QPen pen(locked ? QColor(255, 165, 0) : QColor(0, 180, 255)); pen.setCosmetic(true); painter->setPen(pen); painter->setBrush(Qt::NoBrush); painter->drawRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5)); }
    }
};

static QVector<QPointF> roomResizeHandles(const QRectF &rect)
{
    const QPointF center = rect.center();
    return {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft(),
            QPointF(center.x(), rect.top()), QPointF(rect.right(), center.y()),
            QPointF(center.x(), rect.bottom()), QPointF(rect.left(), center.y())};
}

static Qt::CursorShape roomResizeCursor(RoomGraphicsItem *item, int handle)
{
    const auto points = roomResizeHandles(item->bounds);
    const QPointF direction = item->mapToScene(points.at(handle)) - item->mapToScene(item->bounds.center());
    const int sector = (qRound(std::atan2(direction.y(), direction.x()) * 4 / 3.141592653589793) + 8) % 4;
    const Qt::CursorShape shapes[] = {Qt::SizeHorCursor, Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor};
    return shapes[sector];
}

void RoomCanvas::invalidateOverlay(const QRectF &rect)
{
    if (rect.isEmpty()) return;
    // Clip floating-point coordinates before converting to widget pixels.
    const QRectF visible = viewportTransform().mapRect(rect).adjusted(-8, -8, 8, 8).intersected(QRectF(viewport()->rect()));
    if (!visible.isEmpty()) viewport()->update(visible.toAlignedRect());
}

void RoomCanvas::refreshPlacementVisual()
{
    invalidateOverlay(m_ghostRect); m_ghostImage = QPixmap(); m_ghostBounds = QRectF();
    if (m_mode == Mode::Objects && !m_object.isEmpty()) {
        const RoomVisual visual = m_assets->object(m_object);
        m_ghostImage = visual.image;
        if (m_ghostImage.isNull()) m_ghostImage = QPixmap(QStringLiteral(":/images/object.png"));
        m_ghostSource = m_ghostImage.rect(); m_ghostBounds = QRectF(-QPointF(visual.origin), m_ghostSource.size());
    } else if (m_mode == Mode::Tiles && !m_tileBackground.isEmpty() && !m_tileSource.isEmpty()) {
        const RoomVisual visual = m_assets->background(m_tileBackground);
        if (visual.image.rect().contains(m_tileSource)) {
            m_ghostImage = visual.image; m_ghostSource = m_tileSource;
            m_ghostBounds = QRectF(QPointF(), m_tileSource.size());
        }
    }
    updateGhost(); invalidateOverlay(m_ghostRect);
}

void RoomCanvas::updateGhost()
{
    const QRectF previous = m_ghostRect; m_ghostRect = QRectF();
    const auto modifiers = QApplication::keyboardModifiers();
    if (m_pointerInside && !m_panning && m_dragPositions.isEmpty() && m_resizeId.isEmpty()) {
        RoomGraphicsItem *handleItem = nullptr;
        const int handle = m_space ? -1 : resizeHandleAt(m_pointerPosition, &handleItem);
        if (m_space) setCursor(Qt::OpenHandCursor);
        else if (handle >= 0 && !(modifiers & Qt::ControlModifier)) setCursor(roomResizeCursor(handleItem, handle));
        else unsetCursor();
        const bool adding = modifiers & Qt::ControlModifier;
        if (!m_space && !m_ghostImage.isNull() && (!m_leftPressed || m_adding)
            && (!(modifiers & Qt::ShiftModifier) || adding)
            && (adding || (handle < 0 && !itemAtRoom(m_pointerPosition)))) {
            m_ghostRect = m_ghostBounds.translated(snapped(mapToScene(m_pointerPosition), modifiers));
        }
    }
    if (previous != m_ghostRect) { invalidateOverlay(previous); invalidateOverlay(m_ghostRect); }
}

int RoomCanvas::resizeHandleAt(const QPoint &position, RoomGraphicsItem **result) const
{
    const QPointF pointer(position);
    // Query the scene index around the pointer, not every room entity.
    for (auto *raw : items(QRect(position - QPoint(7, 7), QSize(15, 15)), Qt::IntersectsItemBoundingRect)) {
        auto *item = static_cast<RoomGraphicsItem *>(raw);
        if (!item->isSelected() || item->locked || !item->isVisible()
            || (m_mode != Mode::Inspect && item->tile != (m_mode == Mode::Tiles))) continue;
        auto points = roomResizeHandles(item->bounds);
        const QTransform view = item->deviceTransform(viewportTransform());
        for (auto &point : points) point = view.map(point);
        // Corners take precedence over edges for small sprites.
        for (int i = 0; i < points.size(); ++i) {
            const QPointF delta = pointer - points.at(i);
            if (QPointF::dotProduct(delta, delta) <= 49) { *result = item; return i; }
        }
        for (int i = 0; i < 4; ++i) {
            const QPointF start = points.at(i), delta = points.at((i + 1) % 4) - start;
            const qreal length = QPointF::dotProduct(delta, delta);
            if (length <= 0) continue;
            const qreal fraction = qBound(0.0, QPointF::dotProduct(pointer - start, delta) / length, 1.0);
            const QPointF distance = pointer - (start + delta * fraction);
            if (QPointF::dotProduct(distance, distance) <= 25) { *result = item; return 4 + i; }
        }
    }
    return -1;
}

void RoomCanvas::beginResize(RoomGraphicsItem *item, int handle)
{
    m_resizeId = item->id; m_resizeStart = m_cursor;
    const auto points = roomResizeHandles(item->bounds);
    const int opposite = handle < 4 ? (handle + 2) % 4 : 4 + (handle - 4 + 2) % 4;
    m_resizeMovingLocal = points.at(handle); m_resizeFixedLocal = points.at(opposite);
    m_resizeFixedScene = item->mapToScene(m_resizeFixedLocal);
    m_resizeScale = item->scaleFactors; m_resizeRotation = QTransform().rotate(item->angle);
    setCursor(roomResizeCursor(item, handle)); updateGhost();
}

void RoomCanvas::resizeTo(const QPointF &position, Qt::KeyboardModifiers modifiers)
{
    auto *item = m_items.value(m_resizeId);
    if (!item) { m_resizeId.clear(); unsetCursor(); updateGhost(); return; }
    QPointF delta = position - m_resizeStart;
    if (!(modifiers & Qt::AltModifier)) {
        if (ActionXml::text(m_settings.documentElement(), QStringLiteral("isometric")).toInt()) delta = snapped(delta, modifiers);
        else delta = QPointF(qRound(delta.x() / m_snapX) * m_snapX, qRound(delta.y() / m_snapY) * m_snapY);
    }
    delta = m_resizeRotation.inverted().map(delta);
    const QPointF span = m_resizeMovingLocal - m_resizeFixedLocal;
    QPointF scale = m_resizeScale;
    if (span.x() != 0) scale.rx() += delta.x() / span.x();
    if (span.y() != 0) scale.ry() += delta.y() / span.y();
    if (span.x() != 0 && qAbs(scale.x()) < 1.0 / item->bounds.width()) scale.setX((scale.x() < 0 ? -1.0 : 1.0) / item->bounds.width());
    if (span.y() != 0 && qAbs(scale.y()) < 1.0 / item->bounds.height()) scale.setY((scale.y() < 0 ? -1.0 : 1.0) / item->bounds.height());
    scale.setX(qBound(-100000000.0, scale.x(), 100000000.0)); scale.setY(qBound(-100000000.0, scale.y(), 100000000.0));
    QTransform resizedTransform = m_resizeRotation; resizedTransform.scale(scale.x(), scale.y());
    const QPointF positionAfterResize = m_resizeFixedScene - resizedTransform.map(m_resizeFixedLocal);
    if (!std::isfinite(positionAfterResize.x()) || !std::isfinite(positionAfterResize.y())
        || qAbs(positionAfterResize.x()) > 100000000 || qAbs(positionAfterResize.y()) > 100000000) return;
    if (scale == item->scaleFactors && positionAfterResize == item->pos()) return;
    invalidateOverlay(item->sceneBoundingRect());
    item->resizeVisual(scale.x(), scale.y());
    item->setPos(positionAfterResize);
    invalidateOverlay(item->sceneBoundingRect());
}

void RoomCanvas::cancelResize()
{
    if (m_resizeId.isEmpty()) return;
    const QString id = m_resizeId;
    m_resizeId.clear(); unsetCursor();
    if (auto *item = m_items.value(id)) invalidateOverlay(item->sceneBoundingRect());
    synchronize({id}); updateGhost();
}

void RoomCanvas::drawEditingOverlay(QPainter *painter, const QRectF &rect)
{
    if (!m_ghostRect.isEmpty() && m_ghostRect.intersects(rect)) {
        painter->save(); painter->setOpacity(0.45); painter->drawPixmap(m_ghostRect, m_ghostImage, m_ghostSource); painter->restore();
    }
    painter->save(); QPen pen(QColor(0, 180, 255)); pen.setCosmetic(true);
    painter->setPen(pen); painter->setBrush(QColor(35, 35, 35));
    const qreal radius = 3.5 / transform().m11();
    for (auto *raw : scene()->selectedItems()) {
        auto *item = static_cast<RoomGraphicsItem *>(raw);
        if (item->locked || !item->isVisible() || !item->sceneBoundingRect().adjusted(-radius, -radius, radius, radius).intersects(rect)
            || (m_mode != Mode::Inspect && item->tile != (m_mode == Mode::Tiles))) continue;
        for (const auto &point : roomResizeHandles(item->bounds)) {
            const QPointF center = item->mapToScene(point);
            painter->drawRect(QRectF(center - QPointF(radius, radius), QSizeF(radius * 2, radius * 2)));
        }
    }
    painter->restore();
}
RoomCanvas::RoomCanvas(RoomDocument *document, RoomAssets *assets, QWidget *parent)
    : QGraphicsView(parent), m_document(document), m_assets(assets), m_checker(32, 32)
{
    auto *world = new QGraphicsScene(this); world->setItemIndexMethod(QGraphicsScene::BspTreeIndex); setScene(world);
    setObjectName(QStringLiteral("roomCanvas"));
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate); setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform, false);
    // Custom navigation must not also use Qt's cached last mouse position.
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setDragMode(QGraphicsView::RubberBandDrag); setRubberBandSelectionMode(Qt::IntersectsItemBoundingRect); setMouseTracking(true); setFocusPolicy(Qt::StrongFocus);
    m_checker.fill(QColor(192, 192, 192)); QPainter checker(&m_checker); checker.fillRect(0, 0, 16, 16, QColor(128, 128, 128)); checker.fillRect(16, 16, 16, 16, QColor(128, 128, 128)); checker.end();
    connect(world, &QGraphicsScene::selectionChanged, this, &RoomCanvas::selectionChanged);
    connect(this, &RoomCanvas::selectionChanged, this, [this] { viewport()->update(); updateGhost(); });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { updateGhost(); emit viewChanged(); });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { updateGhost(); emit viewChanged(); });
    connect(document, &RoomDocument::entitiesChanged, this, &RoomCanvas::synchronize);
    connect(document, &RoomDocument::settingsChanged, this, &RoomCanvas::refreshSettings);
    refreshSettings(); synchronize(document->entities().keys());
    centerOn(m_room.center());
}
RoomCanvas::~RoomCanvas()
{
    // Deleting selected graphics items can emit selectionChanged. Stop
    // forwarding it before either the item map or the owning inspector dies.
    QGraphicsScene *world = scene();
    QObject::disconnect(world, nullptr, this, nullptr);
    setScene(nullptr);
    delete world;
    m_items.clear();
}
void RoomCanvas::refreshSettings()
{
    m_settings = m_document->settings(); const QDomElement root = m_settings.documentElement();
    m_room = QRectF(0, 0, ActionXml::text(root, QStringLiteral("width")).toInt(), ActionXml::text(root, QStringLiteral("height")).toInt());
    m_snapX = qMax(1, ActionXml::text(root, QStringLiteral("hsnap")).toInt()); m_snapY = qMax(1, ActionXml::text(root, QStringLiteral("vsnap")).toInt());
    scene()->setSceneRect(scene()->sceneRect().united(m_room.adjusted(-128, -128, 512, 256))); viewport()->update(); updateGhost();
    emit previewChanged();
}
void RoomCanvas::synchronize(const QStringList &ids)
{
    if (!m_resizeId.isEmpty() && ids.contains(m_resizeId)) {
        if (auto *item = m_items.value(m_resizeId)) invalidateOverlay(item->sceneBoundingRect());
        m_resizeId.clear(); unsetCursor();
    }
    QSignalBlocker blocker(scene()); QRectF sceneBounds = scene()->sceneRect();
    for (const QString &id : ids) {
        const auto found = m_document->entities().constFind(id);
        if (found == m_document->entities().constEnd()) { delete m_items.take(id); continue; }
        auto *item = m_items.value(id);
        if (!item) { item = new RoomGraphicsItem; m_items.insert(id, item); scene()->addItem(item); }
        item->configure(found.value(), m_assets); item->setVisible(itemVisible(item));
        const QRectF bounds = item->sceneBoundingRect(); if (!sceneBounds.contains(bounds)) sceneBounds = sceneBounds.united(bounds.adjusted(-64, -64, 64, 64));
    }
    if (sceneBounds != scene()->sceneRect()) scene()->setSceneRect(sceneBounds);
    blocker.unblock(); emit selectionChanged(); emit previewChanged();
}
void RoomCanvas::reloadAssets() { finishInteraction(); m_assets->reload(); synchronize(m_document->entities().keys()); refreshPlacementVisual(); viewport()->update(); }
void RoomCanvas::setObject(const QString &name)
{ m_object = name; refreshPlacementVisual(); }
void RoomCanvas::setTile(const QString &name, const QRect &source, int depth)
{
    const bool layerChanged = m_tileDepth != depth;
    m_tileBackground = name; m_tileSource = source; m_tileDepth = depth;
    if (layerChanged && m_hideOtherTileLayers) updateItemVisibility();
    refreshPlacementVisual();
}
void RoomCanvas::setMode(Mode mode)
{
    finishInteraction();
    m_mode = mode;
    if (m_hideOtherTileLayers) updateItemVisibility();
    refreshPlacementVisual(); viewport()->update();
}
void RoomCanvas::setHideOtherTileLayers(bool enabled)
{
    m_hideOtherTileLayers = enabled; updateItemVisibility();
}
bool RoomCanvas::itemVisible(RoomGraphicsItem *item) const
{
    return item->tile ? m_showTiles && (!m_hideOtherTileLayers || m_mode != Mode::Tiles || item->depth == m_tileDepth) : m_showObjects;
}
void RoomCanvas::updateItemVisibility()
{
    QSignalBlocker blocker(scene());
    for (auto *item : m_items) item->setVisible(itemVisible(item));
    blocker.unblock(); emit selectionChanged(); emit previewChanged();
}
QStringList RoomCanvas::selectedIds() const
{ QStringList ids; for (auto *item : scene()->selectedItems()) ids.append(static_cast<RoomGraphicsItem *>(item)->id); return ids; }
void RoomCanvas::selectIds(const QStringList &ids)
{ QSignalBlocker blocker(scene()); scene()->clearSelection(); for (const QString &id : ids) if (m_items.contains(id)) m_items.value(id)->setSelected(true); blocker.unblock(); emit selectionChanged(); }
void RoomCanvas::setDisplay(const QString &key, bool visible)
{
    if (key == QStringLiteral("grid")) m_grid = visible;
    else if (key == QStringLiteral("objects")) m_showObjects = visible;
    else if (key == QStringLiteral("tiles")) m_showTiles = visible;
    else if (key == QStringLiteral("backgrounds")) m_showBackgrounds = visible;
    else if (key == QStringLiteral("foregrounds")) m_showForegrounds = visible;
    else if (key == QStringLiteral("views")) m_showViews = visible;
    if (key == QStringLiteral("objects") || key == QStringLiteral("tiles")) updateItemVisibility();
    viewport()->update();
    emit previewChanged();
}
void RoomCanvas::drawLayers(QPainter *painter, const QRectF &rect, bool foreground)
{
    if (foreground ? !m_showForegrounds : !m_showBackgrounds) return;
    painter->save(); painter->setClipRect(m_room.intersected(rect));
    for (QDomElement bg : ActionXml::elements(m_settings.documentElement().firstChildElement(QStringLiteral("backgrounds")), QStringLiteral("background"))) {
        if (!bg.attribute(QStringLiteral("visible")).toInt() || (bg.attribute(QStringLiteral("foreground")).toInt() != 0) != foreground) continue;
        const QPixmap image = m_assets->background(bg.attribute(QStringLiteral("name"))).image; if (image.isNull()) continue;
        const QPointF position(roomNumber(bg, QStringLiteral("x")), roomNumber(bg, QStringLiteral("y")));
        if (bg.attribute(QStringLiteral("stretch")).toInt()) { painter->drawPixmap(QRectF(position, m_room.size()), image, image.rect()); continue; }
        const bool horizontal = bg.attribute(QStringLiteral("htiled")).toInt(), vertical = bg.attribute(QStringLiteral("vtiled")).toInt();
        QRectF area = rect.intersected(m_room);
        if (!horizontal) area = area.intersected(QRectF(position.x(), area.top(), image.width(), area.height()));
        if (!vertical) area = area.intersected(QRectF(area.left(), position.y(), area.width(), image.height()));
        if (!area.isEmpty()) painter->drawTiledPixmap(area, image, area.topLeft() - position);
    }
    painter->restore();
}
void RoomCanvas::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, QBrush(m_checker));
    const QDomElement root = m_settings.documentElement(); const uint bgr = ActionXml::text(root, QStringLiteral("colour")).toUInt();
    if (ActionXml::text(root, QStringLiteral("showcolour")).toInt()) painter->fillRect(rect.intersected(m_room), QColor(bgr & 255, (bgr >> 8) & 255, (bgr >> 16) & 255));
    drawLayers(painter, rect, false);
}
void RoomCanvas::drawForeground(QPainter *painter, const QRectF &rect)
{
    drawLayers(painter, rect, true); painter->save(); QPen pen(QColor(60, 60, 60)); pen.setCosmetic(true); painter->setPen(pen); painter->setBrush(Qt::NoBrush);
    const QRectF area = rect.intersected(m_room); const qreal zoom = transform().m11();
    if (m_grid && !area.isEmpty()) {
        const qreal dx = m_snapX * qMax(1, int(std::ceil(6.0 / (m_snapX * zoom))));
        const qreal dy = m_snapY * qMax(1, int(std::ceil(6.0 / (m_snapY * zoom))));
        if (ActionXml::text(m_settings.documentElement(), QStringLiteral("isometric")).toInt()) {
            painter->save(); painter->setClipRect(area);
            for (qreal k = std::floor(area.left() / dx + area.top() / dy); k <= std::ceil(area.right() / dx + area.bottom() / dy); ++k)
                painter->drawLine(QPointF((k - area.top() / dy) * dx, area.top()), QPointF((k - area.bottom() / dy) * dx, area.bottom()));
            for (qreal k = std::floor(area.left() / dx - area.bottom() / dy); k <= std::ceil(area.right() / dx - area.top() / dy); ++k)
                painter->drawLine(QPointF((k + area.top() / dy) * dx, area.top()), QPointF((k + area.bottom() / dy) * dx, area.bottom()));
            painter->restore();
        } else {
            for (qreal x = std::ceil(area.left() / dx) * dx; x <= area.right(); x += dx) painter->drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
            for (qreal y = std::ceil(area.top() / dy) * dy; y <= area.bottom(); y += dy) painter->drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }
    }
    painter->drawRect(m_room);
    if (m_showViews && ActionXml::text(m_settings.documentElement(), QStringLiteral("enableViews")).toInt()) {
        pen.setColor(QColor(255, 190, 40)); painter->setPen(pen);
        for (QDomElement view : ActionXml::elements(m_settings.documentElement().firstChildElement(QStringLiteral("views")), QStringLiteral("view")))
            if (view.attribute(QStringLiteral("visible")).toInt()) painter->drawRect(QRectF(roomNumber(view, QStringLiteral("xview")), roomNumber(view, QStringLiteral("yview")), roomNumber(view, QStringLiteral("wview")), roomNumber(view, QStringLiteral("hview"))));
    }
    painter->restore();
    drawEditingOverlay(painter, rect);
}
QPointF RoomCanvas::snapped(QPointF position, Qt::KeyboardModifiers modifiers) const
{
    if (modifiers & Qt::AltModifier) return position;
    if (ActionXml::text(m_settings.documentElement(), QStringLiteral("isometric")).toInt()) {
        const qreal u = qRound(position.x() / m_snapX + position.y() / m_snapY), v = qRound(position.x() / m_snapX - position.y() / m_snapY);
        return QPointF((u + v) * m_snapX / 2, (u - v) * m_snapY / 2);
    }
    return QPointF(std::floor(position.x() / m_snapX) * m_snapX, std::floor(position.y() / m_snapY) * m_snapY);
}
RoomGraphicsItem *RoomCanvas::itemAtRoom(const QPoint &position) const
{
    for (auto *raw : items(position)) { auto *item = static_cast<RoomGraphicsItem *>(raw); if (m_mode == Mode::Inspect || (m_mode == Mode::Tiles) == item->tile) return item; }
    return nullptr;
}
void RoomCanvas::placeAt(QPointF position)
{
    const bool tile = m_mode == Mode::Tiles;
    if (m_mode == Mode::Inspect || (tile ? m_tileBackground.isEmpty() || m_tileSource.isEmpty() : m_object.isEmpty())) return;
    if (tile && !m_assets->background(m_tileBackground).image.rect().contains(m_tileSource)) return;
    if (m_adding) {
        const quint64 key = (quint64(quint32(qRound(position.x()))) << 32) | quint32(qRound(position.y()));
        if (m_stampedPositions.contains(key)) return;
        m_stampedPositions.insert(key);
    }
    RoomEntity record = m_document->createEntity(tile); QDomElement xml = record.xml;
    xml.setAttribute(QStringLiteral("x"), position.x()); xml.setAttribute(QStringLiteral("y"), position.y());
    if (tile) {
        xml.setAttribute(QStringLiteral("bgName"), m_tileBackground); xml.setAttribute(QStringLiteral("xo"), m_tileSource.x()); xml.setAttribute(QStringLiteral("yo"), m_tileSource.y());
        xml.setAttribute(QStringLiteral("w"), m_tileSource.width()); xml.setAttribute(QStringLiteral("h"), m_tileSource.height()); xml.setAttribute(QStringLiteral("depth"), m_tileDepth);
    } else xml.setAttribute(QStringLiteral("objName"), m_object);
    QVector<RoomEntity> changes;
    if (m_deleteUnderlying) for (auto *raw : scene()->items(position, Qt::IntersectsItemBoundingRect)) {
        auto *item = static_cast<RoomGraphicsItem *>(raw); if (item->tile != tile || item->locked) continue;
        RoomEntity removed = m_document->entity(item->id); removed.xml = QDomElement(); changes.append(removed);
    }
    changes.append(record); m_document->editEntities(changes, tr("Add room item")); selectIds({record.id});
}
void RoomCanvas::mousePressEvent(QMouseEvent *event)
{
    setFocus(); m_pointerPosition = event->pos(); m_pointerInside = true; m_cursor = mapToScene(event->pos());
    if (event->button() == Qt::LeftButton) m_leftPressed = true;
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_space)) {
        finishInteraction();
        m_panning = true; m_panButton = event->button();
        m_panAnchor = viewportTransform().inverted().map(event->localPos());
        setCursor(Qt::ClosedHandCursor); updateGhost(); event->accept(); return;
    }
    if (event->button() == Qt::LeftButton) {
        if (!(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
            RoomGraphicsItem *resizeItem = nullptr;
            const int handle = resizeHandleAt(event->pos(), &resizeItem);
            if (handle >= 0) { beginResize(resizeItem, handle); event->accept(); return; }
        }
        auto *item = itemAtRoom(event->pos());
        if ((event->modifiers() & Qt::ControlModifier) || (!item && !(event->modifiers() & Qt::ShiftModifier) && m_mode != Mode::Inspect)) {
            if ((event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) == (Qt::ControlModifier | Qt::ShiftModifier)) {
                m_adding = true; m_stampedPositions.clear(); m_document->undoStack()->beginMacro(tr("Paint room items"));
            }
            placeAt(snapped(m_cursor, event->modifiers())); updateGhost(); event->accept(); return;
        }
        if (item) {
            QSignalBlocker blocker(scene());
            if (event->modifiers() & Qt::ShiftModifier) item->setSelected(!item->isSelected());
            else if (!item->isSelected()) { scene()->clearSelection(); item->setSelected(true); }
            m_dragStart = m_cursor; m_dragPositions.clear();
            for (const QString &id : selectedIds()) if (!m_items.value(id)->locked) m_dragPositions.insert(id, m_items.value(id)->pos());
            blocker.unblock(); emit selectionChanged(); updateGhost();
            event->accept(); return;
        }
    }
    updateGhost(); QGraphicsView::mousePressEvent(event);
}
void RoomCanvas::mouseMoveEvent(QMouseEvent *event)
{
    m_pointerPosition = event->pos(); m_pointerInside = viewport()->rect().contains(event->pos());
    if (m_panning) {
        if (event->buttons() & m_panButton) {
            positionView(m_panAnchor, event->localPos());
            m_cursor = mapToScene(event->pos()); emit cursorMoved(m_cursor);
            event->accept(); return;
        }
        m_panning = false; m_panButton = Qt::NoButton; unsetCursor();
    }
    m_cursor = mapToScene(event->pos()); emit cursorMoved(m_cursor);
    if (!m_resizeId.isEmpty() && (event->buttons() & Qt::LeftButton)) { resizeTo(m_cursor, event->modifiers()); event->accept(); return; }
    if (m_adding && (event->buttons() & Qt::LeftButton)) { placeAt(snapped(m_cursor, event->modifiers())); updateGhost(); event->accept(); return; }
    if (!m_dragPositions.isEmpty() && (event->buttons() & Qt::LeftButton)) {
        QPointF delta = m_cursor - m_dragStart;
        if (!(event->modifiers() & Qt::AltModifier)) {
            if (ActionXml::text(m_settings.documentElement(), QStringLiteral("isometric")).toInt()) delta = snapped(delta, event->modifiers());
            else delta = QPointF(qRound(delta.x() / m_snapX) * m_snapX, qRound(delta.y() / m_snapY) * m_snapY);
        }
        for (auto it = m_dragPositions.constBegin(); it != m_dragPositions.constEnd(); ++it) {
            auto *item = m_items.value(it.key());
            if (!item) continue;
            invalidateOverlay(item->sceneBoundingRect()); item->setPos(it.value() + delta); invalidateOverlay(item->sceneBoundingRect());
        }
        event->accept(); return;
    }
    updateGhost(); QGraphicsView::mouseMoveEvent(event);
}
void RoomCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    m_pointerPosition = event->pos();
    if (event->button() == Qt::LeftButton) m_leftPressed = false;
    if (m_panning && event->button() == m_panButton) {
        m_panning = false; m_panButton = Qt::NoButton; unsetCursor(); updateGhost(); event->accept(); return;
    }
    if (event->button() == Qt::LeftButton && (m_adding || !m_dragPositions.isEmpty() || !m_resizeId.isEmpty())) { finishInteraction(); updateGhost(); event->accept(); return; }
    QGraphicsView::mouseReleaseEvent(event); updateGhost();
}
void RoomCanvas::finishInteraction()
{
    if (m_panning) { m_panning = false; m_panButton = Qt::NoButton; unsetCursor(); }
    if (m_adding) { m_adding = false; m_document->undoStack()->endMacro(); m_stampedPositions.clear(); }
    if (!m_resizeId.isEmpty()) {
        RoomEntity record = m_document->entity(m_resizeId);
        auto *item = m_items.value(m_resizeId);
        m_resizeId.clear(); unsetCursor();
        if (item && !record.xml.isNull()) {
            record.xml.setAttribute(QStringLiteral("x"), item->pos().x()); record.xml.setAttribute(QStringLiteral("y"), item->pos().y());
            record.xml.setAttribute(QStringLiteral("scaleX"), item->scaleFactors.x()); record.xml.setAttribute(QStringLiteral("scaleY"), item->scaleFactors.y());
            m_document->editEntities({record}, tr("Resize room item"));
        }
        updateGhost();
    }
    if (m_dragPositions.isEmpty()) return;
    QVector<RoomEntity> changes;
    for (const QString &id : m_dragPositions.keys()) {
        RoomEntity record = m_document->entity(id); if (record.xml.isNull() || !m_items.contains(id)) continue;
        const QPointF point = m_items.value(id)->pos(); record.xml.setAttribute(QStringLiteral("x"), point.x()); record.xml.setAttribute(QStringLiteral("y"), point.y()); changes.append(record);
    }
    m_dragPositions.clear(); m_document->editEntities(changes, tr("Move room items")); emit selectionChanged();
}
void RoomCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{ if (auto *item = itemAtRoom(event->pos())) { if (!item->tile) emit creationCodeRequested(item->id); } else QGraphicsView::mouseDoubleClickEvent(event); }
void RoomCanvas::expandNavigationBounds(const QRectF &visibleArea)
{
    if (sceneRect().contains(visibleArea)) return;
    // Grow the view's scroll range, not the scene's BSP index. Leave a full
    // viewport of slack so ordinary pan movements only update scroll offsets.
    setSceneRect(sceneRect().united(visibleArea.adjusted(-visibleArea.width(), -visibleArea.height(), visibleArea.width(), visibleArea.height())));
}
void RoomCanvas::positionView(const QPointF &scenePosition, const QPointF &viewportPosition)
{
    const QTransform inverse = transform().inverted();
    const QPointF offset = scenePosition - inverse.map(viewportPosition);
    expandNavigationBounds(inverse.mapRect(QRectF(viewport()->rect())).translated(offset));
    // Use the actual floating-point center. QRect::center() rounds down and
    // introduces a persistent pixel offset when fed back into centerOn().
    // Re-read the size because expanding the range may reveal scrollbars.
    const QPointF center(viewport()->width() / 2.0, viewport()->height() / 2.0);
    centerOn(offset + inverse.map(center));
    emit viewChanged();
}
QRectF RoomCanvas::visibleRoomRect() const
{
    return viewportTransform().inverted().mapRect(QRectF(viewport()->rect()));
}
void RoomCanvas::centerViewportOn(const QPointF &position)
{
    positionView(position, QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
    updateGhost();
}
void RoomCanvas::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    emit viewChanged();
}
void RoomCanvas::zoomAt(qreal factor, const QPointF &viewportPosition)
{
    if (!m_resizeId.isEmpty() || !m_dragPositions.isEmpty()) finishInteraction();
    const qreal zoom = qBound(0.03125, transform().m11() * factor, 16.0);
    const qreal ratio = zoom / transform().m11();
    if (qFuzzyCompare(ratio, qreal(1))) return;
    const QPointF anchor = viewportTransform().inverted().map(viewportPosition);
    scale(ratio, ratio);
    positionView(anchor, viewportPosition);
    updateGhost();
}
void RoomCanvas::zoomBy(qreal factor) { zoomAt(factor, QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0)); }
void RoomCanvas::resetZoom() { zoomBy(1.0 / transform().m11()); }
void RoomCanvas::wheelEvent(QWheelEvent *event)
{
    if (event->angleDelta().y() != 0) zoomAt(event->angleDelta().y() > 0 ? 1.25 : 0.8, event->posF());
    event->accept();
}
void RoomCanvas::deleteSelection()
{
    cancelResize();
    QVector<RoomEntity> changes;
    for (const QString &id : selectedIds()) if (!m_items.value(id)->locked) { auto record = m_document->entity(id); record.xml = QDomElement(); changes.append(record); }
    m_document->editEntities(changes, tr("Delete room items"));
}
void RoomCanvas::copySelection()
{
    QDomDocument xml; auto root = xml.createElement(QStringLiteral("roomClipboard")); xml.appendChild(root);
    QVector<RoomEntity> ordered; for (const QString &id : selectedIds()) ordered.append(m_document->entity(id));
    if (ordered.isEmpty()) return;
    std::sort(ordered.begin(), ordered.end(), [](const RoomEntity &a, const RoomEntity &b) { return a.order < b.order; });
    for (const RoomEntity &record : ordered) root.appendChild(xml.importNode(record.xml, true));
    auto *mime = new QMimeData; mime->setData(QStringLiteral("application/x-qtgms-room-items"), xml.toByteArray()); QApplication::clipboard()->setMimeData(mime);
}
void RoomCanvas::pasteSelection()
{
    const QByteArray data = QApplication::clipboard()->mimeData()->data(QStringLiteral("application/x-qtgms-room-items"));
    QDomDocument xml; if (data.size() > 16 * 1024 * 1024 || !xml.setContent(data) || xml.documentElement().tagName() != QStringLiteral("roomClipboard")) return;
    QVector<RoomEntity> records; QStringList ids;
    for (QDomElement element = xml.documentElement().firstChildElement(); !element.isNull(); element = element.nextSiblingElement()) {
        if (element.tagName() != QStringLiteral("instance") && element.tagName() != QStringLiteral("tile")) continue;
        RoomEntity record = m_document->createEntity(element.tagName() == QStringLiteral("tile")); const QString key = record.tile ? QStringLiteral("id") : QStringLiteral("name"), name = record.xml.attribute(key), instanceName = record.xml.attribute(QStringLiteral("name"));
        record.xml = element.cloneNode(true).toElement(); record.xml.setAttribute(key, name); record.xml.setAttribute(QStringLiteral("name"), instanceName); record.xml.setAttribute(QStringLiteral("locked"), 0);
        record.xml.setAttribute(QStringLiteral("x"), roomNumber(element, QStringLiteral("x")) + m_snapX); record.xml.setAttribute(QStringLiteral("y"), roomNumber(element, QStringLiteral("y")) + m_snapY);
        ids.append(record.id); records.append(record);
    }
    m_document->editEntities(records, tr("Paste room items")); selectIds(ids);
}
void RoomCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space) { m_space = true; updateGhost(); event->accept(); return; }
    if (event->key() == Qt::Key_Escape && !m_resizeId.isEmpty()) { cancelResize(); event->accept(); return; }
    if (event->key() == Qt::Key_Delete) { deleteSelection(); return; }
    if (event->matches(QKeySequence::Copy)) { copySelection(); return; }
    if (event->matches(QKeySequence::Paste)) { pasteSelection(); return; }
    if (event->matches(QKeySequence::SelectAll)) { QSignalBlocker blocker(scene()); for (auto *item : m_items) if (item->isVisible() && (m_mode == Mode::Inspect || item->tile == (m_mode == Mode::Tiles))) item->setSelected(true); blocker.unblock(); emit selectionChanged(); return; }
    if (event->key() == Qt::Key_Escape && !m_dragPositions.isEmpty()) { const auto ids = m_dragPositions.keys(); m_dragPositions.clear(); synchronize(ids); return; }
    QGraphicsView::keyPressEvent(event); updateGhost();
}
void RoomCanvas::keyReleaseEvent(QKeyEvent *event) { if (event->key() == Qt::Key_Space) m_space = false; QGraphicsView::keyReleaseEvent(event); updateGhost(); }
bool RoomCanvas::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::Enter) {
        m_pointerInside = true; m_pointerPosition = viewport()->mapFromGlobal(QCursor::pos()); updateGhost();
    } else if (event->type() == QEvent::Leave) { m_pointerInside = false; updateGhost(); }
    return QGraphicsView::viewportEvent(event);
}
void RoomCanvas::focusOutEvent(QFocusEvent *event)
{
    finishInteraction(); m_space = false; m_leftPressed = false; updateGhost();
    QGraphicsView::focusOutEvent(event);
}
void RoomCanvas::contextMenuEvent(QContextMenuEvent *event)
{
    auto *item = itemAtRoom(event->pos()); if (item && !item->isSelected()) selectIds({item->id});
    QMenu menu(this); menu.addAction(tr("Delete"), this, &RoomCanvas::deleteSelection)->setEnabled(!selectedIds().isEmpty());
    menu.addAction(tr("Copy Selection"), this, &RoomCanvas::copySelection); menu.addAction(tr("Paste Selection"), this, &RoomCanvas::pasteSelection);
    if (item) {
        const QString id = item->id; menu.addSeparator();
        if (!item->tile) { menu.addAction(tr("Creation Code..."), this, [this, id] { emit creationCodeRequested(id); }); menu.addAction(tr("Edit Object"), this, [this, id] { emit openObjectRequested(m_document->entity(id).xml.attribute(QStringLiteral("objName"))); }); }
        auto *lock = menu.addAction(tr("Locked")); lock->setCheckable(true); lock->setChecked(item->locked);
        connect(lock, &QAction::triggered, this, [this, id](bool enabled) { auto record = m_document->entity(id); record.xml.setAttribute(QStringLiteral("locked"), enabled ? -1 : 0); m_document->editEntities({record}, tr("Lock room item")); });
    }
    menu.addSeparator(); menu.addAction(tr("Reset Zoom"), this, &RoomCanvas::resetZoom); menu.exec(event->globalPos());
}

void RoomCanvas::renderPreview(QPainter *painter, const QRectF &visibleArea)
{
    if (visibleArea.isEmpty()) return;
    painter->save();
    painter->setClipRect(visibleArea, Qt::IntersectClip);
    drawBackground(painter, visibleArea);
    // Reuse the scene's visuals and stacking order without editor selections or handles.
    for (auto *raw : scene()->items(visibleArea, Qt::IntersectsItemBoundingRect, Qt::AscendingOrder)) {
        auto *item = static_cast<RoomGraphicsItem *>(raw);
        if (!item->isVisible()) continue;
        painter->save();
        painter->setTransform(item->sceneTransform(), true);
        painter->setOpacity(item->effectiveOpacity());
        painter->drawPixmap(item->bounds, item->image, item->source);
        painter->restore();
    }
    drawLayers(painter, visibleArea, true);
    painter->restore();
}
