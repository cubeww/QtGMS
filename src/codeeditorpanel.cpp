#include "editorstandarddialogs.h"
#include "codeeditorpanel.h"
#include "codeeditor.h"
#include "codefindpanel.h"
#include "codeeditorcolors.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QToolBar>
#include <QTabBar>
#include <QVBoxLayout>
#include <QTextDocument>

void CodeEditorPanel::selectRange(int offset, int length)
{
    const int end = qMax(0, m_editor->document()->characterCount() - 1);
    QTextCursor cursor(m_editor->document());
    cursor.setPosition(qBound(0, offset, end));
    cursor.setPosition(qBound(0, offset + length, end), QTextCursor::KeepAnchor);
    m_editor->setTextCursor(cursor);
    m_editor->centerCursor();
    m_editor->setFocus();
}

CodeEditorPanel::CodeEditorPanel(QTextDocument *document, QWidget *parent)
    : QWidget(parent), m_editor(new CodeEditor(this)), m_findPanel(new CodeFindPanel(m_editor, this)),
      m_toolBar(new QToolBar(this)), m_positionLabel(new QLabel(this)), m_formatLabel(new QLabel(this)),
      m_modeLabel(new QLabel(this)), m_signatureLabel(new QLabel(this)),
      m_statusWidget(new QWidget(this)), m_statusLayout(new QHBoxLayout(m_statusWidget))
{
    m_editor->setDocument(document);
    // An external document keeps its own undo history and lifetime.
    document->setDefaultFont(m_editor->font());
    m_toolBar->setIconSize(QSize(16, 16)); m_toolBar->setFixedHeight(30);
    m_toolBar->setMovable(false); m_toolBar->setFloatable(false);
    m_toolBar->setStyleSheet(QStringLiteral("QToolBar QToolButton { min-width: 21px; max-width: 21px; min-height: 23px; max-height: 23px; padding: 0px; }"));
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    layout->addWidget(m_toolBar);
    auto *body = new QHBoxLayout; body->setContentsMargins(0, 0, 0, 0); body->setSpacing(1);
    body->addWidget(m_editor, 1); body->addWidget(m_findPanel); layout->addLayout(body, 1); m_findPanel->hide();
    m_statusLayout->setContentsMargins(5, 2, 5, 2);
    m_statusLayout->setSpacing(8);
    m_statusLayout->addWidget(m_positionLabel); m_statusLayout->addWidget(m_modeLabel);
    m_signatureLabel->setTextFormat(Qt::RichText);
    m_signatureLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_signatureLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_signatureLabel->setMinimumWidth(0);
    m_signatureLabel->setContentsMargins(8, 0, 8, 0);
    m_modeLabel->hide(); m_statusLayout->addWidget(m_signatureLabel, 1); m_statusLayout->addWidget(m_formatLabel); layout->addWidget(m_statusWidget);
    connect(m_editor, &CodeEditor::signatureHelpChanged, this, [this](const QString &text, int start, int length) {
        m_signatureLabel->setToolTip(text);
        if (text.isEmpty()) { m_signatureLabel->clear(); return; }
        const int open = text.indexOf(QLatin1Char('('));
        QString rich = QStringLiteral("<span style='color:%1'>%2</span>")
            .arg(CodeEditorColors::Function.name(), text.left(open).toHtmlEscaped());
        if (start >= open && length > 0) {
            rich += text.mid(open, start - open).toHtmlEscaped();
            rich += QStringLiteral("<b><span style='color:%1'>%2</span></b>")
                .arg(CodeEditorColors::Comment.name(), text.mid(start, length).toHtmlEscaped());
            rich += text.mid(start + length).toHtmlEscaped();
        } else rich += text.mid(open).toHtmlEscaped();
        m_signatureLabel->setText(rich);
    });
    auto *menu = new QMenu(m_editor);
    auto action = [this, menu](const QString &text, const QString &icon, const QKeySequence &shortcut, bool toolbar) {
        auto *item = new QAction(text, this);
        if (!icon.isEmpty()) item->setIcon(QIcon(QStringLiteral(":/images/") + icon + QStringLiteral(".png")));
        item->setShortcut(shortcut); item->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        // Keep commands scoped to the editing surface: the find fields retain
        // their own native clipboard and undo shortcuts.
        m_editor->addAction(item);
        if (toolbar) m_toolBar->addAction(item);
        menu->addAction(item); return item;
    };
    auto *undo = action(tr("Undo"), QStringLiteral("editor/undo"), QKeySequence::Undo, true);
    auto *redo = action(tr("Redo"), QStringLiteral("editor/redo"), QKeySequence::Redo, true);
    undo->setEnabled(document->isUndoAvailable()); redo->setEnabled(document->isRedoAvailable());
    connect(undo, &QAction::triggered, m_editor, &QPlainTextEdit::undo);
    connect(redo, &QAction::triggered, m_editor, &QPlainTextEdit::redo);
    connect(m_editor, &QPlainTextEdit::undoAvailable, undo, &QAction::setEnabled);
    connect(m_editor, &QPlainTextEdit::redoAvailable, redo, &QAction::setEnabled);
    m_toolBar->addSeparator(); menu->addSeparator();
    auto *cut = action(tr("Cut"), QStringLiteral("editor/cut"), QKeySequence::Cut, true);
    auto *copy = action(tr("Copy"), QStringLiteral("editor/copy"), QKeySequence::Copy, true);
    auto *paste = action(tr("Paste"), QStringLiteral("editor/paste"), QKeySequence::Paste, true);
    cut->setEnabled(false); copy->setEnabled(false);
    connect(m_editor, &QPlainTextEdit::copyAvailable, cut, &QAction::setEnabled);
    connect(m_editor, &QPlainTextEdit::copyAvailable, copy, &QAction::setEnabled);
    connect(cut, &QAction::triggered, m_editor, &QPlainTextEdit::cut);
    connect(copy, &QAction::triggered, m_editor, &QPlainTextEdit::copy);
    connect(paste, &QAction::triggered, m_editor, &QPlainTextEdit::paste);
    auto refreshPaste = [paste] { paste->setEnabled(QApplication::clipboard()->mimeData()->hasText()); };
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, refreshPaste); refreshPaste();
    connect(action(tr("Select All"), QString(), QKeySequence::SelectAll, false), &QAction::triggered, m_editor, &QPlainTextEdit::selectAll);
    connect(action(tr("Delete Line"), QString(), QKeySequence(Qt::SHIFT | Qt::Key_Delete), false),
            &QAction::triggered, m_editor, &CodeEditor::deleteLines);
    m_toolBar->addSeparator(); menu->addSeparator();
    connect(action(tr("Find and Replace"), QStringLiteral("code/find"), QKeySequence::Find, true), &QAction::triggered,
            this, [this] { m_findPanel->open(false); });
    connect(action(tr("Replace"), QString(), QKeySequence(Qt::CTRL | Qt::Key_H), false), &QAction::triggered,
            this, [this] { m_findPanel->open(true); });
    connect(action(tr("Find Next"), QString(), QKeySequence(Qt::Key_F3), false), &QAction::triggered,
            this, [this] { m_findPanel->find(); });
    connect(action(tr("Find Previous"), QString(), QKeySequence(Qt::SHIFT | Qt::Key_F3), false), &QAction::triggered,
            this, [this] { m_findPanel->find(true); });
    menu->addSeparator();
    connect(action(tr("Indent"), QString(), QKeySequence(), false), &QAction::triggered, this, [this] { m_editor->indentSelection(); });
    connect(action(tr("Unindent"), QString(), QKeySequence(), false), &QAction::triggered, this, [this] { m_editor->indentSelection(true); });
    connect(action(tr("Toggle Line Comment"), QString(), QKeySequence(Qt::CTRL | Qt::Key_Slash), false), &QAction::triggered,
            this, [this] { m_editor->toggleLineComment(m_lineCommentPrefix); });
    connect(action(tr("Go to Line..."), QString(), QKeySequence(Qt::CTRL | Qt::Key_G), false), &QAction::triggered, this, [this] {
        bool accepted; const int line = EditorInputDialog::getInt(this, tr("Go to Line"), tr("Line:"),
            m_editor->textCursor().blockNumber() + 1, 1, m_editor->blockCount(), 1, &accepted);
        if (accepted) m_editor->goToLine(line);
    });
    m_editor->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_editor, &QWidget::customContextMenuRequested, this, [this, menu](const QPoint &position) {
        menu->exec(m_editor->viewport()->mapToGlobal(position));
    });
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditorPanel::updatePosition);
    connect(m_editor, &QPlainTextEdit::blockCountChanged, this, &CodeEditorPanel::updatePosition);
    connect(m_editor, &CodeEditor::insertModeChanged, this, &CodeEditorPanel::updatePosition);
    updatePosition(); setFocusProxy(m_editor);
}
CodeEditor *CodeEditorPanel::editor() const { return m_editor; }
QToolBar *CodeEditorPanel::toolBar() const { return m_toolBar; }
void CodeEditorPanel::setTextDocument(QTextDocument *document)
{
    m_editor->setSearchHighlights(QList<QTextEdit::ExtraSelection>());
    m_editor->setDocument(document);
    document->setDefaultFont(m_editor->font());
    m_findPanel->refreshHighlights(); updatePosition();
}
void CodeEditorPanel::setHeaderWidget(QWidget *widget)
{ static_cast<QVBoxLayout *>(layout())->insertWidget(1, widget); }
void CodeEditorPanel::setFooterWidget(QWidget *widget)
{ auto *box = static_cast<QVBoxLayout *>(layout()); box->insertWidget(box->count() - 1, widget); }
void CodeEditorPanel::setLineCommentPrefix(const QString &prefix) { m_lineCommentPrefix = prefix; }
void CodeEditorPanel::setFormatDescription(const QString &description)
{
    if (m_codeTabs) m_formatLabel->setToolTip(description);
    else m_formatLabel->setText(description);
}
void CodeEditorPanel::setCodeTab(const QString &name)
{
    if (!m_codeTabs) {
        m_codeTabs = new QTabBar(this);
        m_codeTabs->setExpanding(false);
        m_codeTabs->setFixedHeight(21);
        m_codeTabs->addTab(name);
        setHeaderWidget(m_codeTabs);
        m_editor->setFrameShape(QFrame::Box);
        m_editor->setLineWidth(1);
        auto *messages = new QFrame(this);
        messages->setFrameStyle(QFrame::Box | QFrame::Plain);
        messages->setFixedHeight(22);
        setFooterWidget(messages);
        setCompactStatus();
        m_editor->installEventFilter(this);
        updateFontDescription();
    } else {
        m_codeTabs->setTabText(0, name);
    }
}
void CodeEditorPanel::updateFontDescription()
{
    const QFont font = m_editor->font();
    const int points = font.pixelSize() > 0 ? qRound(font.pixelSize() * 72.0 / 96.0) : qRound(font.pointSizeF());
    m_formatLabel->setText(tr("%1 pt").arg(points));
}
bool CodeEditorPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_editor && event->type() == QEvent::FontChange) updateFontDescription();
    return QWidget::eventFilter(watched, event);
}
void CodeEditorPanel::setCompactStatus()
{
    if (m_compactStatus) return;
    m_compactStatus = true;
    m_statusLayout->removeWidget(m_formatLabel); m_statusLayout->insertWidget(2, m_formatLabel);
    m_statusWidget->setObjectName(QStringLiteral("codeEditorStatus"));
    m_statusWidget->setStyleSheet(QStringLiteral(
        "QWidget#codeEditorStatus { background: #141414; border: 1px solid #92928a; }"));
    m_statusLayout->setContentsMargins(2, 2, 2, 2); m_statusLayout->setSpacing(6);
    m_positionLabel->setFixedWidth(76); m_modeLabel->setFixedWidth(42); m_formatLabel->setFixedWidth(56);
    for (auto *label : {m_positionLabel, m_modeLabel, m_formatLabel, m_signatureLabel}) {
        label->setAlignment(label == m_signatureLabel ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignCenter); label->setFixedHeight(18);
        label->setStyleSheet(QStringLiteral("background: #212121; color: #dddddd; border: none;"));
    }
    m_modeLabel->show(); updatePosition();
}
void CodeEditorPanel::updatePosition()
{
    const QTextCursor cursor = m_editor->textCursor();
    const QString position = tr("%1/%2: %3").arg(cursor.blockNumber() + 1).arg(m_editor->blockCount()).arg(cursor.positionInBlock() + 1);
    const QString mode = m_editor->overwriteMode() ? QStringLiteral("OVR") : QStringLiteral("INS");
    m_positionLabel->setText(m_compactStatus ? position : position + QStringLiteral("    |    ") + mode);
    m_modeLabel->setText(mode);
}
