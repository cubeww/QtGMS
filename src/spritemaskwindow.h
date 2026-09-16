#ifndef QTGMS_SPRITEMASKWINDOW_H
#define QTGMS_SPRITEMASKWINDOW_H

#include "editorwindow.h"

class SpriteDocument;
class ImageCanvas;
class QCheckBox;
class QButtonGroup;
class QSlider;
class QSpinBox;
class QLabel;
class QRubberBand;

class SpriteMaskWindow : public EditorWindow
{
    Q_OBJECT
public:
    explicit SpriteMaskWindow(SpriteDocument *document, QWidget *parent = nullptr);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void refresh();
    void applySettings();
    void updateBoxSelection(const QPoint &position);
    void updateBoxOutline();
    SpriteDocument *m_document;
    ImageCanvas *m_preview;
    QRubberBand *m_boxOutline;
    QCheckBox *m_showMask;
    QCheckBox *m_separate;
    QButtonGroup *m_shape;
    QButtonGroup *m_boxMode;
    QSpinBox *m_tolerance;
    QSlider *m_toleranceSlider;
    QSpinBox *m_left;
    QSpinBox *m_right;
    QSpinBox *m_top;
    QSpinBox *m_bottom;
    QLabel *m_frameLabel;
    QLabel *m_dimensionsLabel;
    QLabel *m_countLabel;
    QWidget *m_frameNavigation;
    int m_frameIndex = 0;
    bool m_refreshing = false;
    bool m_selectingBox = false;
    QPoint m_boxStart;
    QRect m_selectedBox;
};

#endif
