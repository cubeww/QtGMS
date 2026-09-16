#ifndef QTGMS_IMAGEOPERATIONDIALOG_H
#define QTGMS_IMAGEOPERATIONDIALOG_H
#include "editordialog.h"
#include "imageoperations.h"
class QFormLayout;
class ImageCanvas;
class QTimer;
class ImageOperationDialog : public EditorDialog
{
    Q_OBJECT
public:
    ImageOperationDialog(const QImage &image, ImageOperations::Operation operation, const QString &title,
        const QColor &foreground, const QColor &background, QWidget *parent, bool hasOrigin = false);
    const ImageOperationSettings &settings() const { return m_settings; }
private:
    void createSizeControls(bool hasOrigin);
    void updatePreview();
    QImage m_image;
    ImageOperations::Operation m_operation;
    ImageOperationSettings m_settings;
    ImageCanvas *m_preview;
    QTimer *m_timer;
};
#endif
