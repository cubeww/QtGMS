#include "codefindpanel.h"
#include "codeeditor.h"
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTimer>

CodeFindPanel::CodeFindPanel(CodeEditor *editor, QWidget *parent)
    : QWidget(parent), m_editor(editor), m_findBox(new QComboBox(this)), m_replaceBox(new QComboBox(this)),
      m_caseCheck(new QCheckBox(tr("Case sensitive"), this)), m_wordCheck(new QCheckBox(tr("Whole word only"), this)),
      m_resultLabel(new QLabel(this)), m_highlightTimer(new QTimer(this))
{
    setFixedWidth(185);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(5, 5, 5, 5); layout->setSpacing(10);
    m_findBox->setEditable(true); m_findBox->setInsertPolicy(QComboBox::NoInsert);
    m_replaceBox->setEditable(true); m_replaceBox->setInsertPolicy(QComboBox::NoInsert);
    m_caseCheck->setChecked(true);
    auto *findGroup = new QGroupBox(tr("Find"), this); auto *findLayout = new QGridLayout(findGroup);
    findLayout->setContentsMargins(5, 16, 5, 7); findLayout->setSpacing(5);
    findLayout->addWidget(m_findBox, 0, 0, 1, 2); findLayout->addWidget(m_caseCheck, 1, 0, 1, 2);
    findLayout->addWidget(m_wordCheck, 2, 0, 1, 2);
    auto *replaceGroup = new QGroupBox(tr("Replace"), this); auto *replaceLayout = new QGridLayout(replaceGroup);
    replaceLayout->setContentsMargins(5, 16, 5, 7); replaceLayout->setSpacing(5);
    replaceLayout->addWidget(m_replaceBox, 0, 0, 1, 2);
    const QStringList names = {tr("Previous"), tr("Next"), tr("First"), tr("Last")};
    for (int i = 0; i < names.size(); ++i) {
        const bool backwards = i == 0 || i == 3, boundary = i >= 2;
        auto *findButton = new QPushButton(names.at(i), findGroup);
        findButton->setMinimumWidth(0); findButton->setFixedHeight(25);
        findLayout->addWidget(findButton, 3 + i / 2, i % 2);
        connect(findButton, &QPushButton::clicked, this, [this, backwards, boundary] { find(backwards, boundary); });
        auto *replaceButton = new QPushButton(names.at(i), replaceGroup);
        replaceButton->setMinimumWidth(0); replaceButton->setFixedHeight(25);
        replaceLayout->addWidget(replaceButton, 1 + i / 2, i % 2);
        connect(replaceButton, &QPushButton::clicked, this, [this, backwards, boundary] { replace(backwards, boundary); });
    }
    auto *all = new QPushButton(tr("Replace All"), replaceGroup); all->setFixedHeight(25);
    replaceLayout->addWidget(all, 3, 0, 1, 2); connect(all, &QPushButton::clicked, this, &CodeFindPanel::replaceAll);
    layout->addWidget(findGroup); layout->addWidget(replaceGroup); m_resultLabel->setWordWrap(true);
    layout->addWidget(m_resultLabel); layout->addStretch();
    auto *close = new QPushButton(tr("Close"), this); layout->addWidget(close);
    connect(close, &QPushButton::clicked, this, [this] { hide(); m_editor->setFocus(); });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this); escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, close, &QPushButton::click);
    connect(m_findBox->lineEdit(), &QLineEdit::returnPressed, this, [this] { find(); });
    connect(m_replaceBox->lineEdit(), &QLineEdit::returnPressed, this, [this] { replace(false, false); });
    m_highlightTimer->setSingleShot(true); m_highlightTimer->setInterval(120);
    connect(m_highlightTimer, &QTimer::timeout, this, &CodeFindPanel::highlightMatches);
    connect(editor, &QPlainTextEdit::textChanged, this, [this] { if (isVisible()) m_highlightTimer->start(); });
    connect(m_findBox, &QComboBox::editTextChanged, this, [this] { m_highlightTimer->start(); });
    for (QCheckBox *check : {m_caseCheck, m_wordCheck})
        connect(check, &QCheckBox::toggled, this, [this] { m_highlightTimer->start(); });
}
void CodeFindPanel::open(bool replace)
{
    const QString selected = m_editor->textCursor().selectedText();
    if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator)) m_findBox->setEditText(selected);
    show(); (replace ? m_replaceBox : m_findBox)->setFocus();
    (replace ? m_replaceBox : m_findBox)->lineEdit()->selectAll(); highlightMatches();
}
QTextDocument::FindFlags CodeFindPanel::findFlags(bool backwards) const
{
    QTextDocument::FindFlags flags;
    if (m_caseCheck->isChecked()) flags |= QTextDocument::FindCaseSensitively;
    if (m_wordCheck->isChecked()) flags |= QTextDocument::FindWholeWords;
    if (backwards) flags |= QTextDocument::FindBackward;
    return flags;
}
void CodeFindPanel::remember(QComboBox *box)
{
    const QString text = box->currentText(); if (text.isEmpty()) return;
    const int index = box->findText(text); if (index < 0) box->insertItem(0, text);
    if (box->count() > 20) box->removeItem(box->count() - 1);
    box->setEditText(text);
}
void CodeFindPanel::find(bool backwards, bool boundary)
{
    const QString query = m_findBox->currentText(); if (query.isEmpty()) return;
    remember(m_findBox);
    const QTextCursor current = m_editor->textCursor();
    int position = backwards ? current.selectionStart() : current.selectionEnd();
    const int edge = backwards ? m_editor->document()->characterCount() - 1 : 0;
    if (boundary) position = edge;
    QTextCursor found = m_editor->document()->find(query, position, findFlags(backwards));
    if (found.isNull() && !boundary) found = m_editor->document()->find(query, edge, findFlags(backwards));
    if (found.isNull()) { m_resultLabel->setText(tr("Text not found.")); return; }
    m_editor->setTextCursor(found); m_editor->ensureCursorVisible();
    m_resultLabel->clear();
}
bool CodeFindPanel::selectionMatches() const
{
    if (m_findBox->currentText().isEmpty()) return false;
    const QTextCursor current = m_editor->textCursor();
    const QTextCursor found = m_editor->document()->find(m_findBox->currentText(), current.selectionStart(), findFlags());
    return !found.isNull() && current.hasSelection() && found.selectionStart() == current.selectionStart()
        && found.selectionEnd() == current.selectionEnd();
}
void CodeFindPanel::replace(bool backwards, bool boundary)
{
    if (m_editor->isReadOnly()) return;
    if (boundary || !selectionMatches()) find(backwards, boundary);
    if (!selectionMatches()) return;
    remember(m_replaceBox);
    QTextCursor cursor = m_editor->textCursor(); cursor.beginEditBlock();
    cursor.insertText(m_replaceBox->currentText()); cursor.endEditBlock();
    if (backwards) cursor.setPosition(cursor.position() - m_replaceBox->currentText().size());
    m_editor->setTextCursor(cursor); find(backwards);
}
void CodeFindPanel::replaceAll()
{
    const QString query = m_findBox->currentText(), replacement = m_replaceBox->currentText();
    if (query.isEmpty() || m_editor->isReadOnly()) return;
    remember(m_findBox); remember(m_replaceBox);
    QTextCursor edit(m_editor->document()); edit.beginEditBlock(); int count = 0, position = 0;
    for (;;) {
        QTextCursor found = m_editor->document()->find(query, position, findFlags());
        if (found.isNull()) break;
        found.insertText(replacement); position = found.position(); ++count;
    }
    edit.endEditBlock(); m_resultLabel->setText(tr("Replaced %1 occurrence(s).").arg(count));
}
void CodeFindPanel::highlightMatches()
{
    QList<QTextEdit::ExtraSelection> selections;
    if (isVisible() && !m_findBox->currentText().isEmpty()) {
        int position = 0;
        // Limit paint overlays, not the search or replace operations themselves.
        while (selections.size() < 2000) {
            const QTextCursor found = m_editor->document()->find(m_findBox->currentText(), position, findFlags());
            if (found.isNull()) break;
            QTextEdit::ExtraSelection match; match.cursor = found;
            match.format.setBackground(m_editor->palette().highlight());
            match.format.setForeground(m_editor->palette().highlightedText());
            selections.append(match); position = found.selectionEnd();
        }
    }
    m_editor->setSearchHighlights(selections);
}
void CodeFindPanel::hideEvent(QHideEvent *event)
{
    m_highlightTimer->stop(); m_editor->setSearchHighlights(QList<QTextEdit::ExtraSelection>());
    QWidget::hideEvent(event);
}
void CodeFindPanel::refreshHighlights() { highlightMatches(); }
