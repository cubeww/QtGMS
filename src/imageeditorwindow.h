#ifndef QTGMS_IMAGEEDITORWINDOW_H
#define QTGMS_IMAGEEDITORWINDOW_H
#include "editorwindow.h"
#include "imagecanvas.h"
#include "imageoperations.h"
#include <QImage>
#include <QFont>
class QLabel;
class QPushButton;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QGroupBox;
class QScrollArea;
class QAction;
class ImagePalette;
class ImageEditorWindow : public EditorWindow
{
    Q_OBJECT
public:
    explicit ImageEditorWindow(const QImage &image, QWidget *parent = nullptr);
    void setFrames(const QList<QImage> &frames, int index, const QString &name);
    void setSpriteOrigin(const QPoint &origin) { m_origin = m_originalOrigin = origin; m_hasOrigin = true; }
signals:
    void imageAccepted(const QImage &image);
    void imagesAccepted(const QList<QImage> &images, const QPoint &origin);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    ImageCanvas *createCanvas(const QImage &image);
    void activateCanvas(ImageCanvas *canvas);
    void changeFrame(int offset);
    void toggleScratch();
    void updateTools();
    void refresh();
    void updateOnion();
    void acceptImage();
    void publishImages();
    void chooseColor(bool secondary);
    void updateColors();
    void exportImage();
    void openImage(bool paste);
    void newImage();
    void applyOperation(ImageOperations::Operation operation, const QString &title);
    void configureGrid();
    void configureBackground();
    void drawText(const QPoint &position, bool secondary, bool editing);
    void paletteFile(bool save);
    QList<ImageCanvas *> m_frames;
    QList<QImage> m_originalImages;
    ImageCanvas *m_canvas = nullptr;
    ImageCanvas *m_scratch = nullptr;
    ImageCanvas *m_preview;
    QUndoStack *m_history;
    QScrollArea *m_scroll;
    ImagePalette *m_palette;
    QLabel *m_statusLabel;
    QLabel *m_toolLabel;
    QPushButton *m_foregroundButton;
    QPushButton *m_backgroundButton;
    QSpinBox *m_sizeSpin;
    QSpinBox *m_opacitySpin;
    QSpinBox *m_hardnessSpin;
    QSpinBox *m_toleranceSpin;
    QSpinBox *m_onionOpacity;
    QSpinBox *m_onionForward;
    QSpinBox *m_onionBackward;
    QComboBox *m_shapeStyle;
    QComboBox *m_arrowMode;
    QComboBox *m_textAlignment;
    QCheckBox *m_antialias;
    QCheckBox *m_colorOnly;
    QCheckBox *m_rightErase;
    QGroupBox *m_sizeGroup;
    QGroupBox *m_hardnessGroup;
    QGroupBox *m_toleranceGroup;
    QGroupBox *m_shapeGroup;
    QGroupBox *m_lineGroup;
    QGroupBox *m_fontGroup;
    QAction *m_previousAction;
    QAction *m_nextAction;
    QAction *m_scratchAction;
    QAction *m_gridAction;
    QList<QAction *> m_toolActions;
    QColor m_foreground = Qt::black, m_background = Qt::white;
    QColor m_checkerFirst = QColor(192, 192, 192), m_checkerSecond = QColor(240, 240, 240), m_gridColor = QColor(0, 0, 0, 100);
    QSize m_gridSize = QSize(1, 1);
    QPoint m_gridOffset;
    QPoint m_mousePosition;
    QPoint m_origin, m_originalOrigin;
    QFont m_textFont;
    QString m_text;
    QString m_resourceName;
    int m_frameIndex = 0, m_checkerSize = 8, m_previewZoom = 1;
    bool m_accepted = false, m_blend = true, m_hasOrigin = false;
    ImageCanvas::Tool m_tool = ImageCanvas::Tool::Brush;
};
#endif
