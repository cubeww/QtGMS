#include "scriptsource.h"
#include <QRegularExpression>

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
