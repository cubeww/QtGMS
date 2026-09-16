#ifndef QTGMS_SPRITEPROPERTIESWINDOW_H
#define QTGMS_SPRITEPROPERTIESWINDOW_H

#include "resourceeditorwindow.h"
#include <QPointer>
#include <QStringList>

class SpriteDocument;
class SpriteFramesWindow;
class SpriteMaskWindow;
class ImageCanvas;
class QLabel;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QPushButton;

class SpritePropertiesWindow : public ResourceEditorWindow
{
    Q_OBJECT
public:
    SpritePropertiesWindow(SpriteDocument *document, const QStringList &textureGroups, QWidget *parent = nullptr);
    void setTextureGroups(const QStringList &groups);
    bool save() override;
    QString filePath() const override;
    void relocate(const QString &oldDirectory, const QString &newDirectory) override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void refresh();
    void applySettings();
    void editSprite();
    SpriteDocument *m_document;
    ImageCanvas *m_preview;
    QLabel *m_dimensions;
    QLabel *m_count;
    QLabel *m_frameLabel;
    QPushButton *m_previousFrameButton;
    QPushButton *m_nextFrameButton;
    QSpinBox *m_xOrigin;
    QSpinBox *m_yOrigin;
    QCheckBox *m_precise;
    QCheckBox *m_separate;
    QLabel *m_modifiedMaskLabel;
    QCheckBox *m_horizontal;
    QCheckBox *m_vertical;
    QCheckBox *m_for3D;
    QComboBox *m_textureGroup;
    QPointer<SpriteFramesWindow> m_framesWindow;
    QPointer<SpriteMaskWindow> m_maskWindow;
    int m_frameIndex = 0;
    bool m_refreshing = false;
};

#endif
