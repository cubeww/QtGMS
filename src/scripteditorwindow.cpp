#include "editorstandarddialogs.h"
#include "scripteditorwindow.h"
#include "textfiledocument.h"
#include "codeeditorpanel.h"
#include "codeeditor.h"
#include "gmlhighlighter.h"
#include "gmlsymbols.h"
#include "scriptsource.h"
#include <QAction>
#include <QCloseEvent>
#include <QFileDialog>
#include <QLabel>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenuBar>
#include <QShortcut>
#include <QToolBar>
#include <QTabBar>
#include <QPlainTextDocumentLayout>
#include <QScrollBar>
#include <QSignalBlocker>

ScriptEditorWindow::ScriptEditorWindow(TextFileDocument *document, const Project &project, QWidget *parent)
    : ResourceEditorWindow(parent), m_document(document), m_codePanel(new CodeEditorPanel(document->textDocument(), this)),
      m_project(&project)
{
    document->setParent(this); setAttribute(Qt::WA_DeleteOnClose);
    resize(984, 480); setMinimumSize(508, 250); editorMenuBar()->hide();
    m_codePanel->setLineCommentPrefix(QStringLiteral("//"));
    m_codePanel->setCodeTab(document->name());
    m_codePanel->setFormatDescription(document->formatDescription());
    m_codePanel->editor()->setCompletionItems(GmlSymbols::completionItems(project));
    // Closing a script unregisters its document during widget destruction.
    // Refresh surviving controls on the next event-loop turn; Qt drops the
    // queued call if this editor has been deleted in the meantime.
    connect(&GmlSymbols::instance(), &GmlSymbols::scriptSignaturesChanged, this, &ScriptEditorWindow::refreshSymbols, Qt::QueuedConnection);
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
    auto *nameFields = new QWidget(toolbar);
    auto *nameLayout = new QHBoxLayout(nameFields);
    nameLayout->setContentsMargins(0, 0, 0, 0); nameLayout->setSpacing(2);
    nameLayout->addWidget(new QLabel(tr("  Name: "), nameFields));
    auto *nameEdit = new QLineEdit(document->name(), nameFields);
    m_nameEdit = nameEdit;
    connect(nameEdit, &QLineEdit::editingFinished, this, &ScriptEditorWindow::renameTab);
    nameEdit->setMinimumWidth(80); nameEdit->setMaximumWidth(211); nameEdit->setFixedHeight(22);
    nameLayout->addWidget(nameEdit, 1);
    nameLayout->addStretch();
    nameFields->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(nameFields);
    setCentralWidget(m_codePanel);
    connect(document->textDocument(), &QTextDocument::modificationChanged, this, &ScriptEditorWindow::refreshTitle);
    connect(document, &TextFileDocument::saved, this, [this] {
        m_savedSource = m_document->textDocument()->toPlainText();
        for (auto &tab : m_tabs) tab.document->setModified(false);
        emit resourceSaved(ResourceType::Script, filePath(), QString());
    });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &ScriptEditorWindow::saveProjectRequested);
    m_tabBar = m_codePanel->codeTabs();
    m_tabBar->setTabsClosable(true);
    connect(m_tabBar, &QTabBar::currentChanged, this, &ScriptEditorWindow::changeTab);
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &ScriptEditorWindow::removeTab);
    m_savedSource = document->textDocument()->toPlainText();
    loadSections();
    GmlSymbols::instance().watchScript(document);
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
    QString error;
    if (m_document->save(error)) return true;
    EditorMessageBox::critical(this, tr("Cannot Save Script"), error); return false;
}
void ScriptEditorWindow::importCode()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Code"), QString(), tr("Code files (*.gml *.txt);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!m_document->importFile(path, error)) EditorMessageBox::critical(this, tr("Cannot Load Code"), error);
    else loadSections();
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
        refreshSymbols();
    }
}

void ScriptEditorWindow::loadSections()
{
    const auto oldTabs = m_tabs;
    m_tabs.clear();
    m_currentTab = -1;
    const QString source = m_document->textDocument()->toPlainText();
    m_definitions = ScriptSource::hasDefinitions(source);
    for (const auto &section : ScriptSource::sections(source, m_document->name())) {
        ScriptTab tab;
        tab.name = section.name;
        tab.document = new QTextDocument(this);
        tab.document->setDocumentLayout(new QPlainTextDocumentLayout(tab.document));
        tab.document->setPlainText(section.code);
        tab.document->setModified(false);
        tab.highlighter = new GmlHighlighter(tab.document);
        tab.highlighter->setResources(*m_project);
        tab.cursor = QTextCursor(tab.document);
        connect(tab.document, &QTextDocument::contentsChanged, this, &ScriptEditorWindow::synchronizeSource);
        m_tabs.append(tab);
    }
    rebuildTabs(0);
    for (const auto &tab : oldTabs) delete tab.document;
}

void ScriptEditorWindow::rebuildTabs(int selected)
{
    const QSignalBlocker blocker(m_tabBar);
    while (m_tabBar->count()) m_tabBar->removeTab(0);
    for (const auto &tab : m_tabs) m_tabBar->addTab(tab.name);
    const int addIndex = m_tabBar->addTab(QStringLiteral("   "));
    m_tabBar->setTabToolTip(addIndex, tr("Create a new script"));
    for (auto side : {QTabBar::LeftSide, QTabBar::RightSide}) {
        QWidget *button = m_tabBar->tabButton(addIndex, side);
        m_tabBar->setTabButton(addIndex, side, nullptr);
        if (button) button->deleteLater();
    }
    m_tabBar->setCurrentIndex(qBound(0, selected, m_tabs.size() - 1));
    changeTab(m_tabBar->currentIndex());
}

void ScriptEditorWindow::changeTab(int index)
{
    if (index < 0) return;
    if (index == m_tabs.size()) { addTab(); return; }
    if (index >= m_tabs.size()) return;
    auto *editor = m_codePanel->editor();
    if (m_currentTab >= 0 && m_currentTab < m_tabs.size()
        && editor->document() == m_tabs[m_currentTab].document) {
        m_tabs[m_currentTab].cursor = editor->textCursor();
        m_tabs[m_currentTab].scroll = editor->verticalScrollBar()->value();
    }
    m_currentTab = index;
    auto &tab = m_tabs[index];
    m_codePanel->setTextDocument(tab.document);
    editor->setTextCursor(tab.cursor);
    editor->verticalScrollBar()->setValue(tab.scroll);
    m_nameEdit->setText(tab.name);
    editor->setFocus();
}

bool ScriptEditorWindow::nameAvailable(const QString &name, int except, QString &error) const
{
    if (!m_project->validateResourceName(ResourceType::Script, filePath(), name, error)) return false;
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (i != except && m_tabs.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            error = tr("A script named %1 already exists.").arg(name);
            return false;
        }
    }
    const QString current = except >= 0 ? m_tabs.at(except).name : QString();
    for (const auto &item : GmlSymbols::completionItems(*m_project)) {
        if (item.name.compare(name, Qt::CaseInsensitive) == 0 && item.name != current) {
            error = tr("A script or built-in symbol named %1 already exists.").arg(name);
            return false;
        }
    }
    return true;
}

void ScriptEditorWindow::addTab()
{
    ScriptTab tab;
    QString error;
    int number = 0;
    do { tab.name = QStringLiteral("script%1").arg(number++); } while (!nameAvailable(tab.name, -1, error));
    tab.document = new QTextDocument(this);
    tab.document->setDocumentLayout(new QPlainTextDocumentLayout(tab.document));
    tab.highlighter = new GmlHighlighter(tab.document);
    tab.highlighter->setResources(*m_project);
    tab.cursor = QTextCursor(tab.document);
    connect(tab.document, &QTextDocument::contentsChanged, this, &ScriptEditorWindow::synchronizeSource);
    m_tabs.append(tab);
    m_definitions = true;
    rebuildTabs(m_tabs.size() - 1);
    synchronizeSource();
}

void ScriptEditorWindow::removeTab(int index)
{
    if (index < 0 || index >= m_tabs.size()) return;
    if (m_tabs.size() == 1) { close(); return; }
    if (EditorMessageBox::question(this, tr("Delete Script"),
            tr("Delete script %1 and its code from this file?").arg(m_tabs.at(index).name),
            EditorMessageBox::Yes | EditorMessageBox::No, EditorMessageBox::No) != EditorMessageBox::Yes) return;
    changeTab(index);
    const auto removed = m_tabs.takeAt(index);
    m_currentTab = -1;
    rebuildTabs(qMin(index, m_tabs.size() - 1));
    delete removed.document;
    synchronizeSource();
}

void ScriptEditorWindow::renameTab()
{
    if (m_currentTab < 0) return;
    const QString name = m_nameEdit->text().trimmed();
    const QString original = m_tabs.at(m_currentTab).name;
    if (name == original) return;
    QString error;
    if (!nameAvailable(name, m_currentTab, error)) {
        m_nameEdit->setText(original);
        EditorMessageBox::warning(this, tr("Cannot Rename Script"), error);
        return;
    }
    if (!m_definitions) {
        m_nameEdit->setText(original);
        emit renameResourceRequested(ResourceType::Script, filePath(), name);
        return;
    }
    m_tabs[m_currentTab].name = name;
    m_tabBar->setTabText(m_currentTab, name);
    m_nameEdit->setText(name);
    synchronizeSource();
}

void ScriptEditorWindow::synchronizeSource()
{
    QVector<ScriptSection> sections;
    for (const auto &tab : m_tabs) {
        ScriptSection section;
        section.name = tab.name;
        section.code = tab.document->toPlainText();
        sections.append(section);
    }
    const QString source = ScriptSource::combine(sections, m_definitions);
    auto *document = m_document->textDocument();
    if (document->toPlainText() != source) document->setPlainText(source);
    document->setModified(source != m_savedSource);
}

void ScriptEditorWindow::refreshSymbols()
{
    for (const auto &tab : m_tabs) tab.highlighter->setResources(*m_project);
    m_codePanel->editor()->setCompletionItems(GmlSymbols::completionItems(*m_project));
}

void ScriptEditorWindow::selectCode(int offset, int length)
{
    const auto sections = ScriptSource::sections(m_document->textDocument()->toPlainText(), m_document->name());
    int index = 0;
    for (int i = 0; i < sections.size(); ++i)
        if (offset >= sections.at(i).headerOffset) index = i;
    m_tabBar->setCurrentIndex(index);
    if (offset < sections.at(index).offset) {
        m_nameEdit->setFocus();
        m_nameEdit->selectAll();
        return;
    }
    m_codePanel->selectRange(qMax(0, offset - sections.at(index).offset), length);
}

void ScriptEditorWindow::selectScript(const QString &name)
{
    for (int i = 0; i < m_tabs.size(); ++i)
        if (m_tabs.at(i).name == name) { m_tabBar->setCurrentIndex(i); return; }
}
