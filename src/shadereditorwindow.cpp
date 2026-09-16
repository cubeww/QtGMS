#include "editorstandarddialogs.h"
#include "shadereditorwindow.h"
#include "shaderdocument.h"
#include "shaderhighlighter.h"
#include "codeeditorpanel.h"
#include "codeeditor.h"
#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QScrollBar>
#include <QShortcut>
#include <QTabBar>
#include <QToolBar>

void ShaderEditorWindow::selectCode(int stage, int offset, int length)
{
    m_tabs->setCurrentIndex(stage);
    m_codePanel->selectRange(offset, length);
}

ShaderEditorWindow::ShaderEditorWindow(ShaderDocument *document, Project *project, QWidget *parent)
    : ResourceEditorWindow(parent), m_document(document), m_project(project),
      m_codePanel(new CodeEditorPanel(document->stageDocument(0), this)), m_tabs(new QTabBar(this)),
      m_typeCombo(new QComboBox(this)), m_stage(0), m_savedType(project->shaderType(document->filePath()))
{
    document->setParent(this); setAttribute(Qt::WA_DeleteOnClose);
    connect(m_codePanel->editor(), &CodeEditor::identifierActivated, this, &EditorWindow::resourceNameActivated);
    resize(984, 525); setMinimumSize(508, 250); editorMenuBar()->hide();
    m_codePanel->setLineCommentPrefix(QStringLiteral("//")); m_codePanel->setFormatDescription(document->formatDescription());
    m_tabs->addTab(tr("Vertex")); m_tabs->addTab(tr("Fragment")); m_tabs->setExpanding(false);
    m_tabs->setDrawBase(true); m_codePanel->setHeaderWidget(m_tabs);
    for (int i = 0; i < 2; ++i) {
        m_highlighters[i] = new ShaderHighlighter(document->stageDocument(i));
        m_highlighters[i]->setLanguage(m_savedType); m_cursors[i] = QTextCursor(document->stageDocument(i));
    }
    connect(m_tabs, &QTabBar::currentChanged, this, &ShaderEditorWindow::changeStage);
    QToolBar *toolbar = m_codePanel->toolBar(); QAction *first = toolbar->actions().first();
    auto *ok = new QAction(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("OK, Save changes"), this);
    addAction(ok); toolbar->insertAction(first, ok);
    connect(ok, &QAction::triggered, this, [this] { if (save()) close(); }); toolbar->insertSeparator(first);
    auto *load = new QAction(QIcon(QStringLiteral(":/images/open.png")), tr("Load code into the current tab"), this);
    auto *exportAction = new QAction(QIcon(QStringLiteral(":/images/save.png")), tr("Save the current tab to a text file"), this);
    toolbar->insertAction(first, load); toolbar->insertAction(first, exportAction);
    connect(load, &QAction::triggered, this, &ShaderEditorWindow::importCode);
    connect(exportAction, &QAction::triggered, this, &ShaderEditorWindow::exportCode);
    auto *print = new QAction(QIcon(QStringLiteral(":/images/code/print.png")), tr("Print (not implemented)"), this);
    print->setEnabled(false); toolbar->insertAction(first, print); toolbar->insertSeparator(first);
    toolbar->addSeparator();
    auto *completion = toolbar->addAction(QIcon(QStringLiteral(":/images/code/completion.png")), tr("Code completion (not implemented)"));
    completion->setEnabled(false);
    toolbar->addWidget(new QLabel(tr("  Name: "), toolbar));
    auto *nameEdit = new QLineEdit(document->name(), toolbar); enableResourceRenaming(nameEdit, ResourceType::Shader); nameEdit->setFixedSize(211, 22);
    toolbar->addWidget(nameEdit);
    toolbar->addWidget(new QLabel(QStringLiteral("    "), toolbar));
    m_typeCombo->addItem(tr("GLSL ES (auto)"), QStringLiteral("GLSLES"));
    for (const QString &type : {QStringLiteral("GLSL"), QStringLiteral("HLSL9"), QStringLiteral("HLSL11")}) m_typeCombo->addItem(type, type);
    if (m_typeCombo->findData(m_savedType) < 0) m_typeCombo->addItem(m_savedType, m_savedType);
    m_typeCombo->setCurrentIndex(m_typeCombo->findData(m_savedType)); m_typeCombo->setFixedSize(145, 22);
    m_typeCombo->setToolTip(tr("Shader language. Changing it does not convert the source code.")); toolbar->addWidget(m_typeCombo);
    connect(m_typeCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this] {
        for (ShaderHighlighter *highlighter : m_highlighters) highlighter->setLanguage(m_typeCombo->currentData().toString());
        refreshTitle();
    });
    connect(document, &ShaderDocument::modifiedChanged, this, &ShaderEditorWindow::refreshTitle);
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &ShaderEditorWindow::saveProjectRequested);
    auto *nextTab = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
    auto *previousTab = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
    connect(nextTab, &QShortcut::activated, this, [this] { m_tabs->setCurrentIndex(1 - m_stage); });
    connect(previousTab, &QShortcut::activated, this, [this] { m_tabs->setCurrentIndex(1 - m_stage); });
    setCentralWidget(m_codePanel); refreshTitle(); m_codePanel->editor()->setFocus();
}
bool ShaderEditorWindow::isModified() const
{ return m_document->isModified() || m_typeCombo->currentData().toString() != m_savedType; }
void ShaderEditorWindow::refreshTitle()
{
    setWindowTitle(tr("Shader: %1%2").arg(m_document->name(), isModified() ? QStringLiteral(" *") : QString()));
    m_tabs->setTabText(0, tr("Vertex") + (m_document->stageDocument(0)->isModified() ? QStringLiteral(" *") : QString()));
    m_tabs->setTabText(1, tr("Fragment") + (m_document->stageDocument(1)->isModified() ? QStringLiteral(" *") : QString()));
}
void ShaderEditorWindow::changeStage(int stage)
{
    if (stage == m_stage || stage < 0 || stage > 1) return;
    CodeEditor *editor = m_codePanel->editor(); m_cursors[m_stage] = editor->textCursor();
    m_verticalScroll[m_stage] = editor->verticalScrollBar()->value(); m_horizontalScroll[m_stage] = editor->horizontalScrollBar()->value();
    m_stage = stage; m_codePanel->setTextDocument(m_document->stageDocument(stage));
    editor->setTextCursor(m_cursors[stage]); editor->verticalScrollBar()->setValue(m_verticalScroll[stage]);
    editor->horizontalScrollBar()->setValue(m_horizontalScroll[stage]); editor->setFocus();
}
QString ShaderEditorWindow::filePath() const { return m_document->filePath(); }
void ShaderEditorWindow::relocate(const QString &oldDirectory, const QString &newDirectory)
{ m_document->relocate(oldDirectory, newDirectory); }
bool ShaderEditorWindow::save()
{
    QString error;
    if (!m_document->save(error)) { EditorMessageBox::critical(this, tr("Cannot Save Shader"), error); return false; }
    const QString type = m_typeCombo->currentData().toString();
    if (type != m_savedType && !m_project->setShaderType(filePath(), type, error)) {
        EditorMessageBox::critical(this, tr("Cannot Save Shader Language"), tr("The shader source is saved, but its language setting could not be saved:\n%1").arg(error)); return false;
    }
    m_savedType = type; refreshTitle(); emit resourceSaved(ResourceType::Shader, filePath(), QString()); return true;
}
void ShaderEditorWindow::importCode()
{
    const QString path = QFileDialog::getOpenFileName(this, m_stage == 0 ? tr("Load Vertex Code") : tr("Load Fragment Code"), QString(),
        tr("Shader source (*.vsh *.fsh *.vert *.frag *.glsl *.hlsl *.txt);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!m_document->importStage(m_stage, path, error)) EditorMessageBox::critical(this, tr("Cannot Load Code"), error);
    else m_codePanel->editor()->goToLine(1);
}
void ShaderEditorWindow::exportCode()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Shader Code"), m_document->name() + (m_stage == 0 ? QStringLiteral(".vsh") : QStringLiteral(".fsh")),
        tr("Shader source (*.vsh *.fsh);;GLSL files (*.glsl);;HLSL files (*.hlsl);;Text files (*.txt)"));
    if (path.isEmpty()) return;
    QString error; if (!m_document->exportStage(m_stage, path, error)) EditorMessageBox::critical(this, tr("Cannot Save Code"), error);
}
void ShaderEditorWindow::closeEvent(QCloseEvent *event)
{
    if (isModified()) {
        const auto answer = EditorMessageBox::question(this, tr("Shader"), tr("Save changes to %1?").arg(m_document->name()),
            EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel || (answer == EditorMessageBox::Save && !save())) { event->ignore(); return; }
    }
    event->accept();
}
