#ifndef QTGMS_ROOMCANVAS_H
#define QTGMS_ROOMCANVAS_H
#include <QGraphicsView>
#include <QDomDocument>
#include <QHash>
#include <QSet>
#include "roomdocument.h"
class RoomAssets;
class RoomGraphicsItem;
class RoomCanvas : public QGraphicsView
{
    Q_OBJECT
public:
    enum class Mode { Objects, Tiles, Inspect };
    RoomCanvas(RoomDocument *document, RoomAssets *assets, QWidget *parent = nullptr);
    ~RoomCanvas() override;
    void setMode(Mode mode);
    void setViewsPageActive(bool active);
    void setObject(const QString &name);
    void setTile(const QString &name, const QRect &source, int depth);
    void setDeleteUnderlying(bool enabled) { m_deleteUnderlying = enabled; }
    void setHideOtherTileLayers(bool enabled);
    QStringList selectedIds() const;
    void selectIds(const QStringList &ids);
    void deleteSelection();
    void copySelection();
    void pasteSelection();
    void zoomBy(qreal factor);
    void resetZoom();
    void setDisplay(const QString &key, bool visible);
    void reloadAssets();
    void synchronize(const QStringList &ids);
    void refreshSettings();
    void finishInteraction();
    void renderPreview(QPainter *painter, const QRectF &visibleArea);
    QRectF roomRect() const { return m_room; }
    QRectF visibleRoomRect() const;
    void centerViewportOn(const QPointF &position);
signals:
    void previewChanged();
    void viewChanged();
    void selectionChanged();
    void cursorMoved(const QPointF &position);
    void creationCodeRequested(const QString &id);
    void openObjectRequested(const QString &name);
protected:
    void resizeEvent(QResizeEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
private:
    void refreshPlacementVisual();
    void updateGhost();
    void invalidateOverlay(const QRectF &rect);
    void drawEditingOverlay(QPainter *painter, const QRectF &rect);
    int resizeHandleAt(const QPoint &position, RoomGraphicsItem **item) const;
    void beginResize(RoomGraphicsItem *item, int handle);
    void resizeTo(const QPointF &position, Qt::KeyboardModifiers modifiers);
    void cancelResize();
    void expandNavigationBounds(const QRectF &visibleArea);
    void positionView(const QPointF &scenePosition, const QPointF &viewportPosition);
    void zoomAt(qreal factor, const QPointF &viewportPosition);
    void drawLayers(QPainter *painter, const QRectF &rect, bool foreground);
    QPointF snapped(QPointF position, Qt::KeyboardModifiers modifiers) const;
    RoomGraphicsItem *itemAtRoom(const QPoint &position) const;
    void placeAt(QPointF position);
    void appendUnderlyingRemovals(QVector<RoomEntity> &changes);
    void updateItemVisibility();
    bool itemVisible(RoomGraphicsItem *item) const;
    RoomDocument *m_document;
    RoomAssets *m_assets;
    QHash<QString, RoomGraphicsItem *> m_items;
    QDomDocument m_settings;
    QRectF m_room;
    Mode m_mode = Mode::Objects;
    QString m_object, m_tileBackground;
    QRect m_tileSource;
    int m_tileDepth = 1000000;
    int m_snapX = 32, m_snapY = 32;
    bool m_deleteUnderlying = false, m_panning = false, m_space = false;
    bool m_hideOtherTileLayers = false;
    bool m_viewsPageActive = false;
    bool m_grid = true, m_showObjects = true, m_showTiles = true, m_showBackgrounds = true, m_showForegrounds = true, m_showViews = false;
    QPointF m_panAnchor;
    Qt::MouseButton m_panButton = Qt::NoButton;
    QPointF m_dragStart, m_cursor;
    QHash<QString, QPointF> m_dragPositions;
    QPixmap m_checker;
    bool m_adding = false;
    QSet<quint64> m_stampedPositions;
    QPixmap m_ghostImage;
    QRectF m_ghostSource, m_ghostBounds, m_ghostRect;
    QPoint m_pointerPosition;
    bool m_pointerInside = false, m_leftPressed = false;
    QString m_resizeId;
    QPointF m_resizeStart, m_resizeFixedLocal, m_resizeFixedScene, m_resizeMovingLocal, m_resizeScale;
    QTransform m_resizeRotation;
};
#endif
