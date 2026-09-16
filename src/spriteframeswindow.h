#ifndef QTGMS_SPRITEFRAMESWINDOW_H
#define QTGMS_SPRITEFRAMESWINDOW_H

#include "editorwindow.h"
#include "imageoperations.h"
#include "spriteanimation.h"
#include <QPointer>

class SpriteDocument;
class ImageCanvas;
class ImageEditorWindow;
class QListWidget;
class QLabel;
class QTimer;
class QAction;
struct SpriteFrame;

class SpriteFramesWindow : public EditorWindow
{
    Q_OBJECT
public:
    explicit SpriteFramesWindow(SpriteDocument *document, QWidget *parent = nullptr);
    void importFrames(bool replace);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void refresh();
    void refreshPreview();
    void editFrame();
    void newSprite();
    void insertFrame(bool append = false);
    void deleteFrame();
    void moveFrame(int direction);
    void importStrip(bool replace);
    void exportStrip();
    void copyFrames(bool cut);
    void pasteFrames();
    void insertFrames(const QList<SpriteFrame> &frames, bool replace, bool append, const QString &title);
    void eraseFrames();
    void setTransparencyBackground();
    void applyOperation(ImageOperations::Operation operation, const QString &title);
    void changeAnimationLength(bool stretch);
    void reverseAnimation(bool append);
    void cycleFrames(int direction);
    void createAnimation(SpriteAnimation::Kind kind, int direction = 0);
    void blendAnimation(bool morph);
    void premultiplyAlpha();
    QList<int> selectedRows() const;
    void updateActions();
    SpriteDocument *m_document;
    QListWidget *m_frames;
    ImageCanvas *m_preview;
    QLabel *m_status;
    QTimer *m_timer;
    QPointer<ImageEditorWindow> m_imageEditor;
    QList<QAction *> m_selectionActions, m_imageActions;
    QAction *m_pasteAction;
    QAction *m_moveLeftAction;
    QAction *m_moveRightAction;
    QColor m_checkerFirst = QColor(192, 192, 192), m_checkerSecond = QColor(128, 128, 128);
    QColor m_previewColor = QColor(56, 56, 56);
    QImage m_previewBackground;
    bool m_stretchBackground = false;
    int m_checkerSize = 16;
    int m_previewFrame = 0;
};

#endif
