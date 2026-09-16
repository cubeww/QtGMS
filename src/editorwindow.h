#ifndef QTGMS_EDITORWINDOW_H
#define QTGMS_EDITORWINDOW_H

#include <QMainWindow>

class QMenuBar;
class WindowTitleBar;

class EditorWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit EditorWindow(QWidget *parent = nullptr);
signals:
    void resourceNameActivated(const QString &name);

protected:
    QMenuBar *editorMenuBar() const;
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void updateFrameGeometry();
    void showSystemMenu(const QPoint &nativeScreenPosition);

    WindowTitleBar *m_titleBar;
    QMenuBar *m_editorMenuBar;
    QRectF m_frameRect;
};

#endif
