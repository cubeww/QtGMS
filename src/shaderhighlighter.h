#ifndef QTGMS_SHADERHIGHLIGHTER_H
#define QTGMS_SHADERHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QSet>

class ShaderHighlighter : public QSyntaxHighlighter
{
public:
    explicit ShaderHighlighter(QTextDocument *document);
    void setLanguage(const QString &language);
protected:
    void highlightBlock(const QString &text) override;
private:
    QSet<QString> m_keywords;
    QSet<QString> m_types;
    bool m_hlsl;
    QTextCharFormat m_keywordFormat;
    QTextCharFormat m_typeFormat;
    QTextCharFormat m_builtinFormat;
    QTextCharFormat m_numberFormat;
    QTextCharFormat m_stringFormat;
    QTextCharFormat m_commentFormat;
    QTextCharFormat m_directiveFormat;
};

#endif
