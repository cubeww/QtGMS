#ifndef QTGMS_GMLHIGHLIGHTER_H
#define QTGMS_GMLHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QSet>
#include "gmldeclarations.h"

class Project;

// GML 1.4 presentation only; this is not a compiler or a syntax validator.
class GmlHighlighter : public QSyntaxHighlighter
{
public:
    explicit GmlHighlighter(QTextDocument *document);
    void setResources(const Project &project);
protected:
    void highlightBlock(const QString &text) override;
private:
    void updateDeclarations();
    QString m_declarationSource;
    GmlDeclarations m_declarations;
    QSet<QString> m_keywords;
    QSet<QString> m_constants;
    QSet<QString> m_variables;
    QSet<QString> m_functions;
    QSet<QString> m_scripts;
    QSet<QString> m_resources;
    QTextCharFormat m_keywordFormat;
    QTextCharFormat m_constantFormat;
    QTextCharFormat m_variableFormat;
    QTextCharFormat m_functionFormat;
    QTextCharFormat m_stringFormat;
    QTextCharFormat m_commentFormat;
    QTextCharFormat m_numberFormat;
    QTextCharFormat m_resourceFormat;
};

#endif
