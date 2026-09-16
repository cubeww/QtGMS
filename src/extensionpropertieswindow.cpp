#include "extensionpropertieswindow.h"
#include "extensioneditordialogs.h"
#include "actionxml.h"
#include "codesnippeteditorwindow.h"
#include "editorstandarddialogs.h"
#include <QAction>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTextCodec>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

static QString extensionSelectionKey(QTreeWidgetItem *item)
{
    if (!item) return QString();
    return item->data(0, Qt::UserRole).toString() + QLatin1Char('|') + item->data(0, Qt::UserRole + 1).toString() + QLatin1Char('|') + item->data(0, Qt::UserRole + 2).toString();
}
ExtensionPropertiesWindow::ExtensionPropertiesWindow(ExtensionDocument *document, QWidget *parent)
    : ResourceEditorWindow(parent), m_document(document), m_tree(new QTreeWidget), m_description(new QLabel)
{
    document->setParent(this); setAttribute(Qt::WA_DeleteOnClose); editorMenuBar()->hide(); resize(800, 540); setMinimumSize(580, 330);
    auto *body = new QWidget; auto *layout = new QVBoxLayout(body); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(3);
    auto *toolbar = new QToolBar; toolbar->setMovable(false); toolbar->setIconSize(QSize(16, 16)); toolbar->setFixedHeight(28); layout->addWidget(toolbar);
    auto *ok = toolbar->addAction(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("OK, Save changes")); connect(ok, &QAction::triggered, this, [this] { if (save()) close(); });
    auto *undo = document->undoStack()->createUndoAction(this); undo->setIcon(QIcon(QStringLiteral(":/images/editor/undo.png"))); undo->setShortcut(QKeySequence::Undo); addAction(undo); toolbar->addAction(undo);
    auto *redo = document->undoStack()->createRedoAction(this); redo->setIcon(QIcon(QStringLiteral(":/images/editor/redo.png"))); redo->setShortcut(QKeySequence::Redo); addAction(redo); toolbar->addAction(redo); toolbar->addSeparator();
    auto *file = toolbar->addAction(QIcon(QStringLiteral(":/images/open.png")), tr("Add File...")); connect(file, &QAction::triggered, this, [this] { addFile(false); });
    auto *placeholder = toolbar->addAction(QIcon(QStringLiteral(":/images/new.png")), tr("Add Placeholder...")); connect(placeholder, &QAction::triggered, this, [this] { addFile(true); });
    m_addFunction = toolbar->addAction(QIcon(QStringLiteral(":/images/script.png")), tr("Add Function...")); connect(m_addFunction, &QAction::triggered, this, [this] { addMember(true); });
    m_addConstant = toolbar->addAction(QIcon(QStringLiteral(":/images/tree/macro.png")), tr("Add Constant...")); connect(m_addConstant, &QAction::triggered, this, [this] { addMember(false); }); toolbar->addSeparator();
    m_edit = toolbar->addAction(QIcon(QStringLiteral(":/object/controls/change.png")), tr("Properties...")); connect(m_edit, &QAction::triggered, this, &ExtensionPropertiesWindow::editSelected);
    m_editSource = toolbar->addAction(QIcon(QStringLiteral(":/images/code/completion.png")), tr("Edit GML...")); connect(m_editSource, &QAction::triggered, this, &ExtensionPropertiesWindow::editSource);
    m_delete = toolbar->addAction(QIcon(QStringLiteral(":/object/controls/delete.png")), tr("Delete")); connect(m_delete, &QAction::triggered, this, &ExtensionPropertiesWindow::removeSelected);
    auto *rename = toolbar->addAction(QIcon(QStringLiteral(":/images/resourcecommands/rename.png")), tr("Rename"));
    connect(rename, &QAction::triggered, this, [this] { emit renameResourceRequested(ResourceType::Extension, filePath(), QString()); });
    m_tree->setHeaderLabels({tr("Name"), tr("Type / Declaration")}); m_tree->setColumnWidth(0, 290); m_tree->setUniformRowHeights(true); m_tree->setContextMenuPolicy(Qt::CustomContextMenu); layout->addWidget(m_tree, 1);
    m_description->setMargin(5); m_description->setWordWrap(true); layout->addWidget(m_description); setCentralWidget(body);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &ExtensionPropertiesWindow::updateActions);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this] { editSelected(); });
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this, file, placeholder](const QPoint &point) {
        if (auto *item = m_tree->itemAt(point)) m_tree->setCurrentItem(item);
        QMenu menu(this); menu.addAction(file); menu.addAction(placeholder); menu.addSeparator(); menu.addAction(m_addFunction); menu.addAction(m_addConstant); menu.addSeparator(); menu.addAction(m_edit); menu.addAction(m_editSource); menu.addAction(m_delete); menu.exec(m_tree->viewport()->mapToGlobal(point));
    });
    connect(document, &ExtensionDocument::changed, this, &ExtensionPropertiesWindow::refresh);
    connect(document->undoStack(), &QUndoStack::cleanChanged, this, [this] { setWindowTitle(tr("Extension Properties: %1%2").arg(m_document->name(), m_document->isModified() ? QStringLiteral(" *") : QString())); });
    connect(document, &ExtensionDocument::saved, this, [this] { emit resourceSaved(ResourceType::Extension, filePath(), QString()); });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this); connect(saveShortcut, &QShortcut::activated, this, &ExtensionPropertiesWindow::saveProjectRequested);
    refresh();
}
ExtensionPropertiesWindow::~ExtensionPropertiesWindow()
{
    for (QObject *child : findChildren<QObject *>()) QObject::disconnect(child, nullptr, this, nullptr);
    delete takeCentralWidget();
}
void ExtensionPropertiesWindow::refresh()
{
    m_refreshing = true; QSignalBlocker blocker(m_tree); const QString selection = extensionSelectionKey(m_tree->currentItem());
    QSet<QString> collapsed; for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *root = m_tree->topLevelItem(i); for (int j = 0; j < root->childCount(); ++j) if (!root->child(j)->isExpanded()) collapsed.insert(extensionSelectionKey(root->child(j)));
    }
    m_tree->clear(); const auto state = m_document->state(); const auto xmlRoot = state.xml.documentElement();
    auto *root = new QTreeWidgetItem(m_tree, {m_document->name(), tr("Extension %1").arg(ActionXml::text(xmlRoot, QStringLiteral("version")))}); root->setIcon(0, QIcon(QStringLiteral(":/images/tree/extension.png"))); root->setData(0, Qt::UserRole, QStringLiteral("package"));
    QTreeWidgetItem *selected = root;
    for (const auto &file : ActionXml::elements(xmlRoot.firstChildElement(QStringLiteral("files")), QStringLiteral("file"))) {
        const QString fileName = ActionXml::text(file, QStringLiteral("filename")); const int kind = ActionXml::text(file, QStringLiteral("kind")).toInt();
        auto *item = new QTreeWidgetItem(root, {fileName, kind == 2 ? QStringLiteral("GML") : kind == 5 ? QStringLiteral("JavaScript") : kind == 1 ? tr("Native library") : tr("Extension file")});
        item->setIcon(0, QIcon(QStringLiteral(":/images/new.png"))); item->setData(0, Qt::UserRole, QStringLiteral("file")); item->setData(0, Qt::UserRole + 1, fileName); item->setData(0, Qt::UserRole + 3, kind);
        if (extensionSelectionKey(item) == selection) selected = item;
        for (const QString &tag : {QStringLiteral("function"), QStringLiteral("constant")}) {
            int index = 0;
            for (const auto &member : ActionXml::elements(file.firstChildElement(tag + QLatin1Char('s')), tag)) {
                const QString name = ActionXml::text(member, QStringLiteral("name"));
                auto *child = new QTreeWidgetItem(item, {name, ActionXml::text(member, tag == QStringLiteral("function") ? QStringLiteral("help") : QStringLiteral("value"))});
                child->setIcon(0, QIcon(tag == QStringLiteral("function") ? QStringLiteral(":/images/script.png") : QStringLiteral(":/images/tree/macro.png")));
                child->setData(0, Qt::UserRole, tag); child->setData(0, Qt::UserRole + 1, fileName); child->setData(0, Qt::UserRole + 2, index++); child->setData(0, Qt::UserRole + 3, kind);
                if (extensionSelectionKey(child) == selection) selected = child;
            }
        }
        item->setExpanded(!collapsed.contains(extensionSelectionKey(item)));
    }
    root->setExpanded(true); m_tree->setCurrentItem(selected);
    m_description->setText(ActionXml::text(xmlRoot, QStringLiteral("description")));
    setWindowTitle(tr("Extension Properties: %1%2").arg(m_document->name(), m_document->isModified() ? QStringLiteral(" *") : QString()));
    m_refreshing = false; blocker.unblock(); updateActions();
}
void ExtensionPropertiesWindow::updateActions()
{
    if (m_refreshing) return;
    auto *item = m_tree->currentItem(); const bool file = item && item->data(0, Qt::UserRole).toString() != QStringLiteral("package");
    m_edit->setEnabled(item); m_delete->setEnabled(file); m_addFunction->setEnabled(file); m_addConstant->setEnabled(file);
    m_editSource->setEnabled(file && item->data(0, Qt::UserRole + 3).toInt() == 2);
}
QDomElement ExtensionPropertiesWindow::selectedFile(ExtensionState &state) const
{
    if (!m_tree->currentItem()) return QDomElement();
    const QString name = m_tree->currentItem()->data(0, Qt::UserRole + 1).toString();
    for (auto file : ActionXml::elements(state.xml.documentElement().firstChildElement(QStringLiteral("files")), QStringLiteral("file"))) if (ActionXml::text(file, QStringLiteral("filename")) == name) return file;
    return QDomElement();
}
void ExtensionPropertiesWindow::addFile(bool placeholder)
{
    QStringList paths;
    if (placeholder) {
        bool accepted; QString name = EditorInputDialog::getText(this, tr("Add Placeholder"), tr("Name:"), QLineEdit::Normal, QStringLiteral("extension.ext"), &accepted);
        if (!accepted) return;
        if (!ExtensionDocument::validFileName(name) || ExtensionDocument::fileKind(name) != 4) { EditorMessageBox::warning(this, tr("Placeholder Name"), tr("Enter a relative filename with a placeholder extension such as .ext.")); return; }
        paths.append(name);
    } else paths = QFileDialog::getOpenFileNames(this, tr("Add Extension Files"), QString(), tr("Extension files (*.dll *.gml *.js *.so *.dylib *.lib *.a *.ext);;All files (*)"));
    if (paths.isEmpty()) return;
    ExtensionState state = m_document->state(); auto files = ActionXml::child(state.xml.documentElement(), QStringLiteral("files")); QString error;
    for (const QString &path : paths) {
        const QString name = placeholder ? path : QFileInfo(path).fileName();
        for (auto file : ActionXml::elements(files, QStringLiteral("file"))) if (ActionXml::text(file, QStringLiteral("filename")).compare(name, Qt::CaseInsensitive) == 0) { EditorMessageBox::warning(this, tr("Add File"), tr("The extension already contains %1.").arg(name)); return; }
        QByteArray bytes;
        if (!placeholder) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) { EditorMessageBox::critical(this, tr("Add File"), file.errorString()); return; } bytes = file.readAll(); if (file.error() != QFile::NoError) { EditorMessageBox::critical(this, tr("Add File"), file.errorString()); return; } }
        if (QFileInfo::exists(QDir(m_document->contentDirectory()).filePath(name))) { EditorMessageBox::warning(this, tr("Add File"), tr("A file named %1 already exists in the extension directory. Choose a different filename.").arg(name)); return; }
        if (!m_document->stageFile(state, name, bytes, error)) { EditorMessageBox::critical(this, tr("Add File"), error); return; }
        auto file = state.xml.createElement(QStringLiteral("file")); files.appendChild(file);
        ActionXml::setText(file, QStringLiteral("filename"), name); ActionXml::setText(file, QStringLiteral("origname"), QStringLiteral("extensions\\") + name);
        ActionXml::setText(file, QStringLiteral("kind"), QString::number(ExtensionDocument::fileKind(name))); ActionXml::setText(file, QStringLiteral("init"), QString()); ActionXml::setText(file, QStringLiteral("final"), QString()); ActionXml::setText(file, QStringLiteral("uncompress"), QStringLiteral("0"));
        for (const QString &key : {QStringLiteral("functions"), QStringLiteral("constants"), QStringLiteral("ProxyFiles")}) ActionXml::child(file, key);
        const auto config = state.xml.documentElement().firstChildElement(QStringLiteral("ConfigOptions")); file.appendChild(config.cloneNode(true));
    }
    m_document->edit(state, placeholder ? tr("Add extension placeholder") : tr("Add extension files"));
}
static bool extensionMemberUnique(QDomElement file, const QDomElement &member)
{
    const QString name = ActionXml::text(member, QStringLiteral("name"));
    for (const QString &tag : {QStringLiteral("function"), QStringLiteral("constant")})
        for (auto other : ActionXml::elements(file.firstChildElement(tag + QLatin1Char('s')), tag)) if (other != member && ActionXml::text(other, QStringLiteral("name")) == name) return false;
    return true;
}
void ExtensionPropertiesWindow::addMember(bool function)
{
    auto state = m_document->state(); auto file = selectedFile(state); if (file.isNull()) return;
    const QString tag = function ? QStringLiteral("function") : QStringLiteral("constant"); auto member = state.xml.createElement(tag); ActionXml::child(file, tag + QLatin1Char('s')).appendChild(member);
    int number = 0; do { ActionXml::setText(member, QStringLiteral("name"), tag + QString::number(number++)); } while (!extensionMemberUnique(file, member));
    const int kind = ActionXml::text(file, QStringLiteral("kind")).toInt();
    ActionXml::setText(member, QStringLiteral("externalName"), ActionXml::text(member, QStringLiteral("name")));
    if (function) {
        ActionXml::setText(member, QStringLiteral("kind"), QString::number(kind == 1 ? 11 : kind)); ActionXml::setText(member, QStringLiteral("returnType"), QStringLiteral("2")); ActionXml::setText(member, QStringLiteral("argCount"), QStringLiteral("0")); ActionXml::child(member, QStringLiteral("args"));
    } else { member.removeChild(member.firstChildElement(QStringLiteral("externalName"))); ActionXml::setText(member, QStringLiteral("value"), QStringLiteral("0")); }
    while (function ? ExtensionEditorDialogs::function(member, kind, this) : ExtensionEditorDialogs::constant(member, this)) {
        if (!extensionMemberUnique(file, member)) { EditorMessageBox::warning(this, tr("Duplicate Name"), tr("This file already declares that name.")); continue; }
        m_document->edit(state, function ? tr("Add extension function") : tr("Add extension constant")); return;
    }
}
void ExtensionPropertiesWindow::editSelected()
{
    auto *item = m_tree->currentItem(); if (!item) return;
    const QString tag = item->data(0, Qt::UserRole).toString(); auto state = m_document->state(); auto file = selectedFile(state); bool accepted = false;
    if (tag == QStringLiteral("package")) accepted = ExtensionEditorDialogs::package(state.xml.documentElement(), this);
    else if (tag == QStringLiteral("file")) accepted = ExtensionEditorDialogs::file(file, m_document, state, this);
    else {
        auto member = ActionXml::elements(file.firstChildElement(tag + QLatin1Char('s')), tag).value(item->data(0, Qt::UserRole + 2).toInt()); if (member.isNull()) return;
        const QString oldName = ActionXml::text(member, QStringLiteral("name"));
        while (tag == QStringLiteral("function") ? ExtensionEditorDialogs::function(member, ActionXml::text(file, QStringLiteral("kind")).toInt(), this) : ExtensionEditorDialogs::constant(member, this)) {
            if (!extensionMemberUnique(file, member)) { EditorMessageBox::warning(this, tr("Duplicate Name"), tr("This file already declares that name.")); continue; }
            if (tag == QStringLiteral("function")) for (const QString &hook : {QStringLiteral("init"), QStringLiteral("final")}) if (ActionXml::text(file, hook) == oldName) ActionXml::setText(file, hook, ActionXml::text(member, QStringLiteral("name")));
            accepted = true; break;
        }
    }
    if (accepted) m_document->edit(state, tr("Edit extension properties"));
}
void ExtensionPropertiesWindow::removeSelected()
{
    auto *item = m_tree->currentItem(); if (!item || item->data(0, Qt::UserRole).toString() == QStringLiteral("package")) return;
    if (EditorMessageBox::question(this, tr("Delete Extension Item"), tr("Remove %1 from this extension?").arg(item->text(0)), EditorMessageBox::Yes | EditorMessageBox::No, EditorMessageBox::No) != EditorMessageBox::Yes) return;
    auto state = m_document->state(); auto file = selectedFile(state); const QString tag = item->data(0, Qt::UserRole).toString();
    if (tag == QStringLiteral("file")) { state.files.remove(ActionXml::text(file, QStringLiteral("filename"))); file.parentNode().removeChild(file); }
    else {
        auto member = ActionXml::elements(file.firstChildElement(tag + QLatin1Char('s')), tag).value(item->data(0, Qt::UserRole + 2).toInt());
        if (tag == QStringLiteral("function")) for (const QString &hook : {QStringLiteral("init"), QStringLiteral("final")}) if (ActionXml::text(file, hook) == ActionXml::text(member, QStringLiteral("name"))) ActionXml::setText(file, hook, QString());
        member.parentNode().removeChild(member);
    }
    m_document->edit(state, tr("Remove extension item"));
}
void ExtensionPropertiesWindow::editSource()
{
    auto state = m_document->state(); const auto file = selectedFile(state); if (file.isNull() || ActionXml::text(file, QStringLiteral("kind")).toInt() != 2) return;
    const QString name = ActionXml::text(file, QStringLiteral("filename")); QByteArray bytes; QString error;
    if (!m_document->fileData(name, bytes, error)) { EditorMessageBox::critical(this, tr("Cannot Open GML"), error); return; }
    const QString source = QTextCodec::codecForUtfText(bytes, QTextCodec::codecForName("UTF-8"))->toUnicode(bytes);
    CodeSnippetEditorWindow editor(tr("Extension Code: %1").arg(name), name, source, QFileInfo(name).completeBaseName(), this);
    if (!editor.exec() || editor.code() == source) return;
    if (!m_document->stageFile(state, name, editor.code().toUtf8(), error)) { EditorMessageBox::critical(this, tr("Cannot Edit GML"), error); return; }
    m_document->edit(state, tr("Edit extension GML"));
}
QString ExtensionPropertiesWindow::filePath() const { return m_document->filePath(); }
void ExtensionPropertiesWindow::relocate(const QString &oldDirectory, const QString &newDirectory) { m_document->relocate(oldDirectory, newDirectory); refresh(); }
bool ExtensionPropertiesWindow::save()
{
    QString error; if (m_document->save(error)) return true;
    EditorMessageBox::critical(this, tr("Cannot Save Extension"), error); return false;
}
void ExtensionPropertiesWindow::closeEvent(QCloseEvent *event)
{
    if (m_document->isModified()) {
        const auto answer = EditorMessageBox::question(this, tr("Extension Properties"), tr("Save changes to %1?").arg(m_document->name()), EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel || (answer == EditorMessageBox::Save && !save())) { event->ignore(); return; }
    }
    event->accept();
}
