#ifndef QTGMS_WINDOWTITLEBAR_H
#define QTGMS_WINDOWTITLEBAR_H

#include <QWidget>

class QToolButton;

class WindowTitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit WindowTitleBar(QWidget *parent = nullptr);
    void updateWindowState();
    bool containsCaption(const QPoint &position) const;
    bool containsSystemMenu(const QPoint &position) const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int captionRight() const;
    QToolButton *m_minimizeButton;
    QToolButton *m_maximizeButton;
    QToolButton *m_closeButton;
};

#endif
