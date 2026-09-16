#ifndef QTGMS_IMAGECANVAS_H
#define QTGMS_IMAGECANVAS_H
#include <QImage>
#include <QFont>
#include <QRegion>
#include <QPolygon>
#include <QTimer>
#include <QUndoStack>
#include <QWidget>
class ImageEditCommand;

struct ImageText {
    QString text;
    QFont font;
    QColor color;
    int alignment = 0;
    bool antialias = false;
};

class ImageCanvas : public QWidget
{
    Q_OBJECT
public:
    enum class Tool { Brush, Airbrush, Eraser, Picker, Line, Polygon, Rectangle, Ellipse,
        RoundedRectangle, Selection, Fill, ReplaceColor, Text, BrushSelection, Wand };
    enum class ShapeStyle { Outline, OutlineAndFill, Filled };
    explicit ImageCanvas(QWidget *parent = nullptr);
    const QImage &image() const { return m_image; }
    QImage editingImage() const;
    QRect selectionBounds() const { return m_selection.boundingRect(); }
    QUndoStack *undoStack() { return m_history; }
    void setUndoStack(QUndoStack *stack) { m_history = stack; }
    qreal zoom() const { return m_zoom; }
    Tool tool() const { return m_tool; }
    void setImage(const QImage &image);
    void replaceImage(const QImage &image, const QString &description);
    void replaceSelection(const QImage &image, const QString &description);
    void setEditable(bool editable);
    void setTool(Tool tool);
    void setZoom(qreal zoom);
    void setGridVisible(bool visible);
    void setGrid(const QSize &size, const QPoint &offset, const QColor &color);
    void setTransparencyBackground(const QColor &first, const QColor &second, int size);
    void setBrushSize(int size) { m_brushSize = size; }
    void setHardness(int hardness) { m_hardness = hardness; }
    void setOpacity(int opacity) { m_opacity = opacity; }
    void setBlend(bool blend) { m_blend = blend; }
    void setTolerance(int tolerance) { m_tolerance = tolerance; }
    void setColorOnly(bool colorOnly) { m_colorOnly = colorOnly; }
    void setAntialiasing(bool enabled) { m_antialiasing = enabled; }
    void setShapeStyle(ShapeStyle style) { m_shapeStyle = style; }
    void setArrowMode(int mode) { m_arrowMode = mode; }
    void setRightErase(bool enabled) { m_rightErase = enabled; }
    void setForeground(const QColor &color) { m_foreground = color; }
    void setBackground(const QColor &color) { m_background = color; }
    void setFilled(bool filled) { m_shapeStyle = filled ? ShapeStyle::Filled : ShapeStyle::Outline; }
    void setCrosshair(const QPoint &position, bool visible);
    void setOverlay(const QImage &image);
    void setOnionImages(const QList<QImage> &images, int opacity);
    void copy();
    void cut();
    void paste();
    void pasteImage(const QImage &image, const QPoint &position, const QString &description);
    void pasteText(const QImage &image, const QPoint &position, const ImageText &text, bool replace);
    const ImageText &floatingText() const { return m_floatingText; }
    void eraseSelection();
    void fillSelection();
    void selectAll();
    void clearSelection();
    void cancelGesture();
    void finishGesture();
    void undo();
    void redo();
signals:
    void imageChanged();
    void zoomChanged();
    void toolChanged();
    void positionChanged(const QPoint &position);
    void positionPicked(const QPoint &position);
    void colorPicked(const QColor &color, bool secondary);
    void textRequested(const QPoint &position, bool secondary, bool editing);
    void zoomRequested(int steps);
    void panRequested(const QPoint &delta);
    void doubleClicked();
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
private:
    friend class ImageEditCommand;
    void applyImage(const QImage &image, const QRegion &selection);
    void commitGesture(const QString &description);
    void drawTo(QPoint position);
    void paintStroke(const QPoint &position);
    void drawPolygon(const QPoint &position);
    void finishPolygon();
    QRegion matchingRegion(const QPoint &position, bool contiguous) const;
    void paintRegion(const QRegion &region, const QColor &color);
    void changeSelection(const QRegion &region);
    QPoint constrainedPosition(QPoint position) const;
    QPoint imagePosition(const QPoint &position) const;
    QRegion editRegion() const;
    bool selectionTool() const;
    QImage selectionImage(const QImage &image, const QRegion &region) const;
    QImage m_image, m_before, m_overlay;
    QImage m_floatingBase, m_floatingImage;
    ImageText m_floatingText;
    bool m_hasFloatingText = false;
    QPoint m_floatingPosition;
    QList<QImage> m_onionImages;
    QUndoStack m_undoStack;
    QUndoStack *m_history = &m_undoStack;
    QRegion m_selection, m_selectionBefore, m_gestureSelection, m_floatingRegion;
    QPolygon m_polygon;
    QPoint m_start, m_last, m_crosshair, m_panPosition;
    QColor m_foreground = Qt::black, m_background = Qt::white, m_drawColor;
    QColor m_checkerFirst = QColor(192, 192, 192), m_checkerSecond = QColor(240, 240, 240);
    QColor m_gridColor = QColor(0, 0, 0, 100);
    QSize m_gridSize = QSize(1, 1);
    QPoint m_gridOffset;
    Tool m_tool = Tool::Brush;
    ShapeStyle m_shapeStyle = ShapeStyle::Outline;
    qreal m_zoom = 1.0;
    int m_brushSize = 1, m_hardness = 100, m_opacity = 255, m_tolerance = 0, m_arrowMode = 0;
    int m_checkerSize = 8, m_onionOpacity = 64;
    Qt::KeyboardModifiers m_modifiers;
    Qt::MouseButton m_drawButton = Qt::NoButton;
    bool m_editable = true, m_drawing = false, m_movingSelection = false;
    bool m_gridVisible = false, m_crosshairVisible = false, m_colorOnly = false;
    bool m_antialiasing = false, m_blend = true, m_rightErase = false, m_strokeErase = false;
    bool m_copySelection = false, m_panning = false;
    QTimer m_sprayTimer;
};
#endif
