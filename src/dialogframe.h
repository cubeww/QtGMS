#ifndef QTGMS_DIALOGFRAME_H
#define QTGMS_DIALOGFRAME_H
#include <QObject>
#include <QPoint>
class QDialog;
class QWidget;
class QSizeGrip;
class WindowTitleBar;
// Shared frame for application forms and Qt's standard dialog widgets.
class DialogFrame : public QObject
{
public:
    explicit DialogFrame(QDialog *dialog);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void updateGeometry();
    QDialog *m_dialog;
    WindowTitleBar *m_titleBar;
    QWidget *m_border;
    QSizeGrip *m_sizeGrip;
    QPoint m_dragOffset;
    bool m_dragging = false;
};
#endif
