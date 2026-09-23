#ifndef QTGMS_CODESNIPPETS_H
#define QTGMS_CODESNIPPETS_H
#include <QString>
#include <QVector>

struct CodeSnippet
{
    QString name;
    // Tabs represent indentation levels; pipes delimit the initial selection.
    QString text;
};

class CodeSnippets
{
public:
    static const QVector<CodeSnippet> &items();
};
#endif
