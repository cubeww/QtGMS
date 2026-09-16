#include "codesignaturehelp.h"
#include "codeeditor.h"
#include "codeblockdata.h"
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>
#include <QStringList>

class BackwardCodeTokens
{
public:
    BackwardCodeTokens(QTextDocument *document, int position) : m_block(document->findBlock(position))
    {
        const auto *data = dynamic_cast<const CodeBlockData *>(m_block.userData());
        m_index = data ? data->tokens.size() - 1 : -1;
        while (m_index >= 0 && data->tokens.at(m_index).position >= position - m_block.position()) --m_index;
    }
    bool previous(CodeBlockData::Token &token, QTextBlock &block)
    {
        while (m_block.isValid()) {
            const auto *data = dynamic_cast<const CodeBlockData *>(m_block.userData());
            if (!data) return false;
            if (m_index >= 0) { token = data->tokens.at(m_index--); block = m_block; return true; }
            m_block = m_block.previous();
            data = m_block.isValid() ? dynamic_cast<const CodeBlockData *>(m_block.userData()) : nullptr;
            m_index = data ? data->tokens.size() - 1 : -1;
        }
        return false;
    }
private:
    QTextBlock m_block;
    int m_index;
};

CodeSignatureHelp::CodeSignatureHelp(CodeEditor *editor)
    : QObject(editor), m_editor(editor), m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    // Read lexical metadata after highlighting has finished processing the edit.
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this] { if (m_enabled) m_timer->start(0); });
    connect(editor, &QPlainTextEdit::textChanged, this, [this] { if (m_enabled) m_timer->start(0); });
    connect(m_timer, &QTimer::timeout, this, &CodeSignatureHelp::update);
}

void CodeSignatureHelp::setItems(const QVector<CodeCompletionItem> &items)
{
    m_signatures.clear();
    for (const auto &item : items) {
        if (item.kind != CodeCompletionItem::Kind::Function && item.kind != CodeCompletionItem::Kind::Script) continue;
        for (const QString &text : item.detail.split(QLatin1Char('\n'))) {
            const int open = text.indexOf(QLatin1Char('(')), close = text.lastIndexOf(QLatin1Char(')'));
            if (open < 0 || close <= open || text.left(open).trimmed() != item.name) continue;
            Signature signature; signature.text = text;
            if (!text.mid(open + 1, close - open - 1).trimmed().isEmpty()) {
                int start = open + 1;
                for (int i = start; i <= close; ++i) {
                    if (i != close && text.at(i) != QLatin1Char(',')) continue;
                    int left = start, right = i;
                    while (left < right && text.at(left).isSpace()) ++left;
                    while (right > left && text.at(right - 1).isSpace()) --right;
                    signature.parameters.append({left, right - left}); start = i + 1;
                }
                const auto &last = signature.parameters.last();
                signature.variadic = text.mid(last.start, last.length).contains(QStringLiteral("..."));
            }
            m_signatures[item.name].append(signature);
        }
    }
    m_timer->start(0);
}

void CodeSignatureHelp::publish(const QString &text, int start, int length)
{
    if (text == m_text && start == m_parameterStart && length == m_parameterLength) return;
    m_text = text; m_parameterStart = start; m_parameterLength = length;
    emit changed(text, start, length);
}

void CodeSignatureHelp::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (enabled) m_timer->start(0);
    else { m_timer->stop(); publish(); }
}

void CodeSignatureHelp::update()
{
    if (!m_enabled || m_signatures.isEmpty() || m_editor->textCursor().hasSelection()) { publish(); return; }
    BackwardCodeTokens iterator(m_editor->document(), m_editor->textCursor().position());
    CodeBlockData::Token token; QTextBlock block;
    QVector<QChar> closing;
    int argument = 0;
    while (iterator.previous(token, block)) {
        const QChar kind = token.kind;
        if (QStringLiteral(")]}").contains(kind)) { closing.append(kind); continue; }
        if (QStringLiteral("([{").contains(kind)) {
            if (!closing.isEmpty()) {
                const QChar expected = kind == QLatin1Char('(') ? QLatin1Char(')')
                                     : kind == QLatin1Char('[') ? QLatin1Char(']') : QLatin1Char('}');
                if (closing.last() != expected) { publish(); return; }
                closing.removeLast(); continue;
            }
            if (kind == QLatin1Char('{')) break;
            if (kind == QLatin1Char('(')) {
                auto beforeCall = iterator;
                CodeBlockData::Token nameToken; QTextBlock nameBlock;
                if (beforeCall.previous(nameToken, nameBlock) && nameToken.kind == QLatin1Char('i')) {
                    const QString name = nameBlock.text().mid(nameToken.position, nameToken.length);
                    const auto found = m_signatures.constFind(name);
                    // Do not attribute an unknown inner call's arguments to an outer function.
                    if (found == m_signatures.constEnd()) { publish(); return; }
                    const auto &signatures = found.value();
                    const Signature *signature = &signatures.first();
                    for (const auto &candidate : signatures) {
                        if (argument < candidate.parameters.size() || candidate.variadic) { signature = &candidate; break; }
                    }
                    int index = argument;
                    if (signature->variadic) index = qMin(index, signature->parameters.size() - 1);
                    if (index < signature->parameters.size()) {
                        const auto &parameter = signature->parameters.at(index);
                        publish(signature->text, parameter.start, parameter.length);
                    } else publish(signature->text);
                    return;
                }
            }
            // Commas inside grouping parentheses or an array subscript do not
            // advance the argument of the enclosing function.
            argument = 0;
        } else if (closing.isEmpty()) {
            if (kind == QLatin1Char(';')) break;
            if (kind == QLatin1Char(',')) ++argument;
        }
    }
    publish();
}
