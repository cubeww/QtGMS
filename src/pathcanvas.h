#ifndef QTGMS_PATHCANVAS_H
#define QTGMS_PATHCANVAS_H
#include <QWidget>
#include <QPainterPath>
#include "pathdocument.h"
class RoomCanvas;
class PathCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit PathCanvas(PathDocument *document, QWidget *parent = nullptr);
    void setRoomPreview(RoomCanvas *room);
    void setGridVisible(bool visible) { m_grid = visible; update(); }
    void panBy(const QPointF &distance);
    void centerPath();
    void finishInteraction(bool commit = true);
    QRectF visibleArea() const;
    void deleteSelected();
signals:
    void cursorMoved(const QPointF &position);
    void viewChanged();
protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
private:
    QPointF toWorld(const QPointF &pixel) const;
    QPointF snapped(const QPointF &point, Qt::KeyboardModifiers modifiers) const;
    int hitPoint(const QPointF &pixel) const;
    void rebuildCurve();
    void synchronize();
    PathDocument *m_document;
    RoomCanvas *m_room = nullptr;
    QVector<PathPoint> m_points;
    QPainterPath m_curve;
    QPointF m_origin, m_panAnchor, m_dragOffset, m_dragStart;
    qreal m_zoom = 1;
    int m_dragIndex = -1;
    Qt::MouseButton m_panButton = Qt::NoButton;
    bool m_grid = true, m_space = false, m_newPoint = false;
};
#endif
