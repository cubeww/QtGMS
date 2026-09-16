#include "editorstandarddialogs.h"
#include "scripteditorwindow.h"
#include "textfiledocument.h"
#include "codeeditorpanel.h"
#include "codeeditor.h"
#include "gmlhighlighter.h"
#include "gmlsymbols.h"
#include <QAction>
#include <QCloseEvent>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QShortcut>
#include <QToolBar>

ScriptEditorWindow::ScriptEditorWindow(TextFileDocument *document, const Project &project, QWidget *parent)
    : ResourceEditorWindow(parent), m_document(document), m_codePanel(new CodeEditorPanel(document->textDocument(), this)),
      m_project(&project), m_highlighter(new GmlHighlighter(document->textDocument()))
{
    document->setParent(this); setAttribute(Qt::WA_DeleteOnClose);
    resize(984, 480); setMinimumSize(508, 250); editorMenuBar()->hide();
    m_codePanel->setLineCommentPrefix(QStringLiteral("//"));
    m_codePanel->setFormatDescription(document->formatDescription());
    m_highlighter->setResources(project);
    GmlSymbols::instance().watchScript(document);
    m_codePanel->editor()->setCompletionItems(GmlSymbols::completionItems(project));
    auto *codeEditor = m_codePanel->editor();
    const Project *projectContext = m_project;
    // Closing a script unregisters its document during widget destruction.
    // Refresh surviving controls on the next event-loop turn; Qt drops the
    // queued call if this editor has been deleted in the meantime.
    connect(&GmlSymbols::instance(), &GmlSymbols::scriptSignaturesChanged, codeEditor, [codeEditor, projectContext] {
        codeEditor->setCompletionItems(GmlSymbols::completionItems(*projectContext));
    }, Qt::QueuedConnection);
    connect(m_codePanel->editor(), &CodeEditor::identifierActivated, this, &EditorWindow::resourceNameActivated);
    QToolBar *toolbar = m_codePanel->toolBar(); QAction *first = toolbar->actions().first();
    auto *ok = new QAction(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("OK, Save changes"), this);
    addAction(ok); toolbar->insertAction(first, ok);
    connect(ok, &QAction::triggered, this, [this] { if (save()) close(); });
    toolbar->insertSeparator(first);
    auto *load = new QAction(QIcon(QStringLiteral(":/images/open.png")), tr("Load the code from a text file"), this);
    auto *exportAction = new QAction(QIcon(QStringLiteral(":/images/save.png")), tr("Save the code to a text file"), this);
    toolbar->insertAction(first, load); toolbar->insertAction(first, exportAction);
    connect(load, &QAction::triggered, this, &ScriptEditorWindow::importCode);
    connect(exportAction, &QAction::triggered, this, &ScriptEditorWindow::exportCode);
    auto *print = new QAction(QIcon(QStringLiteral(":/images/code/print.png")), tr("Print (not implemented)"), this);
    print->setEnabled(false); toolbar->insertAction(first, print); toolbar->insertSeparator(first);
    toolbar->addSeparator();
    auto *check = toolbar->addAction(QIcon(QStringLiteral(":/images/code/check.png")), tr("Syntax checking (not implemented)"));
    auto *completion = toolbar->addAction(QIcon(QStringLiteral(":/images/code/completion.png")), tr("Code completion (Ctrl+Space)"));
    check->setEnabled(false);
    connect(completion, &QAction::triggered, m_codePanel->editor(), &CodeEditor::requestCompletion);
    toolbar->addWidget(new QLabel(tr("  Name: "), toolbar));
    auto *nameEdit = new QLineEdit(document->name(), toolbar); enableResourceRenaming(nameEdit, ResourceType::Script);
    nameEdit->setFixedSize(211, 22); toolbar->addWidget(nameEdit);
    setCentralWidget(m_codePanel);
    connect(document->textDocument(), &QTextDocument::modificationChanged, this, &ScriptEditorWindow::refreshTitle);
    connect(document, &TextFileDocument::saved, this, [this] { emit resourceSaved(ResourceType::Script, filePath(), QString()); });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &ScriptEditorWindow::saveProjectRequested);
    refreshTitle(); m_codePanel->editor()->setFocus();
}
QString ScriptEditorWindow::filePath() const { return m_document->filePath(); }
void ScriptEditorWindow::relocate(const QString &oldDirectory, const QString &newDirectory)
{ m_document->relocate(oldDirectory, newDirectory); refreshTitle(); }
void ScriptEditorWindow::refreshTitle()
{
    setWindowTitle(tr("Script: %1%2").arg(m_document->name(), m_document->textDocument()->isModified() ? QStringLiteral(" *") : QString()));
}
bool ScriptEditorWindow::save()
{
    QString error; if (m_document->save(error)) return true;
    EditorMessageBox::critical(this, tr("Cannot Save Script"), error); return false;
}
void ScriptEditorWindow::importCode()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Code"), QString(), tr("Code files (*.gml *.txt);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!m_document->importFile(path, error)) EditorMessageBox::critical(this, tr("Cannot Load Code"), error);
    else m_codePanel->editor()->goToLine(1);
}
void ScriptEditorWindow::exportCode()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Code"), m_document->name() + QStringLiteral(".gml"),
        tr("GML files (*.gml);;Text files (*.txt)"));
    if (path.isEmpty()) return;
    QString error;
    if (!m_document->exportFile(path, error)) EditorMessageBox::critical(this, tr("Cannot Save Code"), error);
}
void ScriptEditorWindow::closeEvent(QCloseEvent *event)
{
    if (m_document->textDocument()->isModified()) {
        const auto answer = EditorMessageBox::question(this, tr("Script"), tr("Save changes to %1?").arg(m_document->name()),
            EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel || (answer == EditorMessageBox::Save && !save())) { event->ignore(); return; }
    }
    event->accept();
}
void ScriptEditorWindow::changeEvent(QEvent *event)
{
    ResourceEditorWindow::changeEvent(event);
    // Project edits can add or rename symbols while this window is inactive.
    if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
        m_highlighter->setResources(*m_project);
        m_codePanel->editor()->setCompletionItems(GmlSymbols::completionItems(*m_project));
    }
}
