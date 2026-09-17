#include "shaderhighlighter.h"
#include "codeblockdata.h"
#include "codeeditorcolors.h"
#include <QRegularExpression>
#include <QStringList>

ShaderHighlighter::ShaderHighlighter(QTextDocument *document) : QSyntaxHighlighter(document), m_hlsl(false)
{
    m_keywordFormat.setForeground(CodeEditorColors::Keyword); m_typeFormat = m_keywordFormat;
    m_builtinFormat.setForeground(CodeEditorColors::BuiltinVariable);
    m_numberFormat.setForeground(CodeEditorColors::Value);
    m_stringFormat.setForeground(CodeEditorColors::Value);
    m_commentFormat.setForeground(CodeEditorColors::Comment);
    m_directiveFormat.setForeground(CodeEditorColors::Keyword); setLanguage(QStringLiteral("GLSLES"));
}
void ShaderHighlighter::setLanguage(const QString &language)
{
    m_hlsl = language.startsWith(QStringLiteral("HLSL")); m_keywords.clear(); m_types.clear();
    const QString common = QStringLiteral("if else for while do break continue return discard struct const in out inout uniform static switch case default true false");
    const QString specific = m_hlsl ? QStringLiteral("extern inline nointerpolation precise shared groupshared volatile row_major column_major register packoffset technique technique10 pass compile cbuffer tbuffer")
        : QStringLiteral("attribute varying precision lowp mediump highp invariant centroid flat smooth layout coherent volatile restrict readonly writeonly buffer shared");
    for (const QString &word : (common + QLatin1Char(' ') + specific).split(QLatin1Char(' '))) m_keywords.insert(word);
    const QString types = m_hlsl ? QStringLiteral("void bool int uint half float double vector matrix string texture texture2D textureCUBE sampler sampler1D sampler2D sampler3D samplerCUBE SamplerState SamplerComparisonState Texture1D Texture2D Texture3D TextureCube")
        : QStringLiteral("void bool int uint float double vec2 vec3 vec4 ivec2 ivec3 ivec4 bvec2 bvec3 bvec4 uvec2 uvec3 uvec4 mat2 mat3 mat4 mat2x3 mat2x4 mat3x2 mat3x4 mat4x2 mat4x3 sampler2D samplerCube sampler3D sampler2DShadow");
    for (const QString &word : types.split(QLatin1Char(' '))) m_types.insert(word);
    if (m_hlsl) for (const QString &base : {QStringLiteral("bool"), QStringLiteral("int"), QStringLiteral("uint"), QStringLiteral("half"), QStringLiteral("float"), QStringLiteral("double")}) {
        for (int i = 1; i <= 4; ++i) {
            m_types.insert(base + QString::number(i));
            for (int j = 1; j <= 4; ++j) m_types.insert(base + QString::number(i) + QLatin1Char('x') + QString::number(j));
        }
    }
    rehighlight();
}
void ShaderHighlighter::highlightBlock(const QString &text)
{
    auto *blockData = new CodeBlockData;
    // Incremental lexical colouring, not shader compilation or diagnostics.
    int state = qMax(0, previousBlockState()), position = 0;
    bool directive = state == 4;
    if (state == 4) state = 0;
    static const QRegularExpression Number(QStringLiteral("(?:0[xX][0-9a-fA-F]+|(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)[uUfFlL]*"));
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
            const QChar quote = state == 2 ? QLatin1Char('"') : state == 3 ? QLatin1Char('\'') : text.at(position++);
            bool closed = false;
            while (position < text.size()) {
                const QChar ch = text.at(position++);
                if (ch == QLatin1Char('\\')) { if (position < text.size()) ++position; }
                else if (ch == quote) { closed = true; break; }
            }
            state = !closed && text.endsWith(QLatin1Char('\\')) ? (quote == QLatin1Char('"') ? 2 : 3) : 0;
            blockData->exclude(start, position, continued, !closed, true);
            setFormat(start, position - start, m_stringFormat); continue;
        }
        if (text.mid(position, 2) == QStringLiteral("//")) {
            blockData->exclude(position, text.size(), false, true);
            setFormat(position, text.size() - position, m_commentFormat); break;
        }
        const QChar ch = text.at(position);
        if (ch == QLatin1Char('#') && text.left(position).trimmed().isEmpty()) directive = true;
        if (directive) { setFormat(position++, 1, m_directiveFormat); continue; }
        if (ch == QLatin1Char('{') || ch == QLatin1Char('}')) {
            setFormat(position++, 1, m_keywordFormat); continue;
        }
        if (ch.isLetter() || ch == QLatin1Char('_')) {
            while (++position < text.size() && (text.at(position).isLetterOrNumber() || text.at(position) == QLatin1Char('_'))) {}
            const QString word = text.mid(start, position - start);
            int next = position; while (next < text.size() && text.at(next).isSpace()) ++next;
            if (word == QStringLiteral("true") || word == QStringLiteral("false")) setFormat(start, position - start, m_numberFormat);
            else if (m_types.contains(word)) setFormat(start, position - start, m_typeFormat);
            else if (m_keywords.contains(word)) setFormat(start, position - start, m_keywordFormat);
            else if (word.startsWith(QStringLiteral("gm_")) || word.startsWith(QStringLiteral("gl_"))
                     || word.startsWith(QStringLiteral("MATRIX_")) || (m_hlsl && word.startsWith(QStringLiteral("SV_"))))
                setFormat(start, position - start, m_builtinFormat);
            else if (next < text.size() && text.at(next) == QLatin1Char('(')) setFormat(start, position - start, m_keywordFormat);
        } else if (ch.isDigit() || ch == QLatin1Char('.')) {
            const auto match = Number.match(text, position, QRegularExpression::NormalMatch, QRegularExpression::AnchoredMatchOption);
            if (match.hasMatch()) { position += match.capturedLength(); setFormat(start, position - start, m_numberFormat); }
            else ++position;
        } else ++position;
    }
    if (!state && directive && text.endsWith(QLatin1Char('\\'))) state = 4;
    setCurrentBlockState(state);
    if (text.isEmpty() && state == 1) blockData->exclude(0, 0, true, true);
    if (text.isEmpty() && (state == 2 || state == 3)) blockData->exclude(0, 0, true, true, true);
    blockData->collectTokens(text);
    setCurrentBlockUserData(blockData);
}
