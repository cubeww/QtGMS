#ifndef QTGMS_CODESIGNATUREHELP_H
#define QTGMS_CODESIGNATUREHELP_H

#include "codecompletionitem.h"
#include <QObject>
#include <QHash>
#include <QVector>

class CodeEditor;
class QTimer;

class CodeSignatureHelp : public QObject
{
    Q_OBJECT
public:
    explicit CodeSignatureHelp(CodeEditor *editor);
    void setItems(const QVector<CodeCompletionItem> &items);
signals:
    void changed(const QString &text, int parameterStart, int parameterLength);
private:
    struct Parameter { int start; int length; };
    struct Signature { QString text; QVector<Parameter> parameters; bool variadic = false; };
    void update();
    void publish(const QString &text = QString(), int start = -1, int length = 0);
    CodeEditor *m_editor;
    QTimer *m_timer;
    QHash<QString, QVector<Signature>> m_signatures;
    QString m_text;
    int m_parameterStart = -1;
    int m_parameterLength = 0;
};

#endif
