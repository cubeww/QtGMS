#include "objectpropertieswindow.h"
#include "objectdocument.h"
#include "objecteventdialog.h"
#include "codeactioneditorwindow.h"
#include "actionxml.h"
#include "editorstandarddialogs.h"

static QDomElement findCodeAction(QDomDocument &xml, const QString &id)
{
    const auto actions = xml.elementsByTagName(QStringLiteral("action"));
    for (int index = 0; index < actions.size(); ++index) {
        const auto action = actions.at(index).toElement();
        if (ActionXml::editorId(action) == id) return action;
    }
    return QDomElement();
}

static QByteArray actionBytes(QDomElement action)
{
    QDomDocument xml;
    xml.appendChild(xml.importNode(action, true));
    return xml.toByteArray();
}

void ObjectPropertiesWindow::openCodeAction(int row, int offset, int length)
{
    QDomDocument xml = m_document->xml();
    const auto event = selectedEvent(xml);
    const auto actions = ActionXml::elements(event, QStringLiteral("action"));
    if (row < 0 || row >= actions.size()) return;
    const auto action = actions.at(row);
    const QString id = ActionXml::editorId(action);
    auto *editor = m_codeEditors.value(id).data();
    if (!editor) {
        const QString context = m_document->name() + QLatin1Char('_') + ObjectEventDialog::eventName(event)
            + QStringLiteral("_%1").arg(row + 1);
        editor = new CodeActionEditorWindow(action, *m_project, context, this);
        editor->setAttribute(Qt::WA_DeleteOnClose);
        m_codeEditors.insert(id, editor);
        editor->setCommitHandler([this, id, editor](QDomElement original, QDomElement edited) {
            QDomDocument xml = m_document->xml();
            auto current = findCodeAction(xml, id);
            if (current.isNull() || actionBytes(current) != actionBytes(original)) {
                editor->showNormal(); editor->raise(); editor->activateWindow();
                EditorMessageBox::warning(editor, tr("Cannot Save Code"), current.isNull()
                    ? tr("This action was deleted. Restore it with Undo, or copy the code elsewhere before discarding this window.")
                    : tr("This action changed while its code was being edited. Copy your changes before discarding and reopening this window."));
                return false;
            }
            current.parentNode().replaceChild(xml.importNode(edited, true), current);
            m_document->edit(xml, tr("Edit action"));
            return true;
        });
        connect(editor, &CodeSnippetEditorWindow::finished, this, [this, id] { m_codeEditors.remove(id); });
    }
    if (offset >= 0) editor->selectRange(offset, length);
    if (editor->isMinimized()) editor->showNormal(); else editor->show();
    editor->raise(); editor->activateWindow();
}

void ObjectPropertiesWindow::refreshCodeEditors()
{
    QDomDocument xml = m_document->xml();
    const auto editors = m_codeEditors;
    for (auto it = editors.constBegin(); it != editors.constEnd(); ++it) {
        auto *editor = it.value().data();
        if (!editor) continue;
        const auto action = findCodeAction(xml, it.key());
        if (action.isNull()) {
            if (!editor->hasChanges()) editor->close();
            continue;
        }
        editor->refreshAction(action);
        const auto event = action.parentNode().toElement();
        const int row = ActionXml::elements(event, QStringLiteral("action")).indexOf(action);
        editor->setWindowTitle(CodeActionEditorWindow::tr("Event: %1").arg(m_document->name() + QLatin1Char('_')
            + ObjectEventDialog::eventName(event) + QStringLiteral("_%1").arg(row + 1)));
    }
}

bool ObjectPropertiesWindow::saveCodeEditors()
{
    const auto editors = m_codeEditors.values();
    for (const auto &editor : editors)
        if (editor && !editor->commitChanges()) return false;
    return true;
}

QDomDocument ObjectPropertiesWindow::editingXml() const
{
    QDomDocument xml = m_document->xml();
    for (auto it = m_codeEditors.constBegin(); it != m_codeEditors.constEnd(); ++it) {
        if (!it.value()) continue;
        const auto action = findCodeAction(xml, it.key());
        if (!action.isNull()) action.parentNode().replaceChild(xml.importNode(it.value()->action(), true), action);
    }
    return xml;
}

bool ObjectPropertiesWindow::closeCodeEditors()
{
    const auto editors = m_codeEditors.values();
    for (const auto &editor : editors)
        if (editor && !editor->close()) return false;
    return true;
}
