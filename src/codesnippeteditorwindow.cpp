#include "codesnippeteditorwindow.h"
#include "editorstandarddialogs.h"
#include "codeeditor.h"
#include "codeeditorpanel.h"
#include "gmlhighlighter.h"
#include "gmlsymbols.h"
#include "textfiledocument.h"
#include <QAction>
#include <QCloseEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QMenuBar>
#include <QPlainTextDocumentLayout>
#include <QSaveFile>
#include <QShortcut>
#include <QTextDocument>
#include <QToolBar>

void CodeSnippetEditorWindow::selectRange(int offset, int length)
{
    m_codePanel->selectRange(offset, length);
}

void CodeSnippetEditorWindow::setReadOnly(bool readOnly)
{
    m_codePanel->editor()->setReadOnly(readOnly);
    m_codePanel->toolBar()->setVisible(!readOnly);
}

CodeSnippetEditorWindow::CodeSnippetEditorWindow(const QString &title, const QString &tabName,
                                               const QString &code, const QString &contextName, QWidget *parent)
    : EditorWindow(parent), m_document(new QTextDocument(this)), m_contextName(contextName), m_originalCode(code)
{
    m_document->setDocumentLayout(new QPlainTextDocumentLayout(m_document));
    m_document->setPlainText(code);
    m_document->setModified(false);
    m_highlighter = new GmlHighlighter(m_document);
    m_codePanel = new CodeEditorPanel(m_document, this);
    connect(m_codePanel->editor(), &CodeEditor::identifierActivated, this, &EditorWindow::resourceNameActivated);
    m_codePanel->editor()->setCompletionItems(GmlSymbols::instance().items);
    auto *codeEditor = m_codePanel->editor();
    connect(&GmlSymbols::instance(), &GmlSymbols::scriptSignaturesChanged, codeEditor, [this, codeEditor] {
        if (m_project) {
            m_highlighter->setResources(*m_project);
            codeEditor->setCompletionItems(GmlSymbols::completionItems(*m_project));
        }
    }, Qt::QueuedConnection);
    m_codePanel->setCodeTab(tabName);
    m_codePanel->setLineCommentPrefix(QStringLiteral("//"));
    setWindowTitle(title);
    editorMenuBar()->hide(); resize(786, 492); setMinimumSize(500, 250);
    setCentralWidget(m_codePanel);

    QToolBar *toolbar = m_codePanel->toolBar();
    QAction *first = toolbar->actions().first();
    auto *ok = new QAction(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("OK, Save changes"), this);
    ok->setShortcut(QKeySequence::Save);
    addAction(ok); toolbar->insertAction(first, ok);
    connect(ok, &QAction::triggered, this, &CodeSnippetEditorWindow::accept);
    toolbar->insertSeparator(first);
    auto *load = new QAction(QIcon(QStringLiteral(":/images/open.png")), tr("Load the code from a text file"), this);
    auto *save = new QAction(QIcon(QStringLiteral(":/images/save.png")), tr("Save the code to a text file"), this);
    toolbar->insertAction(first, load); toolbar->insertAction(first, save);
    connect(load, &QAction::triggered, this, &CodeSnippetEditorWindow::importCode);
    connect(save, &QAction::triggered, this, &CodeSnippetEditorWindow::exportCode);
    auto *print = new QAction(QIcon(QStringLiteral(":/images/code/print.png")), tr("Print (not implemented)"), this);
    print->setEnabled(false); toolbar->insertAction(first, print); toolbar->insertSeparator(first);
    toolbar->addSeparator();
    auto *check = toolbar->addAction(QIcon(QStringLiteral(":/images/code/check.png")), tr("Syntax checking (not implemented)"));
    auto *completion = toolbar->addAction(QIcon(QStringLiteral(":/images/code/completion.png")), tr("Code completion (Ctrl+Space)"));
    check->setEnabled(false);
    connect(completion, &QAction::triggered, m_codePanel->editor(), &CodeEditor::requestCompletion);

    auto *cancel = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(cancel, &QShortcut::activated, this, [this] { close(); });
}

void CodeSnippetEditorWindow::setResources(const Project &project)
{
    m_project = &project;
    m_highlighter->setResources(project);
    m_codePanel->editor()->setCompletionItems(GmlSymbols::completionItems(project));
}
void CodeSnippetEditorWindow::changeEvent(QEvent *event)
{
    EditorWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && isActiveWindow() && m_project) setResources(*m_project);
}
bool CodeSnippetEditorWindow::exec()
{
    QEventLoop loop;
    connect(this, &CodeSnippetEditorWindow::finished, &loop, &QEventLoop::quit);
    // The resource host commits the accepted text as one undoable change.
    setWindowModality(Qt::ApplicationModal);
    show(); m_codePanel->editor()->setFocus(); loop.exec(QEventLoop::DialogExec);
    return m_accepted;
}

QString CodeSnippetEditorWindow::code() const
{ return m_document->isModified() ? m_document->toPlainText() : m_originalCode; }
bool CodeSnippetEditorWindow::hasChanges() const { return code() != m_originalCode; }
bool CodeSnippetEditorWindow::commitChanges() { return true; }
void CodeSnippetEditorWindow::resetCode(const QString &code)
{
    if (this->code() != code) m_document->setPlainText(code);
    m_originalCode = code;
    m_document->setModified(false);
}
void CodeSnippetEditorWindow::accept() { m_accepted = true; close(); }
void CodeSnippetEditorWindow::closeEvent(QCloseEvent *event)
{
    if (!m_accepted && hasChanges()) {
        const auto answer = EditorMessageBox::question(this, windowTitle(), tr("Save changes to this code?"),
            EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel) { event->ignore(); return; }
        m_accepted = answer == EditorMessageBox::Save;
    }
    if (m_accepted && !commitChanges()) { m_accepted = false; event->ignore(); return; }
    event->accept(); emit finished();
}
void CodeSnippetEditorWindow::importCode()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Code"), QString(), tr("Code files (*.gml *.txt);;All files (*)"));
    if (path.isEmpty()) return;
    QString text, error;
    if (!TextFileDocument::readText(path, text, error)) { EditorMessageBox::critical(this, tr("Cannot Load Code"), error); return; }
    QTextCursor cursor(m_document); cursor.beginEditBlock(); cursor.select(QTextCursor::Document); cursor.insertText(text); cursor.endEditBlock();
    m_codePanel->editor()->goToLine(1);
}
void CodeSnippetEditorWindow::exportCode()
{
    QString fileName = m_contextName;
    for (const QChar character : QStringLiteral("<>:\"/\\|?*")) fileName.replace(character, QLatin1Char('_'));
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Code"), fileName + QStringLiteral(".gml"), tr("GML files (*.gml);;Text files (*.txt)"));
    if (path.isEmpty()) return;
    QSaveFile file(path); const QByteArray bytes = m_document->toPlainText().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        EditorMessageBox::critical(this, tr("Cannot Save Code"), file.errorString());
}
