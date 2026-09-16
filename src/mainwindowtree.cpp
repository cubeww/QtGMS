#include "mainwindow.h"
#include "resourcebrowser.h"
#include "resourceeditorwindow.h"
#include "resourcereferences.h"
#include "actionxml.h"
#include "editorstandarddialogs.h"
#include "editordialog.h"
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

static QList<ResourceNode> *treeChildren(QList<ResourceNode> &nodes, const QList<int> &path)
{
    auto *children = &nodes;
    for (int index : path) {
        if (index < 0 || index >= children->size() || !children->at(index).isGroup) return nullptr;
        children = &(*children)[index].children;
    }
    return children;
}

static bool treeNode(const QList<ResourceNode> &nodes, const QList<int> &path, ResourceNode &result)
{
    const auto *children = &nodes;
    for (int i = 0; i < path.size(); ++i) {
        const int index = path.at(i);
        if (index < 0 || index >= children->size()) return false;
        result = children->at(index);
        if (i + 1 < path.size() && !result.isGroup) return false;
        children = &children->at(index).children;
    }
    return !path.isEmpty();
}

static bool treePrefix(const QList<int> &prefix, const QList<int> &path)
{
    if (prefix.size() > path.size()) return false;
    for (int i = 0; i < prefix.size(); ++i) if (prefix.at(i) != path.at(i)) return false;
    return true;
}

static QString uniqueGroupName(const QList<ResourceNode> &siblings, const QString &base)
{
    QString name = base; int suffix = 1;
    while (true) {
        bool found = false;
        for (const auto &node : siblings) if (node.isGroup && node.name.compare(name, Qt::CaseInsensitive) == 0) { found = true; break; }
        if (!found) return name;
        name = base + QString::number(suffix++);
    }
}

void MainWindow::processTreeCommand(const ResourceTreeRequest &request)
{
    if (m_treeBusy || !m_project.isOpen() || ResourceReferences::tag(request.type).isEmpty()) return;
    QScopedValueRollback<bool> busy(m_treeBusy, true);
    const auto command = request.command;
    auto nodes = m_project.resources(request.type);
    ResourceNode source;
    const bool hasSource = treeNode(nodes, request.source, source);
    auto sourceParent = request.source; if (!sourceParent.isEmpty()) sourceParent.removeLast();
    if (command != ResourceTreeCommand::CreateGroup && command != ResourceTreeCommand::AddExisting && command != ResourceTreeCommand::Sort && !hasSource) return;
    QString name;
    if (command == ResourceTreeCommand::CreateGroup || command == ResourceTreeCommand::RenameGroup) {
        if (command == ResourceTreeCommand::RenameGroup && !source.isGroup) return;
        auto *children = treeChildren(nodes, command == ResourceTreeCommand::CreateGroup ? request.destination : sourceParent);
        if (!children) return;
        EditorInputDialog dialog(this); dialog.setWindowTitle(command == ResourceTreeCommand::CreateGroup ? tr("Create Group") : tr("Rename Group"));
        dialog.setLabelText(tr("Name:")); dialog.setTextValue(command == ResourceTreeCommand::CreateGroup ? uniqueGroupName(*children, tr("New Group")) : source.name);
        if (dialog.exec() != QDialog::Accepted) return;
        name = dialog.textValue().trimmed();
        if (name.isEmpty() || (command == ResourceTreeCommand::RenameGroup && name == source.name)) return;
    }
    QStringList imported;
    if (command == ResourceTreeCommand::AddExisting) {
        const QString suffix = ResourceReferences::suffix(request.type);
        const QString filter = request.type == ResourceType::Extension ? tr("Extension packages (*.gmez *.extension.gmx)")
            : suffix.isEmpty() ? tr("All files (*)") : tr("Resource files (*%1)").arg(suffix);
        imported = QFileDialog::getOpenFileNames(this, tr("Add Existing Resource"), QFileInfo(m_project.filePath()).absolutePath(), filter);
        if (imported.isEmpty()) return;
    }
    if (command == ResourceTreeCommand::DeleteGroup) {
        if (!source.isGroup) return;
        if (EditorMessageBox::question(this, tr("Delete Group"), tr("Delete group %1 and all resources inside it?\n\nReferences will be cleared and matching room instances or tiles removed.").arg(source.name), EditorMessageBox::Yes | EditorMessageBox::No, EditorMessageBox::No) != EditorMessageBox::Yes) return;
    }
    if (command == ResourceTreeCommand::Move && (request.source.isEmpty() || treePrefix(request.source, request.destination))) return;
    if (!saveResources()) return;

    if (command == ResourceTreeCommand::References) {
        QString error; const auto paths = m_project.resourceReferences(request.type, source.filePath, error);
        if (!error.isEmpty()) { EditorMessageBox::warning(this, tr("Check References"), error); return; }
        EditorDialog dialog(this); dialog.setWindowTitle(tr("References to %1").arg(source.name)); dialog.resize(480, 340);
        auto *layout = new QVBoxLayout(dialog.bodyWidget()); layout->addWidget(new QLabel(tr("Structured resource references (code is not searched):")));
        auto *list = new QListWidget; layout->addWidget(list, 1);
        for (const auto &path : paths) { auto *item = new QListWidgetItem(QFileInfo(path).fileName(), list); item->setToolTip(path); item->setData(Qt::UserRole, path); }
        if (paths.isEmpty()) layout->addWidget(new QLabel(tr("No references found.")));
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Close); layout->addWidget(buttons);
        buttons->button(QDialogButtonBox::Open)->setEnabled(!paths.isEmpty());
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept); if (list->count()) list->setCurrentRow(0);
        if (dialog.exec() == QDialog::Accepted && list->currentItem()) {
            const QString path = list->currentItem()->data(Qt::UserRole).toString();
            for (auto type : {ResourceType::Object, ResourceType::Timeline, ResourceType::Room, ResourceType::Path, ResourceType::Extension})
                for (const auto &resource : ActionXml::resourceList(m_project, type)) if (resource.filePath == path) { openResource(type, path); return; }
        }
        return;
    }

    struct OpenEditor { ResourceType type; QString path; QRect geometry; bool maximized; };
    QList<OpenEditor> opened;
    const auto windows = m_resourceEditors.values();
    for (const auto &window : windows) if (window) {
        OpenEditor item; item.type = static_cast<ResourceType>(window->property("resourceType").toInt()); item.path = window->filePath(); item.geometry = window->normalGeometry(); item.maximized = window->isMaximized(); opened.append(item);
    }
    if (!closeResourceEditors()) return;
    for (const auto &window : windows) delete window.data();
    QString error, selectedFile; QList<int> selection = request.destination;
    bool success = true;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (command == ResourceTreeCommand::CreateGroup) {
        auto *children = treeChildren(nodes, request.destination);
        if (children) { ResourceNode group; group.type = request.type; group.name = name; group.isGroup = true; children->append(group); selection.append(children->size() - 1); success = m_project.saveResourceTree(request.type, nodes, error); }
        else success = false;
    } else if (command == ResourceTreeCommand::RenameGroup) {
        auto *children = treeChildren(nodes, sourceParent);
        if (children) { (*children)[request.source.last()].name = name; selection = request.source; success = m_project.saveResourceTree(request.type, nodes, error); }
        else success = false;
    } else if (command == ResourceTreeCommand::Sort) {
        auto *children = treeChildren(nodes, request.destination);
        std::function<void(QList<ResourceNode> &)> sort = [&](QList<ResourceNode> &items) {
            for (auto &node : items) if (node.isGroup) sort(node.children);
            std::stable_sort(items.begin(), items.end(), [](const ResourceNode &a, const ResourceNode &b) {
                if (a.isGroup != b.isGroup) return a.isGroup;
                return QString::localeAwareCompare(a.name.toCaseFolded(), b.name.toCaseFolded()) < 0;
            });
        };
        if (children) { sort(*children); success = m_project.saveResourceTree(request.type, nodes, error); } else success = false;
    } else if (command == ResourceTreeCommand::Move) {
        auto destination = request.destination;
        auto *from = treeChildren(nodes, sourceParent);
        const int oldRow = request.source.last();
        if (!from || oldRow < 0 || oldRow >= from->size()) success = false;
        else {
            const ResourceNode moved = from->takeAt(oldRow);
            if (destination.size() > sourceParent.size() && treePrefix(sourceParent, destination) && destination.at(sourceParent.size()) > oldRow) --destination[sourceParent.size()];
            auto *to = treeChildren(nodes, destination);
            if (!to) success = false;
            else {
                int row = request.position < 0 ? to->size() : request.position;
                if (request.position >= 0 && request.destination == sourceParent && oldRow < row) --row;
                row = qBound(0, row, to->size()); to->insert(row, moved); selection = destination; selection.append(row);
                success = m_project.saveResourceTree(request.type, nodes, error);
            }
        }
    } else if (command == ResourceTreeCommand::DeleteGroup) {
        std::function<bool(const ResourceNode &)> remove = [&](const ResourceNode &node) {
            if (!node.isGroup) return m_project.removeResource(node.type, node.filePath, error);
            for (const auto &child : node.children) if (!remove(child)) return false;
            return true;
        };
        success = remove(source);
        if (success) { nodes = m_project.resources(request.type); auto *children = treeChildren(nodes, sourceParent); if (children) { children->removeAt(request.source.last()); success = m_project.saveResourceTree(request.type, nodes, error); selection = sourceParent; } }
    } else if (command == ResourceTreeCommand::Duplicate || command == ResourceTreeCommand::AddExisting) {
        std::function<bool(const ResourceNode &, const QList<int> &, QList<int> &)> duplicate;
        duplicate = [&](const ResourceNode &node, const QList<int> &destination, QList<int> &createdPath) {
            if (!node.isGroup) {
                ResourceNode copied;
                if (!m_project.copyResource(node.type, node.filePath, destination, copied, error)) return false;
                auto current = m_project.resources(request.type); auto *siblings = treeChildren(current, destination);
                createdPath = destination; if (siblings) createdPath.append(siblings->size() - 1);
                selectedFile = copied.filePath; return true;
            }
            auto current = m_project.resources(request.type); auto *siblings = treeChildren(current, destination);
            if (!siblings) return false;
            ResourceNode group; group.type = node.type; group.isGroup = true; group.name = uniqueGroupName(*siblings, node.name + (destination == request.destination ? QStringLiteral("_copy") : QString()));
            createdPath = destination; createdPath.append(siblings->size()); siblings->append(group);
            if (!m_project.saveResourceTree(request.type, current, error)) return false;
            for (const auto &child : node.children) { QList<int> childPath; if (!duplicate(child, createdPath, childPath)) return false; }
            selectedFile.clear(); return true;
        };
        if (command == ResourceTreeCommand::Duplicate) success = duplicate(source, request.destination, selection);
        else for (const auto &path : imported) {
            ResourceNode node; node.type = request.type; node.filePath = path;
            if (!duplicate(node, request.destination, selection)) { success = false; break; }
        }
        if (success && command == ResourceTreeCommand::Duplicate && request.position >= 0) {
            auto current = m_project.resources(request.type); auto *siblings = treeChildren(current, request.destination);
            if (siblings && !siblings->isEmpty()) {
                const auto copied = siblings->takeLast(); const int row = qBound(0, request.position, siblings->size()); siblings->insert(row, copied);
                selection = request.destination; selection.append(row); success = m_project.saveResourceTree(request.type, current, error);
            }
        }
    }
    QApplication::restoreOverrideCursor();
    // A multi-resource operation keeps already completed copies/deletions if a
    // later resource fails. Always rebuild from the actual saved project state.
    m_settingsEdited = true; m_resourceBrowser->setProject(m_project); updateConfiguration(m_configurationCombo->currentIndex());
    if (command != ResourceTreeCommand::DeleteGroup) m_resourceBrowser->selectTreeNode(request.type, selection);
    for (const auto &item : opened) {
        bool exists = ResourceReferences::tag(item.type).isEmpty();
        for (const auto &node : ActionXml::resourceList(m_project, item.type)) if (node.filePath == item.path) exists = true;
        if (!exists) continue;
        openResource(item.type, item.path);
        for (const auto &editor : m_resourceEditors) if (editor && editor->filePath() == item.path && editor->property("resourceType").toInt() == static_cast<int>(item.type)) {
            if (item.geometry.isValid()) editor->setGeometry(item.geometry); if (item.maximized) editor->showMaximized(); break;
        }
    }
    if (success && !selectedFile.isEmpty() && request.type != ResourceType::IncludedFile) openResource(request.type, selectedFile);
    if (!success && error.isEmpty()) error = tr("The selected tree item or destination no longer exists.");
    if (!success) error += tr("\nAny resources completed before this error remain saved.");
    if (!error.isEmpty()) EditorMessageBox::warning(this, tr("Resource Tree"), error);
}
