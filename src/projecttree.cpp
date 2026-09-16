#include "project.h"
#include "projectloader.h"
#include "projectfiletransaction.h"
#include "actionxml.h"
#include "resourcereferences.h"
#include <QFile>
#include <QFileInfo>
#include <QRegExp>
#include <QSet>
#include <functional>

static QString treePath(const QDir &base, QString reference)
{ return QDir::cleanPath(base.absoluteFilePath(reference.replace(QLatin1Char('\\'), QLatin1Char('/')))); }

static bool readTreeXml(const QString &path, QDomDocument &xml, QByteArray &bytes, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = QObject::tr("Cannot read %1: %2").arg(path, file.errorString()); return false; }
    bytes = file.readAll();
    if (file.error() != QFile::NoError || !xml.setContent(bytes, false, &error)) { error = path + QLatin1Char('\n') + error; return false; }
    return true;
}

static QString treeCategory(ResourceType type)
{
    const QStringList categories = QStringLiteral("Sprites|Sounds|Backgrounds|Paths|Scripts|Shaders|Fonts|Time Lines|Objects|Rooms|Included Files|Extensions").split(QLatin1Char('|'));
    return categories.value(static_cast<int>(type));
}

static void collectTreePaths(const QList<ResourceNode> &nodes, const QString &parent, QMap<QString, QString> &paths, QStringList &ordered)
{
    for (const auto &node : nodes) {
        const QString path = parent + QLatin1Char('/') + node.name;
        if (node.isGroup) collectTreePaths(node.children, path, paths, ordered);
        else { paths[node.filePath] = path; ordered.append(node.filePath); }
    }
}

static void relocateTreeFileReferences(QDomDocument &xml, ResourceType type, const QString &path, const QDir &directory, const QMap<QString, QString> &moves)
{
    const QDir owner = QFileInfo(path).absoluteDir();
    auto root = xml.documentElement();
    auto update = [&](QDomElement field, const QDir &base) {
        if (field.isNull() || field.text().isEmpty()) return;
        const QString resolved = treePath(base, field.text());
        for (auto it = moves.cbegin(); it != moves.cend(); ++it) if (resolved.compare(it.key(), Qt::CaseInsensitive) == 0) {
            while (!field.firstChild().isNull()) field.removeChild(field.firstChild());
            field.appendChild(xml.createTextNode(QString(base.relativeFilePath(it.value())).replace(QLatin1Char('/'), QLatin1Char('\\')))); break;
        }
    };
    if (type == ResourceType::Sprite)
        for (auto frame : ActionXml::elements(root.firstChildElement(QStringLiteral("frames")), QStringLiteral("frame"))) update(frame, owner);
    if (type == ResourceType::Background) update(root.firstChildElement(QStringLiteral("data")), owner);
    if (type == ResourceType::Font) update(root.firstChildElement(QStringLiteral("image")), owner);
    if (type == ResourceType::Sound) {
        update(root.firstChildElement(QStringLiteral("data")), QDir(owner.filePath(QStringLiteral("audio"))));
        update(root.firstChildElement(QStringLiteral("origname")), owner);
    }
    if (type == ResourceType::GameSettings) {
        const auto options = root.firstChildElement(QStringLiteral("Options"));
        for (auto option = options.firstChildElement(); !option.isNull(); option = option.nextSiblingElement()) {
            const QString key = option.tagName();
            if (key.endsWith(QStringLiteral("_icon")) || key.endsWith(QStringLiteral("_splash_screen")) || key.endsWith(QStringLiteral("_runner_header")) || key.endsWith(QStringLiteral("_runner_finished")) || key.endsWith(QStringLiteral("_license")) || key.endsWith(QStringLiteral("_nsis_file"))) update(option, directory);
        }
    }
}

bool Project::saveResourceTree(ResourceType type, const QList<ResourceNode> &nodes, QString &error)
{ return writeResourceTree(type, nodes, QMap<QString, QDomElement>(), error); }

bool Project::writeResourceTree(ResourceType type, const QList<ResourceNode> &nodes, const QMap<QString, QDomElement> &newEntries, QString &error)
{
    error.clear();
    const QString itemTag = ResourceReferences::tag(type);
    if (!isOpen() || itemTag.isEmpty()) { error = QObject::tr("This category cannot be reorganized."); return false; }
    const QString groupTag = type == ResourceType::Extension ? QStringLiteral("NewExtensions") : itemTag + QLatin1Char('s');
    const QDir directory = QFileInfo(m_filePath).absoluteDir();
    QByteArray before; QDomDocument manifest;
    if (!readTreeXml(m_filePath, manifest, before, error)) return false;
    if (before != m_sourceBytes) { error = QObject::tr("The project changed outside the editor. Reopen it before editing the tree."); return false; }
    auto root = manifest.documentElement(); auto group = root.firstChildElement(groupTag);
    if (group.isNull()) {
        group = manifest.createElement(groupTag); group.setAttribute(QStringLiteral("name"), type == ResourceType::Extension ? QStringLiteral("extensions") : groupTag); root.appendChild(group);
    }
    if (!group.nextSiblingElement(groupTag).isNull()) { error = QObject::tr("Multiple root groups for this category cannot be reorganized."); return false; }
    QMap<QString, QDomElement> entries;
    std::function<void(QDomElement, const QString &)> index = [&](QDomElement parent, const QString &dataDirectory) {
        for (auto child = parent.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
            if (child.tagName() == groupTag) index(child, dataDirectory + QLatin1Char('/') + child.attribute(QStringLiteral("name")));
            else if (child.tagName() == itemTag) {
                QString reference = type == ResourceType::IncludedFile ? dataDirectory + QLatin1Char('/') + ActionXml::text(child, QStringLiteral("name")) : child.text().trimmed();
                const QString suffix = ResourceReferences::suffix(type);
                if (!suffix.isEmpty() && !reference.endsWith(suffix, Qt::CaseInsensitive)) reference += suffix;
                entries[treePath(directory, reference)] = child;
            }
        }
    };
    index(group, group.attribute(QStringLiteral("name")));
    QMap<QString, QString> oldTreePaths, newTreePaths, moves;
    QStringList oldOrder, newOrder;
    collectTreePaths(resources(type), treeCategory(type), oldTreePaths, oldOrder);
    collectTreePaths(nodes, treeCategory(type), newTreePaths, newOrder);
    QSet<QString> uniquePaths, uniqueDestinations;
    // Tree edits never silently delete resources; deletion goes through its reference cleanup.
    for (const auto &path : oldOrder) if (!newTreePaths.contains(path)) { error = QObject::tr("Remove resources with Delete before removing their tree entries."); return false; }
    std::function<bool(QDomElement, const QList<ResourceNode> &, const QString &, int)> append;
    append = [&](QDomElement parent, const QList<ResourceNode> &children, const QString &dataDirectory, int depth) {
        if (depth > 128) { error = QObject::tr("Resource folders are nested too deeply."); return false; }
        QSet<QString> groupNames;
        for (const auto &node : children) {
            if (node.type != type) { error = QObject::tr("Resources can only move within their own category."); return false; }
            if (node.isGroup) {
                const QString name = node.name.trimmed();
                if (name.isEmpty() || name.size() > 128 || name == QStringLiteral(".") || name == QStringLiteral("..") || name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')) || name.contains(QRegExp(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]"))) || groupNames.contains(name.toLower()) || QRegExp(QStringLiteral("(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])"), Qt::CaseInsensitive).exactMatch(name.section(QLatin1Char('.'), 0, 0))) {
                    error = QObject::tr("Folder names must be valid and unique within their parent."); return false;
                }
                groupNames.insert(name.toLower());
                auto nested = manifest.createElement(groupTag); nested.setAttribute(QStringLiteral("name"), name);
                parent.appendChild(nested);
                if (!append(nested, node.children, dataDirectory + QLatin1Char('/') + name, depth + 1)) return false;
            } else {
                if (uniquePaths.contains(node.filePath.toLower())) { error = QObject::tr("A resource appears more than once in the tree."); return false; }
                uniquePaths.insert(node.filePath.toLower());
                auto entry = newEntries.value(node.filePath, entries.value(node.filePath));
                if (entry.isNull()) { error = QObject::tr("The resource no longer exists: %1").arg(node.filePath); return false; }
                entry = manifest.importNode(entry, true).toElement();
                if (type == ResourceType::IncludedFile) {
                    const QString destination = treePath(directory, dataDirectory + QLatin1Char('/') + node.name);
                    if (uniqueDestinations.contains(destination.toLower())) { error = QObject::tr("Two included files have the same destination."); return false; }
                    uniqueDestinations.insert(destination.toLower());
                    if (destination != node.filePath) moves[node.filePath] = destination;
                    ActionXml::setText(entry, QStringLiteral("name"), node.name);
                }
                parent.appendChild(entry);
            }
        }
        return true;
    };
    auto replacement = group.cloneNode(false).toElement();
    // Preserve unknown root metadata; only resource/group entries are replaced.
    for (auto child = group.firstChild(); !child.isNull(); child = child.nextSibling())
        if (child.isElement() && child.toElement().tagName() != itemTag && child.toElement().tagName() != groupTag) replacement.appendChild(child.cloneNode(true));
    if (!append(replacement, nodes, group.attribute(QStringLiteral("name")), 0)) return false;
    std::function<int(QDomElement)> updateCounts = [&](QDomElement parent) {
        int count = 0;
        for (auto child = parent.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
            if (child.tagName() == groupTag) count += updateCounts(child);
            else if (child.tagName() == itemTag) ++count;
        }
        if (type == ResourceType::IncludedFile || parent.hasAttribute(QStringLiteral("number")))
            parent.setAttribute(QStringLiteral("number"), count);
        return count;
    };
    updateCounts(replacement);
    if (type == ResourceType::Extension) {
        const auto entries = replacement.elementsByTagName(itemTag);
        for (int i = 0; i < entries.size(); ++i) entries.at(i).toElement().setAttribute(QStringLiteral("index"), i);
    }
    root.replaceChild(replacement, group);
    QMap<QString, QByteArray> writes, originals;
    writes[m_filePath] = manifest.toByteArray(2); originals[m_filePath] = before;
    for (auto ownerType : {ResourceType::Extension, ResourceType::Path, ResourceType::Sprite, ResourceType::Background, ResourceType::Font, ResourceType::Sound, ResourceType::GameSettings}) {
        if (ownerType != ResourceType::Extension && !(ownerType == ResourceType::Path && type == ResourceType::Room) && moves.isEmpty()) continue;
        QList<ResourceNode> owners = ActionXml::resourceList(*this, ownerType);
        if (ownerType == ResourceType::GameSettings) for (const auto &config : m_configurations) { ResourceNode owner; owner.type = ownerType; owner.filePath = config.filePath; owners.append(owner); }
        for (const auto &owner : owners) {
            if (!QFileInfo::exists(owner.filePath)) continue;
            QDomDocument xml; QByteArray bytes;
            if (!readTreeXml(owner.filePath, xml, bytes, error)) return false;
            const QByteArray canonical = xml.toByteArray(2);
            if (ownerType == ResourceType::Path && type == ResourceType::Room) {
                bool valid; const int old = ActionXml::text(xml.documentElement(), QStringLiteral("backroom")).toInt(&valid);
                if (valid && old >= 0 && old < oldOrder.size()) ActionXml::setText(xml.documentElement(), QStringLiteral("backroom"), QString::number(newOrder.indexOf(oldOrder.at(old))));
            }
            if (ownerType == ResourceType::Extension) {
                for (auto reference : ActionXml::elements(xml.documentElement().firstChildElement(QStringLiteral("IncludedResources")), QStringLiteral("Resource"))) {
                    QString value = reference.text(); value.replace(QLatin1Char('\\'), QLatin1Char('/'));
                    for (auto it = oldTreePaths.cbegin(); it != oldTreePaths.cend(); ++it) if (it.value() == value && newTreePaths.value(it.key()) != value) {
                        while (!reference.firstChild().isNull()) reference.removeChild(reference.firstChild());
                        reference.appendChild(xml.createTextNode(QString(newTreePaths.value(it.key())).replace(QLatin1Char('/'), QLatin1Char('\\')))); break;
                    }
                }
            }
            if (!moves.isEmpty()) relocateTreeFileReferences(xml, ownerType, owner.filePath, directory, moves);
            if (canonical != xml.toByteArray(2)) { writes[owner.filePath] = xml.toByteArray(2); originals[owner.filePath] = bytes; }
        }
    }
    ProjectFileTransaction transaction(directory.absolutePath());
    for (auto it = writes.cbegin(); it != writes.cend(); ++it) {
        QFile current(it.key());
        if (!current.open(QIODevice::ReadOnly) || current.readAll() != originals.value(it.key())) { error = QObject::tr("A file changed outside the editor: %1").arg(it.key()); transaction.rollback(error); return false; }
        current.close();
        if (!transaction.write(it.key(), it.value(), error)) { transaction.rollback(error); return false; }
    }
    for (auto it = moves.cbegin(); it != moves.cend(); ++it) {
        if (!QFileInfo::exists(it.key())) continue;
        if (!transaction.move(it.key(), it.value(), error)) { transaction.rollback(error); return false; }
    }
    Project updated; ProjectLoader loader;
    if (!loader.load(m_filePath, updated, error)) { transaction.rollback(error); return false; }
    updated.m_temporaryDirectory = m_temporaryDirectory; *this = updated;
    transaction.finish(error); return true;
}

QStringList Project::resourceReferences(ResourceType type, const QString &path, QString &error) const
{
    error.clear(); QString name; int roomIndex = -1;
    const auto candidates = ActionXml::resourceList(*this, type);
    for (int i = 0; i < candidates.size(); ++i) if (candidates.at(i).filePath == path) { name = candidates.at(i).name; if (type == ResourceType::Room) roomIndex = i; }
    QStringList result;
    if (name.isEmpty()) return result;
    for (auto ownerType : {ResourceType::Object, ResourceType::Timeline, ResourceType::Room, ResourceType::Path, ResourceType::Extension}) {
        for (const auto &owner : ActionXml::resourceList(*this, ownerType)) {
            if (owner.isMissing) continue;
            QDomDocument xml; QByteArray bytes;
            if (!readTreeXml(owner.filePath, xml, bytes, error)) return result;
            const QByteArray before = xml.toByteArray(2);
            ResourceReferences::rename(xml, ownerType, type, name, QStringLiteral("__qtgms_reference_probe__"));
            bool validRoom = false; const int backgroundRoom = ActionXml::text(xml.documentElement(), QStringLiteral("backroom")).toInt(&validRoom);
            const bool indexedRoom = ownerType == ResourceType::Path && roomIndex >= 0 && validRoom && backgroundRoom == roomIndex;
            if (xml.toByteArray(2) != before || indexedRoom) result.append(owner.filePath);
        }
    }
    return result;
}
