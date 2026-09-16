#include "gmlhighlighter.h"
#include "codeblockdata.h"
#include "codeeditorcolors.h"
#include "project.h"
#include "gmlsymbols.h"
#include <QStringList>
#include <QRegularExpression>

static void collectGmlResourceNames(const QList<ResourceNode> &nodes, QSet<QString> &names)
{
    for (const ResourceNode &node : nodes) {
        if (node.isGroup) collectGmlResourceNames(node.children, names);
        else names.insert(node.name);
    }
}

GmlHighlighter::GmlHighlighter(QTextDocument *document) : QSyntaxHighlighter(document)
{
    const GmlSymbols &symbols = GmlSymbols::instance();
    m_keywords = symbols.keywords;
    m_constants = symbols.constants;
    m_variables = symbols.variables;
    m_functions = symbols.functions;
    m_keywordFormat.setForeground(CodeEditorColors::Keyword);
    m_constantFormat.setForeground(CodeEditorColors::Constant);
    m_variableFormat.setForeground(CodeEditorColors::BuiltinVariable);
    m_functionFormat.setForeground(CodeEditorColors::Function);
    m_stringFormat.setForeground(CodeEditorColors::Value);
    m_commentFormat.setForeground(CodeEditorColors::Comment);
    m_numberFormat.setForeground(CodeEditorColors::Value);
    m_resourceFormat.setForeground(CodeEditorColors::Resource);
}
void GmlHighlighter::setResources(const Project &project)
{
    QSet<QString> scripts, resources;
    collectGmlResourceNames(project.resources(ResourceType::Script), scripts);
    for (ResourceType type : {ResourceType::Sprite, ResourceType::Sound, ResourceType::Background,
                             ResourceType::Path, ResourceType::Shader, ResourceType::Font,
                             ResourceType::Timeline, ResourceType::Object, ResourceType::Room})
        collectGmlResourceNames(project.resources(type), resources);
    if (scripts == m_scripts && resources == m_resources) return;
    m_scripts = scripts; m_resources = resources; rehighlight();
}
void GmlHighlighter::highlightBlock(const QString &text)
{
    auto *blockData = new CodeBlockData;
    // State spans text blocks, so strings/comments cannot leak keyword colouring.
    // GML 1.4 strings use either quote and do not use C-style backslash escapes.
    int state = qMax(0, previousBlockState()); int position = 0;
    static const QRegularExpression Number(QStringLiteral("(?:\\$[0-9a-fA-F]+|0[xX][0-9a-fA-F]+|(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)"));
    while (position < text.size()) {
        const int start = position;
        if (state == 1 || (state == 0 && text.mid(position, 2) == QStringLiteral("/*"))) {
            const bool continued = state == 1;
            const int end = text.indexOf(QStringLiteral("*/"), position + (state == 1 ? 0 : 2));
            position = end < 0 ? text.size() : end + 2; state = end < 0 ? 1 : 0;
            blockData->exclude(start, position, continued, end < 0);
            setFormat(start, position - start, m_commentFormat); continue;
        }
        if (state == 2 || state == 3 || text.at(position) == QLatin1Char('"') || text.at(position) == QLatin1Char('\'')) {
            const bool continued = state == 2 || state == 3;
            const QChar quote = state == 2 ? QLatin1Char('"') : state == 3 ? QLatin1Char('\'') : text.at(position);
            const int end = text.indexOf(quote, position + (state ? 0 : 1));
            position = end < 0 ? text.size() : end + 1;
            state = end < 0 ? (quote == QLatin1Char('"') ? 2 : 3) : 0;
            blockData->exclude(start, position, continued, end < 0, true);
            setFormat(start, position - start, m_stringFormat); continue;
        }
        if (text.mid(position, 2) == QStringLiteral("//")) {
            blockData->exclude(position, text.size(), false, true);
            setFormat(position, text.size() - position, m_commentFormat); break;
        }
        const QChar ch = text.at(position);
        if (ch == QLatin1Char('{') || ch == QLatin1Char('}')) {
            setFormat(position++, 1, m_keywordFormat); continue;
        }
        if (ch.isLetter() || ch == QLatin1Char('_')) {
            while (++position < text.size() && (text.at(position).isLetterOrNumber() || text.at(position) == QLatin1Char('_'))) {}
            const QString word = text.mid(start, position - start);
            if (m_keywords.contains(word)) setFormat(start, position - start, m_keywordFormat);
            else if (m_constants.contains(word)) setFormat(start, position - start, m_constantFormat);
            else if (m_variables.contains(word)) setFormat(start, position - start, m_variableFormat);
            else if (m_functions.contains(word)) setFormat(start, position - start, m_functionFormat);
            else if (m_scripts.contains(word)) setFormat(start, position - start, m_functionFormat);
            else if (m_resources.contains(word)) setFormat(start, position - start, m_resourceFormat);
        } else if (ch.isDigit() || ch == QLatin1Char('$') || ch == QLatin1Char('.')) {
            const auto match = Number.match(text, position, QRegularExpression::NormalMatch, QRegularExpression::AnchoredMatchOption);
            if (match.hasMatch()) { position += match.capturedLength(); setFormat(start, position - start, m_numberFormat); }
            else ++position;
        } else ++position;
    }
    setCurrentBlockState(state);
    if (text.isEmpty() && state) blockData->exclude(0, 0, true, true);
    blockData->collectTokens(text);
    setCurrentBlockUserData(blockData);
}
