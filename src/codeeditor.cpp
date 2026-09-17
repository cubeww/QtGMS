#include "codeeditor.h"
#include "codeeditorcolors.h"
#include "codecompletion.h"
#include "codesignaturehelp.h"
#include "codeblockdata.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTimer>
#include <QWheelEvent>

class CodeLineNumberArea : public QWidget
{
public:
    explicit CodeLineNumberArea(CodeEditor *editor) : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override { return QSize(m_editor->lineNumberWidth(), 0); }
protected:
    void paintEvent(QPaintEvent *event) override { m_editor->paintLineNumbers(event); }
private:
    CodeEditor *m_editor;
};

CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent), m_lineNumbers(new CodeLineNumberArea(this)), m_indentSize(4),
      m_signatureHelp(new CodeSignatureHelp(this))
{
    connect(m_signatureHelp, &CodeSignatureHelp::changed, this, &CodeEditor::signatureHelpChanged);
    setLineWrapMode(QPlainTextEdit::NoWrap); setFrameShape(QFrame::NoFrame);
    QFont codeFont(QStringLiteral("Courier New")); codeFont.setStyleHint(QFont::Monospace);
    codeFont.setPixelSize(13); setFont(codeFont); setCodePixelSize(13);
    QPalette colors = palette(); colors.setColor(QPalette::Base, CodeEditorColors::Background);
    colors.setColor(QPalette::Text, CodeEditorColors::Text);
    colors.setColor(QPalette::Highlight, CodeEditorColors::Selection);
    colors.setColor(QPalette::HighlightedText, CodeEditorColors::Background); setPalette(colors);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateGutter(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        if (dy) m_lineNumbers->scroll(0, dy);
        else m_lineNumbers->update(0, rect.y(), m_lineNumbers->width(), rect.height());
        if (rect.contains(viewport()->rect())) updateGutter();
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::updateHighlights);
    connect(this, &QPlainTextEdit::textChanged, this, [this] {
        // Wait until the syntax highlighter has refreshed all affected blocks.
        if (m_highlightsPending) return;
        m_highlightsPending = true;
        QTimer::singleShot(0, this, [this] { m_highlightsPending = false; updateHighlights(); });
    });
    connect(&CodeEditorSettings::instance(), &CodeEditorSettings::changed, this, &CodeEditor::applySettings);
    applySettings();
}
void CodeEditor::applySettings()
{
    const auto options = CodeEditorSettings::instance().options();
    const bool fontChanged = options.fontFamily != m_options.fontFamily || options.fontPixelSize != m_options.fontPixelSize;
    m_options = options;
    m_indentSize = options.indentSize;
    if (fontChanged) {
        QFont codeFont(options.fontFamily);
        codeFont.setStyleHint(QFont::Monospace);
        codeFont.setPixelSize(options.fontPixelSize);
        setFont(codeFont);
        document()->setDefaultFont(codeFont);
    }
    setTabStopWidth(fontMetrics().width(QLatin1Char(' ')) * m_indentSize);
    if (m_completion) m_completion->configure(options.automaticCompletion, options.completionDelay);
    m_signatureHelp->setEnabled(options.functionHelp);
    updateGutter(); updateHighlights();
}
void CodeEditor::setCodePixelSize(int size)
{
    QFont codeFont = font(); codeFont.setPixelSize(qBound(8, size, 32)); setFont(codeFont);
    setTabStopWidth(fontMetrics().width(QLatin1Char(' ')) * m_indentSize); updateGutter();
}
int CodeEditor::lineNumberWidth() const
{ return m_options.lineNumbers ? 12 + fontMetrics().width(QLatin1Char('9')) * qMax(2, QString::number(blockCount()).size()) : 0; }
void CodeEditor::updateGutter()
{
    const int width = lineNumberWidth(); setViewportMargins(width, 0, 0, 0);
    m_lineNumbers->setVisible(m_options.lineNumbers);
    const QRect content = contentsRect(); m_lineNumbers->setGeometry(content.left(), content.top(), width, content.height());
    m_lineNumbers->update();
}
void CodeEditor::resizeEvent(QResizeEvent *event)
{ QPlainTextEdit::resizeEvent(event); updateGutter(); }

static int codeIndentColumns(const QTextBlock &block, int indentSize, bool &blank)
{
    const QString text = block.text();
    int column = 0, position = 0;
    while (position < text.size()) {
        const QChar character = text.at(position);
        if (character == QLatin1Char('\t')) column += indentSize - column % indentSize;
        else if (character == QLatin1Char(' ')) ++column;
        else break;
        ++position;
    }
    blank = position == text.size();
    return column;
}

static int adjacentCodeIndent(QTextBlock block, int indentSize, bool forward)
{
    while (block.isValid()) {
        bool blank;
        const int indent = codeIndentColumns(block, indentSize, blank);
        if (!blank) return indent;
        block = forward ? block.next() : block.previous();
    }
    return 0;
}

void CodeEditor::paintEvent(QPaintEvent *event)
{
    QPlainTextEdit::paintEvent(event);
    if (!m_options.indentGuides) return;
    struct GuideLine { QTextBlock block; int columns; bool blank; int top; int height; };
    QVector<GuideLine> lines;
    const int cursorLine = textCursor().blockNumber();
    int cursorIndex = -1;
    for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
        const int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        if (top > viewport()->height()) break;
        if (!block.isVisible()) continue;
        bool blank;
        const int columns = codeIndentColumns(block, m_indentSize, blank);
        if (block.blockNumber() == cursorLine) cursorIndex = lines.size();
        lines.append({block, columns, blank, top, qRound(blockBoundingRect(block).height())});
    }
    if (lines.isEmpty()) return;

    // Infer a blank run's indentation once, including its neighbours just
    // outside the viewport, so scrolling does not break its vertical guides.
    for (int i = 0; i < lines.size();) {
        if (!lines.at(i).blank) { ++i; continue; }
        int end = i + 1;
        while (end < lines.size() && lines.at(end).blank) ++end;
        const int before = i > 0 ? lines.at(i - 1).columns
            : adjacentCodeIndent(lines.at(i).block.previous(), m_indentSize, false);
        const int after = end < lines.size() ? lines.at(end).columns
            : adjacentCodeIndent(lines.at(end - 1).block.next(), m_indentSize, true);
        for (int j = i; j < end; ++j) lines[j].columns = qMax(lines.at(j).columns, qMax(before, after));
        i = end;
    }

    int activeLevel = 0, activeStart = -1, activeEnd = -1;
    if (cursorIndex >= 0) {
        activeLevel = (lines.at(cursorIndex).columns + m_indentSize - 1) / m_indentSize;
        int anchor = cursorIndex;
        int next = cursorIndex + 1;
        while (next < lines.size() && lines.at(next).blank) ++next;
        if (!lines.at(cursorIndex).blank && next < lines.size()
            && lines.at(next).columns > activeLevel * m_indentSize) {
            ++activeLevel; anchor = cursorIndex + 1;
        }
        if (activeLevel > 0) {
            activeStart = activeEnd = anchor;
            const int column = (activeLevel - 1) * m_indentSize;
            while (activeStart > 0 && lines.at(activeStart - 1).columns > column) --activeStart;
            while (activeEnd + 1 < lines.size() && lines.at(activeEnd + 1).columns > column) ++activeEnd;
        }
    }

    QPainter painter(viewport()); painter.setClipRect(event->rect());
    const int cellWidth = qMax(1, fontMetrics().width(QLatin1Char(' ')));
    const int stepWidth = cellWidth * m_indentSize;
    const QRect caret = cursorRect();
    for (int i = 0; i < lines.size(); ++i) {
        const auto &line = lines.at(i);
        if (line.top > event->rect().bottom() || line.top + line.height < event->rect().top()) continue;
        const int origin = cursorRect(QTextCursor(line.block)).left();
        const int levels = (line.columns + m_indentSize - 1) / m_indentSize;
        const int first = qMax(0, (event->rect().left() - origin + stepWidth - 1) / stepWidth);
        for (int level = first; level < levels; ++level) {
            const int x = origin + level * stepWidth;
            if (x > event->rect().right()) break;
            // The native editor already painted the caret; never cover it.
            if (hasFocus() && i == cursorIndex && qAbs(x - caret.left()) <= 1) continue;
            const bool active = level == activeLevel - 1 && i >= activeStart && i <= activeEnd;
            painter.setPen(active ? CodeEditorColors::ActiveIndentGuide : CodeEditorColors::IndentGuide);
            painter.drawLine(x, line.top, x, line.top + line.height - 1);
        }
    }
}
void CodeEditor::paintLineNumbers(QPaintEvent *event)
{
    QPainter painter(m_lineNumbers); painter.fillRect(event->rect(), CodeEditorColors::LineNumberBackground);
    painter.setFont(font());
    QTextBlock block = firstVisibleBlock();
    while (block.isValid()) {
        const int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        const int height = qRound(blockBoundingRect(block).height());
        if (top > event->rect().bottom()) break;
        if (block.isVisible() && top + height >= event->rect().top()) {
            painter.setPen(block.blockNumber() == textCursor().blockNumber() ? CodeEditorColors::SelectedLineNumber : CodeEditorColors::LineNumber);
            painter.drawText(0, top, m_lineNumbers->width() - 5, fontMetrics().height(),
                             Qt::AlignRight | Qt::AlignTop, QString::number(block.blockNumber() + 1));
        }
        block = block.next();
    }
}
void CodeEditor::setSearchHighlights(const QList<QTextEdit::ExtraSelection> &selections)
{ m_searchHighlights = selections; updateHighlights(); }
void CodeEditor::updateHighlights()
{
    QList<QTextEdit::ExtraSelection> selections;
    QTextEdit::ExtraSelection current; current.cursor = textCursor(); current.cursor.clearSelection();
    current.format.setBackground(CodeEditorColors::CurrentLine); current.format.setProperty(QTextFormat::FullWidthSelection, true);
    if (m_options.currentLine) selections.append(current);
    if (m_options.searchHighlights) selections.append(m_searchHighlights);
    if (m_options.matchingBrackets && !textCursor().hasSelection()) {
        int position = textCursor().position();
        int partner = matchingBracket(position);
        if (partner < 0 && position > 0) partner = matchingBracket(--position);
        if (partner >= 0) {
            for (int offset : {position, partner}) {
                QTextEdit::ExtraSelection bracket;
                bracket.cursor = QTextCursor(document());
                bracket.cursor.setPosition(offset);
                bracket.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
                bracket.format.setBackground(CodeEditorColors::Comment);
                bracket.format.setForeground(CodeEditorColors::Background);
                selections.append(bracket);
            }
        }
    }
    setExtraSelections(selections);
    m_lineNumbers->update();
    viewport()->update();
}
void CodeEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::MiddleButton) { QPlainTextEdit::mousePressEvent(event); return; }
    event->accept();
    // Hit-test the character under the pointer, not the nearest insertion point.
    const QTextCursor cursor = cursorForPosition(event->pos());
    const QTextBlock block = cursor.block();
    if (event->pos().y() < cursorRect(cursor).top() || event->pos().y() >= cursorRect(cursor).bottom() + 1) return;
    int position = cursor.positionInBlock();
    if (event->pos().x() < cursorRect(cursor).left()) --position;
    const auto *data = dynamic_cast<const CodeBlockData *>(block.userData());
    if (!data) return;
    for (const auto &token : data->tokens) {
        if (token.position > position) break;
        if (token.kind == QLatin1Char('i') && position >= token.position && position < token.position + token.length) {
            emit identifierActivated(block.text().mid(token.position, token.length));
            return;
        }
    }
}
void CodeEditor::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) { event->accept(); return; }
    QPlainTextEdit::mouseReleaseEvent(event);
}

static QList<QTextBlock> selectedCodeBlocks(const QTextCursor &cursor)
{
    QList<QTextBlock> blocks;
    QTextBlock block = cursor.document()->findBlock(cursor.selectionStart());
    const int end = cursor.selectionEnd();
    while (block.isValid()) {
        blocks.append(block); block = block.next();
        // A selection ending at the next line's start does not include that line.
        if (!block.isValid() || block.position() >= end) break;
    }
    return blocks;
}
void CodeEditor::indentSelection(bool unindent)
{
    if (isReadOnly()) return;
    QTextCursor selection = textCursor(); const auto blocks = selectedCodeBlocks(selection);
    selection.beginEditBlock();
    for (const QTextBlock &block : blocks) {
        QTextCursor edit(block);
        if (!unindent) edit.insertText(QString(m_indentSize, QLatin1Char(' ')));
        else {
            const QString text = block.text(); int count = 0;
            if (text.startsWith(QLatin1Char('\t'))) count = 1;
            else while (count < qMin(m_indentSize, text.size()) && text.at(count) == QLatin1Char(' ')) ++count;
            edit.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, count); edit.removeSelectedText();
        }
    }
    selection.endEditBlock(); setTextCursor(selection);
}
void CodeEditor::toggleLineComment(const QString &prefix)
{
    if (isReadOnly() || prefix.isEmpty()) return;
    QTextCursor selection = textCursor(); const auto blocks = selectedCodeBlocks(selection);
    bool remove = true;
    for (const QTextBlock &block : blocks)
        if (!block.text().trimmed().isEmpty() && !block.text().trimmed().startsWith(prefix)) remove = false;
    selection.beginEditBlock();
    for (const QTextBlock &block : blocks) {
        const QString text = block.text(); if (text.trimmed().isEmpty()) continue;
        int offset = 0; while (offset < text.size() && text.at(offset).isSpace()) ++offset;
        QTextCursor edit(block); edit.setPosition(block.position() + offset);
        if (remove) {
            int count = prefix.size(); if (text.mid(offset + count, 1) == QStringLiteral(" ")) ++count;
            edit.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, count); edit.removeSelectedText();
        } else edit.insertText(prefix + QLatin1Char(' '));
    }
    selection.endEditBlock(); setTextCursor(selection);
}
void CodeEditor::goToLine(int line)
{
    const QTextBlock block = document()->findBlockByNumber(qBound(1, line, blockCount()) - 1);
    setTextCursor(QTextCursor(block)); centerCursor(); setFocus();
}
void CodeEditor::keyPressEvent(QKeyEvent *event)
{
    if (m_completion && m_completion->handleKeyPress(event)) { event->accept(); return; }
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if (event->key() == Qt::Key_Delete && modifiers == Qt::ShiftModifier) {
        deleteLines(); event->accept(); return;
    }
    if ((event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)
        && (modifiers == Qt::AltModifier || modifiers == (Qt::AltModifier | Qt::ShiftModifier))) {
        editLines(event->key() == Qt::Key_Down, modifiers & Qt::ShiftModifier);
        event->accept(); return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && (modifiers == Qt::ControlModifier || modifiers == (Qt::ControlModifier | Qt::ShiftModifier))) {
        if (!isReadOnly()) insertAdjacentLine(modifiers & Qt::ShiftModifier);
        event->accept(); return;
    }
    if (event->key() == Qt::Key_Insert && event->modifiers() == Qt::NoModifier) {
        setOverwriteMode(!overwriteMode()); emit insertModeChanged(overwriteMode()); event->accept(); return;
    }
    if (!isReadOnly() && event->key() == Qt::Key_Backspace && modifiers == Qt::NoModifier
        && deleteIndent()) { event->accept(); return; }
    if (!isReadOnly() && !overwriteMode() && m_options.automaticBrackets && editBracket(event)) { event->accept(); return; }
    if (!isReadOnly() && (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab)
        && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        if (event->key() == Qt::Key_Backtab || event->modifiers() & Qt::ShiftModifier) indentSelection(true);
        else if (textCursor().hasSelection()) indentSelection();
        else {
            QTextCursor cursor = textCursor(); int column = 0;
            for (const QChar ch : cursor.block().text().left(cursor.positionInBlock()))
                column += ch == QLatin1Char('\t') ? m_indentSize - column % m_indentSize : 1;
            cursor.insertText(QString(m_indentSize - column % m_indentSize, QLatin1Char(' '))); setTextCursor(cursor);
        }
        event->accept(); return;
    }
    if (!isReadOnly() && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && event->modifiers() == Qt::NoModifier) {
        insertIndentedLine(); event->accept(); return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void CodeEditor::moveLines(bool down) { editLines(down, false); }
void CodeEditor::duplicateLines(bool down) { editLines(down, true); }

void CodeEditor::deleteLines()
{
    if (isReadOnly()) return;
    QTextCursor cursor = textCursor();
    const int column = cursor.positionInBlock();
    const auto blocks = selectedCodeBlocks(cursor);
    int start = blocks.first().position();
    const QTextBlock next = blocks.last().next();
    const int end = next.isValid() ? next.position() : document()->characterCount() - 1;
    // At EOF remove the preceding separator, leaving the preceding line intact.
    if (!next.isValid() && start > 0) --start;
    if (start == end) return;
    cursor.beginEditBlock();
    cursor.setPosition(start); cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.removeSelectedText(); cursor.endEditBlock();
    const QTextBlock target = cursor.block();
    cursor.setPosition(target.position() + qMin(column, target.length() - 1));
    setTextCursor(cursor); ensureCursorVisible();
}

void CodeEditor::editLines(bool down, bool duplicate)
{
    if (isReadOnly()) return;
    const QTextCursor original = textCursor();
    const int position = original.position(), anchor = original.anchor();
    const auto blocks = selectedCodeBlocks(original);
    const QTextBlock first = blocks.first(), last = blocks.last();
    const int start = first.position(), end = last.position() + last.length() - 1;
    const QTextBlock adjacent = down ? last.next() : first.previous();
    if (!duplicate && !adjacent.isValid()) return;

    // Copy only the affected lines, excluding the final paragraph separator.
    QTextCursor source(document()); source.setPosition(start); source.setPosition(end, QTextCursor::KeepAnchor);
    const QString text = source.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    QTextCursor edit = original;
    int offset = 0;
    edit.beginEditBlock();
    if (duplicate) {
        edit.setPosition(down ? end : start);
        edit.insertText(down ? QLatin1Char('\n') + text : text + QLatin1Char('\n'));
        if (down) offset = end - start + 1;
    } else if (down) {
        const QString neighbour = adjacent.text();
        edit.setPosition(start);
        edit.setPosition(adjacent.position() + adjacent.length() - 1, QTextCursor::KeepAnchor);
        edit.insertText(neighbour + QLatin1Char('\n') + text);
        offset = neighbour.size() + 1;
    } else {
        const QString neighbour = adjacent.text();
        offset = adjacent.position() - start;
        edit.setPosition(adjacent.position()); edit.setPosition(end, QTextCursor::KeepAnchor);
        edit.insertText(text + QLatin1Char('\n') + neighbour);
    }
    edit.endEditBlock();
    const int lastPosition = document()->characterCount() - 1;
    edit.setPosition(qBound(0, anchor + offset, lastPosition));
    edit.setPosition(qBound(0, position + offset, lastPosition), QTextCursor::KeepAnchor);
    setTextCursor(edit); ensureCursorVisible();
}

void CodeEditor::setCompletionItems(const QVector<CodeCompletionItem> &items)
{
    if (!m_completion) {
        m_completion = new CodeCompletion(this);
        m_completion->configure(m_options.automaticCompletion, m_options.completionDelay);
    }
    m_completion->setItems(items);
    m_signatureHelp->setItems(items);
}

void CodeEditor::requestCompletion()
{ if (m_completion) m_completion->request(); }

bool CodeEditor::deleteIndent()
{
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection() || cursor.positionInBlock() == 0) return false;
    const QString before = cursor.block().text().left(cursor.positionInBlock());
    int column = 0;
    for (const QChar character : before) {
        if (character == QLatin1Char('\t')) column += m_indentSize - column % m_indentSize;
        else if (character == QLatin1Char(' ')) ++column;
        else return false;
    }
    // Tabs advance to a visual tab stop; partial indentation removes only
    // the excess columns, rather than always deleting four characters.
    const int targetColumn = (column - 1) / m_indentSize * m_indentSize;
    int keep = 0;
    column = 0;
    while (column < targetColumn) {
        column += before.at(keep) == QLatin1Char('\t') ? m_indentSize - column % m_indentSize : 1;
        ++keep;
    }
    cursor.beginEditBlock();
    cursor.setPosition(cursor.block().position() + keep, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.endEditBlock();
    setTextCursor(cursor); ensureCursorVisible();
    return true;
}

static QChar closingBracket(QChar character)
{
    switch (character.unicode()) {
    case '(': return QLatin1Char(')');
    case '[': return QLatin1Char(']');
    case '{': return QLatin1Char('}');
    default: return QChar();
    }
}

bool CodeEditor::isCodePosition(int position) const
{
    const QTextBlock block = document()->findBlock(position);
    const auto *data = dynamic_cast<const CodeBlockData *>(block.userData());
    return data && data->isCodePosition(position - block.position());
}

int CodeEditor::matchingBracket(int position) const
{
    if (position < 0 || position >= document()->characterCount() - 1) return -1;
    const QChar character = document()->characterAt(position);
    if (!QStringLiteral("()[]{}").contains(character)) return -1;
    QTextBlock block = document()->findBlock(position);
    const auto *data = dynamic_cast<const CodeBlockData *>(block.userData());
    if (!data) return -1;
    int index = 0;
    while (index < data->brackets.size() && data->brackets.at(index).position < position - block.position()) ++index;
    if (index == data->brackets.size() || data->brackets.at(index).position != position - block.position()) return -1;
    const bool forward = !closingBracket(character).isNull();
    QVector<QChar> stack;
    stack.append(character);
    index += forward ? 1 : -1;
    // Visit cached bracket tokens only, never copy or rescan the whole source.
    while (block.isValid()) {
        if (data) {
            while (index >= 0 && index < data->brackets.size()) {
                const auto &bracket = data->brackets.at(index);
                const bool opening = !closingBracket(bracket.character).isNull();
                if (opening == forward) stack.append(bracket.character);
                else {
                    const bool matches = forward ? closingBracket(stack.last()) == bracket.character
                                                 : closingBracket(bracket.character) == stack.last();
                    if (!matches) return -1;
                    stack.removeLast();
                    if (stack.isEmpty()) return block.position() + bracket.position;
                }
                index += forward ? 1 : -1;
            }
        }
        block = forward ? block.next() : block.previous();
        data = block.isValid() ? dynamic_cast<const CodeBlockData *>(block.userData()) : nullptr;
        index = forward || !data ? 0 : data->brackets.size() - 1;
    }
    return -1;
}

bool CodeEditor::editBracket(QKeyEvent *event)
{
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return false;
    QTextCursor cursor = textCursor();
    const int start = cursor.selectionStart(), end = cursor.selectionEnd();
    const QChar next = document()->characterAt(start);
    const QChar character = event->text().size() == 1 ? event->text().at(0) : QChar();
    const bool quote = character == QLatin1Char('\'') || character == QLatin1Char('"');
    if (!cursor.hasSelection() && (next == QLatin1Char('\'') || next == QLatin1Char('"'))) {
        const QTextBlock block = document()->findBlock(start);
        const auto *data = dynamic_cast<const CodeBlockData *>(block.userData());
        // Use the highlighter's string boundaries, including its language-specific
        // escape rules, so quotes inside comments or string contents are untouched.
        if (data) for (const auto &range : data->excludedRanges) {
            if (!range.string || range.includesEnd || range.end != start - block.position() + 1) continue;
            if (quote && character == next) {
                cursor.movePosition(QTextCursor::Right); setTextCursor(cursor); return true;
            }
            if (event->key() == Qt::Key_Backspace && event->modifiers() == Qt::NoModifier
                && !range.includesStart && range.end - range.start == 2
                && document()->characterAt(start - 1) == next) {
                cursor.beginEditBlock();
                cursor.setPosition(start - 1); cursor.setPosition(start + 1, QTextCursor::KeepAnchor);
                cursor.removeSelectedText(); cursor.endEditBlock(); setTextCursor(cursor); return true;
            }
        }
    }
    if (!isCodePosition(start)) return false;
    if (event->key() == Qt::Key_Backspace && event->modifiers() == Qt::NoModifier && !cursor.hasSelection() && start > 0) {
        const QChar closing = closingBracket(document()->characterAt(start - 1));
        if (!closing.isNull() && document()->characterAt(start) == closing && matchingBracket(start) == start - 1) {
            cursor.beginEditBlock();
            cursor.setPosition(start - 1); cursor.setPosition(start + 1, QTextCursor::KeepAnchor);
            cursor.removeSelectedText(); cursor.endEditBlock(); setTextCursor(cursor); return true;
        }
    }
    if (event->text().size() != 1) return false;
    const QChar closing = quote ? character : closingBracket(character);
    if (!closing.isNull()) {
        if (cursor.hasSelection()) {
            const bool reversed = cursor.position() < cursor.anchor();
            cursor.beginEditBlock();
            cursor.setPosition(end); cursor.insertText(QString(closing));
            cursor.setPosition(start); cursor.insertText(QString(character));
            cursor.endEditBlock();
            cursor.setPosition(reversed ? end + 1 : start + 1);
            cursor.setPosition(reversed ? start + 1 : end + 1, QTextCursor::KeepAnchor);
        } else {
            // Avoid inserting a second delimiter in the middle of an identifier.
            if (!next.isNull() && !next.isSpace() && !QStringLiteral(")]},;:").contains(next)) return false;
            cursor.beginEditBlock(); cursor.insertText(QString(character) + closing);
            cursor.endEditBlock(); cursor.setPosition(start + 1);
        }
        setTextCursor(cursor); ensureCursorVisible(); return true;
    }
    if (!cursor.hasSelection() && QStringLiteral(")]}").contains(character)
        && document()->characterAt(start) == character && matchingBracket(start) >= 0) {
        cursor.movePosition(QTextCursor::Right); setTextCursor(cursor); return true;
    }
    return false;
}

void CodeEditor::insertAdjacentLine(bool above)
{
    QTextCursor cursor = textCursor();
    // Use the caret's line and leave any selected source text intact.
    cursor.clearSelection();
    if (!above) {
        cursor.movePosition(QTextCursor::EndOfBlock);
        setTextCursor(cursor);
        insertIndentedLine();
        return;
    }
    const QString text = cursor.block().text();
    int leading = 0;
    while (m_options.automaticIndentation && leading < text.size() && text.at(leading).isSpace()) ++leading;
    cursor.movePosition(QTextCursor::StartOfBlock);
    const int start = cursor.position();
    cursor.beginEditBlock();
    cursor.insertText(text.left(leading) + QLatin1Char('\n'));
    cursor.endEditBlock();
    cursor.setPosition(start + leading);
    setTextCursor(cursor); ensureCursorVisible();
}

void CodeEditor::insertIndentedLine()
{
    QTextCursor cursor = textCursor();
    if (!m_options.automaticIndentation) {
        cursor.insertText(QStringLiteral("\n"));
        setTextCursor(cursor); ensureCursorVisible();
        return;
    }
    const int start = cursor.selectionStart(), end = cursor.selectionEnd();
    const QTextBlock first = document()->findBlock(start), last = document()->findBlock(end);
    const QString before = first.text().left(start - first.position());
    const QString after = last.text().mid(end - last.position());
    int leading = 0; while (leading < before.size() && before.at(leading).isSpace()) ++leading;
    const QString indent = before.left(leading);
    int previous = before.size() - 1;
    while (previous >= 0 && before.at(previous).isSpace()) --previous;
    const QChar closing = previous >= 0 ? closingBracket(before.at(previous)) : QChar();
    const bool indentMore = !closing.isNull() && isCodePosition(start);
    int next = 0; while (next < after.size() && after.at(next).isSpace()) ++next;
    const bool splitPair = indentMore && next < after.size() && after.at(next) == closing;
    const QString innerIndent = indent + (indentMore ? QString(m_indentSize, QLatin1Char(' ')) : QString());
    cursor.beginEditBlock();
    if (splitPair) {
        cursor.setPosition(start); cursor.setPosition(end + next, QTextCursor::KeepAnchor);
        cursor.insertText(QLatin1Char('\n') + innerIndent + QLatin1Char('\n') + indent);
    } else cursor.insertText(QLatin1Char('\n') + innerIndent);
    cursor.endEditBlock();
    cursor.setPosition(start + 1 + innerIndent.size());
    setTextCursor(cursor); ensureCursorVisible();
}
void CodeEditor::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y()) setCodePixelSize(font().pixelSize() + (event->angleDelta().y() > 0 ? 1 : -1));
        event->accept(); return;
    }
    QPlainTextEdit::wheelEvent(event);
}
