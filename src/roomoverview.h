#ifndef QTGMS_ROOMOVERVIEW_H
#define QTGMS_ROOMOVERVIEW_H

#include <QPixmap>
#include <QPointer>
#include <QWidget>

class RoomCanvas;
class QTimer;

class RoomOverview : public QWidget
{
    Q_OBJECT
public:
    explicit RoomOverview(RoomCanvas *canvas, QWidget *parent = nullptr);
    QSize sizeHint() const override { return QSize(250, 189); }
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
private:
    QRectF previewRect() const;
    QPointF roomPosition(const QPointF &position) const;
    void navigate(const QPointF &position);
    void invalidatePreview();
    void rebuildPreview();
    QPointer<RoomCanvas> m_canvas;
    QTimer *m_refreshTimer;
    QPixmap m_preview;
    QPointF m_dragOffset;
    bool m_dragging = false;
};

#endif
