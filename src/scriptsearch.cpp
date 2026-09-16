#include "scriptsearch.h"
#include "actionxml.h"
#include "gmlsymbols.h"
#include <QObject>

static bool isSearchWord(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

ScriptSearch::ScriptSearch(const Project &project)
{
    const auto &symbols = GmlSymbols::instance();
    for (const auto &name : symbols.variables) m_symbols.insert(name, ScriptSearchFilter::Variable);
    for (const auto &name : symbols.constants) m_symbols.insert(name, ScriptSearchFilter::Constant);
    for (const auto &name : symbols.functions) m_symbols.insert(name, ScriptSearchFilter::Function);
    const ResourceType types[] = { ResourceType::Script, ResourceType::Sound, ResourceType::Sprite,
        ResourceType::Shader, ResourceType::Timeline, ResourceType::Room, ResourceType::Object,
        ResourceType::Path, ResourceType::Font, ResourceType::Extension };
    const ScriptSearchFilter filters[] = { ScriptSearchFilter::Script, ScriptSearchFilter::Sound,
        ScriptSearchFilter::Sprite, ScriptSearchFilter::Shader, ScriptSearchFilter::Timeline,
        ScriptSearchFilter::Room, ScriptSearchFilter::Object, ScriptSearchFilter::Path,
        ScriptSearchFilter::Font, ScriptSearchFilter::Extension };
    for (int i = 0; i < 10; ++i)
        for (const auto &resource : ActionXml::resourceList(project, types[i]))
            m_symbols.insert(resource.name, filters[i]);
}

void ScriptSearch::addConstant(const QString &name)
{
    m_symbols.insert(name, ScriptSearchFilter::Constant);
}

void ScriptSearch::search(const ScriptSearchSource &source, const ScriptSearchOptions &options,
                          QVector<ScriptSearchMatch> &matches) const
{
    if (options.text.isEmpty() || !(options.scopes & (1u << int(source.scope))) || !options.filters) return;
    const QString &text = source.text;
    // Mark comments and quoted strings without changing source offsets. GML 1.4
    // quotes do not use backslash escapes; shader strings do.
    QVector<quint8> regions(text.size(), 0);
    if (source.code) {
        for (int i = 0; i < text.size();) {
            const int start = i;
            int kind = 0;
            if (text.midRef(i, 2) == QLatin1String("//")) {
                kind = 1;
                const int end = text.indexOf(QLatin1Char('\n'), i);
                i = end < 0 ? text.size() : end;
            } else if (text.midRef(i, 2) == QLatin1String("/*")) {
                kind = 1;
                const int end = text.indexOf(QStringLiteral("*/"), i + 2);
                i = end < 0 ? text.size() : end + 2;
            } else if (text.at(i) == QLatin1Char('"') || text.at(i) == QLatin1Char('\'')) {
                kind = 2;
                const QChar quote = text.at(i++);
                while (i < text.size()) {
                    if (source.type == ResourceType::Shader && text.at(i) == QLatin1Char('\\')) {
                        i = qMin(i + 2, text.size());
                    } else if (text.at(i++) == quote) break;
                }
            } else { ++i; }
            if (kind) for (int p = start; p < i; ++p) regions[p] = quint8(kind);
        }
    }
    const auto sensitivity = options.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    int position = 0, line = 1, lineStart = 0;
    while ((position = text.indexOf(options.text, position, sensitivity)) >= 0) {
        const int end = position + options.text.size();
        bool allowed = true;
        if (options.ignoreComments)
            for (int i = position; i < end; ++i) if (regions.at(i) == 1) { allowed = false; break; }
        if (options.wholeWord && ((position > 0 && isSearchWord(text.at(position - 1)))
            || (end < text.size() && isSearchWord(text.at(end))))) allowed = false;
        int wordStart = position, wordEnd = end;
        if (isSearchWord(text.at(position)))
            while (wordStart > 0 && isSearchWord(text.at(wordStart - 1))) --wordStart;
        if (isSearchWord(text.at(end - 1)))
            while (wordEnd < text.size() && isSearchWord(text.at(wordEnd))) ++wordEnd;
        const QString word = text.mid(wordStart, wordEnd - wordStart);
        auto symbol = m_symbols.constFind(word);
        ScriptSearchFilter filter = ScriptSearchFilter::Variable;
        if (symbol != m_symbols.cend()) filter = symbol.value();
        else if (regions.at(position) == 2 || (!word.isEmpty() && word.at(0).isDigit())) filter = ScriptSearchFilter::Constant;
        else {
            int next = wordEnd;
            while (next < text.size() && text.at(next).isSpace()) ++next;
            if (next < text.size() && text.at(next) == QLatin1Char('(')) filter = ScriptSearchFilter::Function;
        }
        if (!(options.filters & (1u << int(filter)))) allowed = false;
        if (allowed) {
            int newline;
            while ((newline = text.indexOf(QLatin1Char('\n'), lineStart)) >= 0 && newline < position) {
                ++line;
                lineStart = newline + 1;
            }
            const int lineEnd = text.indexOf(QLatin1Char('\n'), position);
            ScriptSearchMatch match;
            match.source = source;
            match.offset = position;
            match.length = options.text.size();
            match.line = line;
            match.column = position - lineStart + 1;
            match.found = word;
            match.context = text.mid(lineStart, (lineEnd < 0 ? text.size() : lineEnd) - lineStart);
            match.description = QObject::tr("Found \"%1\" in %2 at line %3, position %4")
                .arg(word, source.description).arg(line).arg(match.column);
            matches.append(match);
        }
        position = end;
    }
}
