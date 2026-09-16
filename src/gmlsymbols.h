#ifndef QTGMS_GMLSYMBOLS_H
#define QTGMS_GMLSYMBOLS_H

#include "codecompletionitem.h"
#include <QSet>
#include <QVector>
#include <QObject>
#include <QHash>
#include <QDateTime>

class Project;
class TextFileDocument;

// One embedded catalogue shared by syntax colouring and completion.
class GmlSymbols : public QObject
{
    Q_OBJECT
public:
    static GmlSymbols &instance();
    static QVector<CodeCompletionItem> completionItems(const Project &project);
    void watchScript(TextFileDocument *document);
    QVector<CodeCompletionItem> scriptItems(const QString &name, const QString &path);
    QSet<QString> keywords;
    QSet<QString> constants;
    QSet<QString> variables;
    QSet<QString> functions;
    QVector<CodeCompletionItem> items;
signals:
    void scriptSignaturesChanged();
private:
    GmlSymbols();
    struct ScriptHeader {
        QDateTime modified;
        qint64 size = -1;
        QString text;
    };
    QHash<QString, ScriptHeader> m_scriptHeaders;
    QHash<TextFileDocument *, QString> m_openScripts;
};

#endif
