#ifndef QTGMS_SPRITEMASKWINDOW_H
#define QTGMS_SPRITEMASKWINDOW_H

#include "editorwindow.h"

class SpriteDocument;
class ImageCanvas;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QLabel;

class SpriteMaskWindow : public EditorWindow
{
    Q_OBJECT
public:
    explicit SpriteMaskWindow(SpriteDocument *document, QWidget *parent = nullptr);
private:
    void refresh();
    void applySettings();
    SpriteDocument *m_document;
    ImageCanvas *m_preview;
    QCheckBox *m_showMask;
    QCheckBox *m_separate;
    QComboBox *m_shape;
    QComboBox *m_boxMode;
    QSpinBox *m_tolerance;
    QSpinBox *m_left;
    QSpinBox *m_right;
    QSpinBox *m_top;
    QSpinBox *m_bottom;
    QLabel *m_frameLabel;
    int m_frameIndex = 0;
    bool m_refreshing = false;
};

#endif
