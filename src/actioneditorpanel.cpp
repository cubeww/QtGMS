#include "actioneditorpanel.h"
#include "actionxml.h"
#include "actionlibrary.h"
#include "actionpalette.h"
#include "actioneditordialog.h"
#include "codeactioneditorwindow.h"
#include "editorstandarddialogs.h"
#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QShortcut>
#include <QUndoStack>
#include <QVBoxLayout>

static const QString ActionsMime = QStringLiteral("application/x-qtgms-actions+xml");
ActionEditorPanel::ActionEditorPanel(Project *project, ActionLibraryManager *libraries, QUndoStack *undoStack, QWidget *parent)
    : QWidget(parent), m_project(project), m_libraries(libraries), m_actions(new ActionList), m_palette(new ActionPalette(libraries))
{
    auto *layout = new QHBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(6);
    auto *actions = new QVBoxLayout; m_listLayout = actions; actions->setContentsMargins(3, 9, 3, 10); actions->setSpacing(5); actions->addWidget(new QLabel(tr("Actions:"))); actions->addWidget(m_actions, 1);
    m_actions->setMinimumWidth(185); layout->addLayout(actions, 1); layout->addWidget(m_palette);
    connect(m_actions, &QListWidget::itemDoubleClicked, this, [this] { editAction(); });
    connect(m_palette, &ActionPalette::actionRequested, this, [this](int libraryId, int id) { addAction(libraryId, id); });
    connect(m_actions, &ActionList::actionDropped, this, &ActionEditorPanel::addAction);
    connect(m_actions, &ActionList::actionsMoved, this, &ActionEditorPanel::moveActions);
    connect(libraries, &ActionLibraryManager::changed, this, &ActionEditorPanel::refreshActions);
    m_actions->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_actions, &QWidget::customContextMenuRequested, this, [this, undoStack](const QPoint &pos) {
        QMenu menu(this); const bool selected = !m_actions->selectedItems().isEmpty();
        auto *edit = menu.addAction(tr("Edit Values..."), this, &ActionEditorPanel::editAction); edit->setEnabled(selected);
        menu.addSeparator(); auto *cut = menu.addAction(tr("Cut"), this, [this] { copyActions(); deleteActions(); });
        auto *copy = menu.addAction(tr("Copy"), this, &ActionEditorPanel::copyActions); auto *remove = menu.addAction(tr("Delete"), this, &ActionEditorPanel::deleteActions);
        for (QAction *action : {cut, copy, remove}) action->setEnabled(selected);
        auto *paste = menu.addAction(tr("Paste"), this, &ActionEditorPanel::pasteActions); paste->setEnabled(!m_xml.documentElement().isNull() && QApplication::clipboard()->mimeData()->hasFormat(ActionsMime));
        menu.addAction(tr("Select All"), m_actions, &QListWidget::selectAll); menu.addSeparator();
        menu.addAction(undoStack->createUndoAction(&menu)); menu.addAction(undoStack->createRedoAction(&menu)); menu.exec(m_actions->viewport()->mapToGlobal(pos));
    });
    auto bind = [this](const QKeySequence &key, QWidget *owner, const std::function<void()> &function) { auto *shortcut = new QShortcut(key, owner); shortcut->setContext(Qt::WidgetWithChildrenShortcut); connect(shortcut, &QShortcut::activated, this, function); };
    bind(QKeySequence::Delete, m_actions, [this] { deleteActions(); });
    bind(QKeySequence::Copy, m_actions, [this] { copyActions(); }); bind(QKeySequence::Cut, m_actions, [this] { copyActions(); deleteActions(); }); bind(QKeySequence::Paste, m_actions, [this] { pasteActions(); });
    bind(QKeySequence(Qt::Key_Return), m_actions, [this] { editAction(); });

    refreshActions();
}
void ActionEditorPanel::refreshActions()
{
    const int row = m_actions->currentRow(); m_actions->clear(); QDomDocument xml = m_xml.cloneNode(true).toDocument(); const QDomElement event = xml.documentElement();
    m_palette->setHasActionContainer(!event.isNull()); m_actions->setEnabled(!event.isNull()); int number = 0, indent = 0;
    for (QDomElement action : ActionXml::elements(event, QStringLiteral("action"))) {
        const int kind = ActionXml::text(action, QStringLiteral("kind")).toInt(); if (kind == 2) indent = qMax(0, indent - 1);
        const LibraryAction *definition = m_libraries->action(ActionXml::text(action, QStringLiteral("libid")).toInt(), ActionXml::text(action, QStringLiteral("id")).toInt());
        const QString description = ActionXml::actionText(action, *m_libraries);
        auto *item = new QListWidgetItem(definition ? definition->icon : QIcon(QStringLiteral(":/images/script.png")), QStringLiteral("%1  ").arg(++number) + QString(indent * 3, QLatin1Char(' ')) + description, m_actions);
        item->setToolTip(description); item->setSizeHint(QSize(0, 28)); if (kind == 1) ++indent;
    }
    if (m_actions->count()) m_actions->setCurrentRow(qBound(0, row, m_actions->count() - 1));
}
void ActionEditorPanel::addAction(int libraryId, int actionId, int before)
{
    const LibraryAction *found = m_libraries->action(libraryId, actionId); if (!found || found->kind >= 8) return;
    const LibraryAction definition = *found; QDomDocument xml = m_xml.cloneNode(true).toDocument(); QDomElement event = xml.documentElement(); if (event.isNull()) return;
    QDomElement action = ActionXml::createAction(xml, definition);
    const auto actions = ActionXml::elements(event, QStringLiteral("action"));
    if (before < 0) before = m_actions->currentRow() < 0 ? actions.size() : m_actions->currentRow() + 1;
    before = qBound(0, before, actions.size());
    if (m_modelessCodeEditing && (definition.kind == 7 || definition.interfaceKind == 5)) {
        if (before < actions.size()) event.insertBefore(action, actions.at(before)); else event.appendChild(action);
        commit(xml, tr("Add action"));
        m_actions->setCurrentRow(before);
        emit codeActionRequested(before, -1, 0);
        return;
    }
    if (definition.kind == 7 || definition.interfaceKind == 5 || (definition.interfaceKind != 1 &&
        (definition.canApplyTo || definition.allowRelative || definition.question || !definition.arguments.isEmpty()))) {
        const QDomElement edited = editActionValues(action, &definition, before); if (edited.isNull()) return;
        action = xml.importNode(edited, true).toElement();
    }
    if (before < actions.size()) event.insertBefore(action, actions.at(before)); else event.appendChild(action);
    commit(xml, tr("Add action")); m_actions->setCurrentRow(before);
}
void ActionEditorPanel::editAction()
{
    openAction(m_actions->currentRow());
}
void ActionEditorPanel::openAction(int row, int offset, int length, int argument)
{
    QDomDocument xml = m_xml.cloneNode(true).toDocument(); QDomElement event = xml.documentElement(); const auto actions = ActionXml::elements(event, QStringLiteral("action"));
    if (row < 0 || row >= actions.size()) return; const QDomElement action = actions.at(row);
    m_actions->setCurrentRow(row);
    const LibraryAction *found = m_libraries->action(ActionXml::text(action, QStringLiteral("libid")).toInt(), ActionXml::text(action, QStringLiteral("id")).toInt());
    LibraryAction definition; if (found) definition = *found;
    if (m_modelessCodeEditing && (ActionXml::text(action, QStringLiteral("kind")).toInt() == 7 || (found && definition.interfaceKind == 5))) {
        emit codeActionRequested(row, argument == 0 ? offset : -1, length);
        return;
    }
    const QDomElement edited = editActionValues(action, found ? &definition : nullptr, row, offset, length, argument); if (edited.isNull()) return;
    event.replaceChild(xml.importNode(edited, true), action); commit(xml, tr("Edit action"));
}
void ActionEditorPanel::openFirstCodeAction()
{
    const auto event = m_xml.documentElement();
    if (event.isNull()) return;
    const auto actions = ActionXml::elements(event, QStringLiteral("action"));
    for (int row = 0; row < actions.size(); ++row) {
        const auto action = actions.at(row);
        const auto *definition = m_libraries->action(ActionXml::text(action, QStringLiteral("libid")).toInt(), ActionXml::text(action, QStringLiteral("id")).toInt());
        if (ActionXml::text(action, QStringLiteral("kind")).toInt() == 7 || (definition && definition->interfaceKind == 5)) {
            m_actions->setCurrentRow(row); editAction(); return;
        }
    }
    // The built-in Execute Code action uses the same insertion path as the palette.
    const auto *code = m_libraries->action(1, 603);
    if (!code || (code->kind != 7 && code->interfaceKind != 5)) {
        EditorMessageBox::warning(this, tr("Execute Code"), tr("The built-in Execute Code action is missing. Reload the action libraries and try again."));
        return;
    }
    addAction(1, 603, actions.size());
}
QDomElement ActionEditorPanel::editActionValues(QDomElement action, const LibraryAction *definition, int row, int offset, int length, int argument)
{
    if (ActionXml::text(action, QStringLiteral("kind")).toInt() == 7 || (definition && definition->interfaceKind == 5)) {
        CodeActionEditorWindow editor(action, *m_project, m_contextName + QStringLiteral("_%1").arg(row + 1), this);
        if (offset >= 0 && argument == 0) editor.selectRange(offset, length);
        return editor.exec() ? editor.action() : QDomElement();
    }
    ActionEditorDialog dialog(action, definition, *m_project, this);
    if (offset >= 0) dialog.selectArgument(argument, offset, length);
    return dialog.exec() == QDialog::Accepted ? dialog.action() : QDomElement();
}
void ActionEditorPanel::deleteActions()
{
    QDomDocument xml = m_xml.cloneNode(true).toDocument(); QDomElement event = xml.documentElement(); const auto actions = ActionXml::elements(event, QStringLiteral("action"));
    for (QListWidgetItem *item : m_actions->selectedItems()) { const int row = m_actions->row(item); if (row >= 0 && row < actions.size()) event.removeChild(actions.at(row)); }
    commit(xml, tr("Delete actions"));
}
void ActionEditorPanel::copyActions()
{
    QDomDocument xml = m_xml.cloneNode(true).toDocument(); const auto actions = ActionXml::elements(xml.documentElement(), QStringLiteral("action"));
    QDomDocument clipboard; QDomElement root = clipboard.createElement(QStringLiteral("actions")); clipboard.appendChild(root);
    for (int i = 0; i < m_actions->count() && i < actions.size(); ++i) if (m_actions->item(i)->isSelected()) root.appendChild(clipboard.importNode(actions.at(i), true));
    if (!root.hasChildNodes()) return;
    ActionXml::clearEditorIds(root);
    auto *mime = new QMimeData; mime->setData(ActionsMime, clipboard.toByteArray()); QApplication::clipboard()->setMimeData(mime);
}
void ActionEditorPanel::pasteActions()
{
    QDomDocument xml = m_xml.cloneNode(true).toDocument(); QDomElement event = xml.documentElement(); if (event.isNull()) return;
    QDomDocument clipboard; const QByteArray bytes = QApplication::clipboard()->mimeData()->data(ActionsMime);
    if (bytes.size() > 16 * 1024 * 1024 || !clipboard.setContent(bytes) || clipboard.documentElement().tagName() != QStringLiteral("actions")) return;
    ActionXml::clearEditorIds(clipboard.documentElement());
    const auto actions = ActionXml::elements(event, QStringLiteral("action")); const int before = m_actions->currentRow() < 0 ? actions.size() : m_actions->currentRow() + 1;
    for (QDomElement action : ActionXml::elements(clipboard.documentElement(), QStringLiteral("action"))) {
        const QDomNode copy = xml.importNode(action, true); if (before < actions.size()) event.insertBefore(copy, actions.at(before)); else event.appendChild(copy);
    }
    commit(xml, tr("Paste actions")); m_actions->setCurrentRow(before);
}
void ActionEditorPanel::moveActions(const QList<int> &rows, int before)
{
    QDomDocument xml = m_xml.cloneNode(true).toDocument(); QDomElement event = xml.documentElement(); const auto actions = ActionXml::elements(event, QStringLiteral("action"));
    QList<QDomElement> moved; for (int row : rows) if (row >= 0 && row < actions.size()) moved.append(actions.at(row));
    if (moved.isEmpty()) return;
    QDomElement anchor; for (int i = qMax(0, before); i < actions.size(); ++i) if (!rows.contains(i)) { anchor = actions.at(i); break; }
    for (QDomElement action : moved) { event.removeChild(action); if (anchor.isNull()) event.appendChild(action); else event.insertBefore(action, anchor); }
    commit(xml, tr("Move actions")); m_actions->clearSelection();
    const auto reordered = ActionXml::elements(event, QStringLiteral("action"));
    for (QDomElement action : moved) { const int row = reordered.indexOf(action); if (row >= 0 && row < m_actions->count()) m_actions->item(row)->setSelected(true); }
}
void ActionEditorPanel::setActions(QDomElement container, const QString &contextName)
{
    m_contextName = contextName;
    QDomDocument xml;
    if (!container.isNull()) xml.appendChild(xml.importNode(container, true));
    if (xml.toByteArray() == m_xml.toByteArray()) return;
    m_xml = xml; refreshActions();
}
void ActionEditorPanel::setListMargins(int left, int top, int right, int bottom)
{ m_listLayout->setContentsMargins(left, top, right, bottom); }
void ActionEditorPanel::commit(const QDomDocument &xml, const QString &description)
{
    if (xml.toByteArray() == m_xml.toByteArray()) return;
    m_xml = xml.cloneNode(true).toDocument(); refreshActions();
    emit actionsEdited(m_xml.documentElement(), description);
}
