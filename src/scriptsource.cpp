#include "scriptsource.h"
#include <QRegularExpression>

QStringList ScriptSource::stringLiterals(const QString &source)
{
    QStringList result;
    for (int position = 0; position < source.size();) {
        if (source.midRef(position, 2) == QLatin1String("//")) {
            const int end = source.indexOf(QLatin1Char('\n'), position + 2);
            position = end < 0 ? source.size() : end + 1;
        } else if (source.midRef(position, 2) == QLatin1String("/*")) {
            const int end = source.indexOf(QStringLiteral("*/"), position + 2);
            position = end < 0 ? source.size() : end + 2;
        } else if (source.at(position) == QLatin1Char('"') || source.at(position) == QLatin1Char('\'')) {
            // GML 1.4 strings can span lines and do not interpret backslash escapes.
            const QChar quote = source.at(position++);
            const int end = source.indexOf(quote, position);
            if (end < 0) break;
            result.append(source.mid(position, end - position));
            position = end + 1;
        } else {
            ++position;
        }
    }
    return result;
}

bool ScriptSource::hasDefinitions(const QString &source)
{
    // GMS 1.4 enables multi-script files when the first line is #define.
    return source.startsWith(QStringLiteral("#define"));
}

QVector<ScriptSection> ScriptSource::sections(const QString &source, const QString &resourceName)
{
    QVector<ScriptSection> result;
    if (hasDefinitions(source)) {
        static const QRegularExpression definition(QStringLiteral("^[\\t ]*#define[\\t ]+([^\\s]+)[^\\r\\n]*(?:\\r?\\n|$)"),
                                                   QRegularExpression::MultilineOption);
        auto matches = definition.globalMatch(source);
        int previousEnd = 0;
        while (matches.hasNext()) {
            const auto match = matches.next();
            if (!result.isEmpty()) result.last().code = source.mid(previousEnd, match.capturedStart() - previousEnd);
            ScriptSection section;
            section.name = match.captured(1);
            section.headerOffset = match.capturedStart();
            section.offset = match.capturedEnd();
            result.append(section);
            previousEnd = match.capturedEnd();
        }
        if (!result.isEmpty()) result.last().code = source.mid(previousEnd);
    }
    if (result.isEmpty()) {
        ScriptSection section;
        section.name = resourceName;
        section.code = source;
        result.append(section);
    }
    return result;
}

QString ScriptSource::combine(const QVector<ScriptSection> &sections, bool definitions)
{
    QString result;
    for (const auto &section : sections) {
        if (definitions) {
            if (!result.isEmpty() && !result.endsWith(QLatin1Char('\n'))) result += QLatin1Char('\n');
            result += QStringLiteral("#define ") + section.name + QLatin1Char('\n');
        }
        result += section.code;
    }
    return result;
}
