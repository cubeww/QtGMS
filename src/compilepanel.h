#ifndef QTGMS_COMPILEPANEL_H
#define QTGMS_COMPILEPANEL_H

#include <QDockWidget>
#include <QStringList>

class QPlainTextEdit;
class QTabWidget;

class CompilePanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit CompilePanel(QWidget *parent = nullptr);
    QSize sizeHint() const override;
    void setMessages(const QStringList &messages);
    void appendMessage(const QString &message);

private:
    QTabWidget *m_tabs;
    QPlainTextEdit *m_outputEdit;
};

#endif
