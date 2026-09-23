#include "codeeditor.h"
#include <QMenu>
#include <QTextBlock>

QString CodeEditor::codeSnippet(const QString &keyword) const
{
    for (const auto &snippet : m_snippets)
        if (snippet.name == keyword) return snippet.text;
    return QString();
}

void CodeEditor::insertCodeSnippet(const QString &text)
{
    if (isReadOnly()) return;
    QTextCursor cursor = textCursor();
    const int start = cursor.selectionStart();
    const QTextBlock block = document()->findBlock(start);
    const QString line = block.text();
    const int column = start - block.position();
    int leading = 0;
    while (leading < column && line.at(leading).isSpace()) ++leading;
    const QString indent = line.left(leading);
    QString expanded = text;
    expanded.replace(QLatin1Char('\t'), QString(m_indentSize, QLatin1Char(' ')));
    expanded.replace(QStringLiteral("\n"), QLatin1Char('\n') + indent);
    const int selectionStart = expanded.indexOf(QLatin1Char('|'));
    const int selectionEnd = expanded.indexOf(QLatin1Char('|'), selectionStart + 1);
    if (selectionStart >= 0 && selectionEnd > selectionStart) {
        expanded.remove(selectionEnd, 1);
        expanded.remove(selectionStart, 1);
    }
    cursor.beginEditBlock();
    cursor.insertText(expanded);
    if (selectionStart >= 0 && selectionEnd > selectionStart) {
        cursor.setPosition(start + selectionStart);
        cursor.setPosition(start + selectionEnd - 1, QTextCursor::KeepAnchor);
    }
    cursor.endEditBlock();
    setTextCursor(cursor); ensureCursorVisible();
}

void CodeEditor::showCodeSnippets()
{
    if (isReadOnly() || !hasCodeSnippets() || !isCodePosition(textCursor().selectionStart())) return;
    QMenu menu(this);
    const auto &snippets = m_snippets;
    for (int index = 0; index < snippets.size(); ++index) {
        const QString label = index < 10 ? QStringLiteral("&%1 - %2").arg((index + 1) % 10).arg(snippets.at(index).name)
                                        : snippets.at(index).name;
        auto *action = menu.addAction(label);
        action->setData(index);
    }
    const auto *selected = menu.exec(viewport()->mapToGlobal(cursorRect().bottomLeft()));
    if (!selected) return;
    const auto &snippet = snippets.at(selected->data().toInt());
    // F2 also expands a keyword already typed immediately before the caret.
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection()) {
        cursor.movePosition(QTextCursor::StartOfWord, QTextCursor::KeepAnchor);
        const QString word = cursor.selectedText();
        if (!word.isEmpty() && (word == snippet.name || snippet.name.startsWith(word + QLatin1Char('-'))
            || snippet.name.startsWith(word + QLatin1Char(' ')))) setTextCursor(cursor);
    }
    insertCodeSnippet(snippet.text);
}
