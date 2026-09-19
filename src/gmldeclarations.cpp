#include "gmldeclarations.h"
#include <QRegularExpression>
#include <QVector>

struct DeclarationToken
{
    QString text;
    int position;
    bool identifier;
};

GmlDeclarations GmlDeclarations::fromCode(const QString &source)
{
    QVector<DeclarationToken> tokens;
    // GML 1.4 permits multiline quoted strings and has no backslash escapes.
    // Consume complete comments/strings before looking for declarations.
    for (int i = 0; i < source.size();) {
        const int start = i;
        const QChar ch = source.at(i);
        if (ch.isSpace()) { ++i; continue; }
        if (source.midRef(i, 2) == QLatin1String("//")) {
            const int end = source.indexOf(QLatin1Char('\n'), i + 2);
            i = end < 0 ? source.size() : end; continue;
        }
        if (source.midRef(i, 2) == QLatin1String("/*")) {
            const int end = source.indexOf(QStringLiteral("*/"), i + 2);
            i = end < 0 ? source.size() : end + 2; continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            const int end = source.indexOf(ch, i + 1);
            i = end < 0 ? source.size() : end + 1;
            tokens.append({QStringLiteral("<string>"), start, false}); continue;
        }
        const bool identifier = ch.isLetter() || ch == QLatin1Char('_');
        ++i;
        if (identifier || ch.isDigit())
            while (i < source.size() && (source.at(i).isLetterOrNumber() || source.at(i) == QLatin1Char('_'))) ++i;
        tokens.append({source.mid(start, i - start), start, identifier});
    }
    GmlDeclarations result;
    static const QRegularExpression Macro(QStringLiteral("^#[ \\t]*macro[ \\t]+([A-Za-z_][A-Za-z_0-9]*)\\b"));
    for (int i = 0; i < tokens.size(); ++i) {
        const auto &token = tokens.at(i);
        if (token.text == QLatin1String("#")) {
            const int lineStart = token.position == 0 ? 0 : source.lastIndexOf(QLatin1Char('\n'), token.position - 1) + 1;
            if (!source.mid(lineStart, token.position - lineStart).trimmed().isEmpty()) continue;
            const auto match = Macro.match(source.mid(token.position));
            if (match.hasMatch()) {
                result.names.insert(match.captured(1));
                result.positions.insert(token.position + match.capturedStart(1));
            }
        }
        if (token.text != QLatin1String("enum") || i + 2 >= tokens.size()
            || !tokens.at(i + 1).identifier || tokens.at(i + 2).text != QLatin1String("{")) continue;
        const QString name = tokens.at(i + 1).text;
        result.names.insert(name); result.positions.insert(tokens.at(i + 1).position);
        int braces = 1, parentheses = 0, brackets = 0;
        bool member = true;
        for (i += 3; i < tokens.size(); ++i) {
            const auto &entry = tokens.at(i);
            if (entry.text == QLatin1String("}")) { if (!--braces) break; }
            else if (entry.text == QLatin1String("{")) ++braces;
            else if (entry.text == QLatin1String("(")) ++parentheses;
            else if (entry.text == QLatin1String(")")) parentheses = qMax(0, parentheses - 1);
            else if (entry.text == QLatin1String("[")) ++brackets;
            else if (entry.text == QLatin1String("]")) brackets = qMax(0, brackets - 1);
            else if (braces == 1 && !parentheses && !brackets) {
                if (entry.text == QLatin1String(",")) member = true;
                else if (member && entry.identifier) {
                    result.names.insert(name + QLatin1Char('.') + entry.text);
                    result.positions.insert(entry.position); member = false;
                }
            }
        }
    }
    return result;
}
