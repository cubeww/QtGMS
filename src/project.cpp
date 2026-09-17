#include "project.h"
#include "spritedocument.h"
#include "backgrounddocument.h"
#include "sounddocument.h"
#include "textfiledocument.h"
#include "shaderdocument.h"
#include "fontdocument.h"
#include "objectdocument.h"
#include "timelinedocument.h"
#include "pathdocument.h"
#include "roomdocument.h"
#include "extensiondocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

static void collectResourceNames(const QList<ResourceNode> &nodes, QSet<QString> &names)
{
    for (const ResourceNode &node : nodes) {
        if (node.isGroup) collectResourceNames(node.children, names);
        else names.insert(node.name.toLower());
    }
}

bool Project::createResource(ResourceType type, const QList<int> &groupPath, ResourceNode &resource, QString &error)
{
    error.clear();
    if (!isOpen()) { error = QObject::tr("Open a project before creating a resource."); return false; }
    QString itemTag;
    switch (type) {
    case ResourceType::Sprite: itemTag = QStringLiteral("sprite"); break;
    case ResourceType::Background: itemTag = QStringLiteral("background"); break;
    case ResourceType::Sound: itemTag = QStringLiteral("sound"); break;
    case ResourceType::Script: itemTag = QStringLiteral("script"); break;
    case ResourceType::Shader: itemTag = QStringLiteral("shader"); break;
    case ResourceType::Font: itemTag = QStringLiteral("font"); break;
    case ResourceType::Object: itemTag = QStringLiteral("object"); break;
    case ResourceType::Path: itemTag = QStringLiteral("path"); break;
    case ResourceType::Timeline: itemTag = QStringLiteral("timeline"); break;
    case ResourceType::Extension: itemTag = QStringLiteral("extension"); break;
    case ResourceType::Room: itemTag = QStringLiteral("room"); break;
    default: error = QObject::tr("This resource type cannot be created yet."); return false;
    }
    const QString groupTag = type == ResourceType::Extension ? QStringLiteral("NewExtensions") : itemTag + QLatin1Char('s');
    const QString resourceDirectory = type == ResourceType::Extension ? QStringLiteral("extensions") : type == ResourceType::Sound || type == ResourceType::Background ? itemTag : groupTag;
    const QString suffix = type == ResourceType::Script ? QStringLiteral(".gml") : type == ResourceType::Shader ? QStringLiteral(".shader")
        : QLatin1Char('.') + itemTag + QStringLiteral(".gmx");
    QFile source(m_filePath);
    if (!source.open(QIODevice::ReadOnly)) { error = source.errorString(); return false; }
    if (source.readAll() != m_sourceBytes) {
        error = QObject::tr("The project file changed outside the editor. Reopen it before creating resources.");
        return false;
    }
    source.close();
    QDomDocument document;
    if (!document.setContent(m_sourceBytes, false, &error)) return false;
    QDomElement root = document.documentElement();
    QDomElement group = root.firstChildElement(groupTag);
    if (group.isNull()) {
        group = document.createElement(groupTag);
        group.setAttribute(QStringLiteral("name"), resourceDirectory);
        root.appendChild(group);
    }
    // Resolve the same ordered group path in the model and GMX document.
    QList<ResourceNode> nodes = resources(type);
    QList<ResourceNode> *children = &nodes;
    for (int index : groupPath) {
        if (index < 0 || index >= children->size() || !children->at(index).isGroup) {
            error = QObject::tr("The selected resource group no longer exists."); return false;
        }
        int position = 0;
        QDomElement nested;
        for (QDomElement element = group.firstChildElement(); !element.isNull(); element = element.nextSiblingElement()) {
            if (element.tagName() != groupTag && element.tagName() != itemTag) continue;
            if (position++ == index) { nested = element; break; }
        }
        if (nested.tagName() != groupTag) {
            error = QObject::tr("The selected resource group does not match the project file."); return false;
        }
        group = nested;
        children = &(*children)[index].children;
    }
    const QDir directory = QFileInfo(m_filePath).absoluteDir();
    QSet<QString> names;
    for (auto it = m_resources.constBegin(); it != m_resources.constEnd(); ++it)
        collectResourceNames(it.value(), names);
    ResourceNode created;
    created.type = type;
    int number = 0;
    do {
        created.name = itemTag + QString::number(number++);
        created.filePath = directory.absoluteFilePath(resourceDirectory + QLatin1Char('/') + created.name + suffix);
    } while (names.contains(created.name.toLower()) || QFileInfo::exists(created.filePath)
             || (type == ResourceType::Font && QFileInfo::exists(directory.absoluteFilePath(QStringLiteral("fonts/%1.png").arg(created.name)))));
    if (!directory.mkpath(resourceDirectory)) {
        error = QObject::tr("Cannot create the resource directory."); return false;
    }
    QDomElement entry = document.createElement(itemTag);
    if (type == ResourceType::Shader) {
        created.value = QStringLiteral("GLSLES"); entry.setAttribute(QStringLiteral("type"), created.value);
    }
    entry.appendChild(document.createTextNode(resourceDirectory + QLatin1Char('\\') + created.name
        + (type == ResourceType::Script || type == ResourceType::Shader ? suffix : QString())));
    group.appendChild(entry);
    const bool createdFile = type == ResourceType::Sprite ? SpriteDocument::createEmpty(created.filePath, error)
        : type == ResourceType::Script ? TextFileDocument::createEmpty(created.filePath, error)
        : type == ResourceType::Shader ? ShaderDocument::createEmpty(created.filePath, error)
        : type == ResourceType::Font ? FontDocument::createEmpty(created.filePath, error)
        : type == ResourceType::Object ? ObjectDocument::createEmpty(created.filePath, error)
        : type == ResourceType::Path ? PathDocument::createEmpty(created.filePath, error)
        : type == ResourceType::Timeline ? TimelineDocument::createEmpty(created.filePath, error)
        : type == ResourceType::Extension ? ExtensionDocument::createEmpty(created.filePath, error)
        : type == ResourceType::Room ? RoomDocument::createEmpty(created.filePath, error)
        : type == ResourceType::Sound ? SoundDocument::createEmpty(created.filePath, error) : BackgroundDocument::createEmpty(created.filePath, error);
    if (!createdFile) return false;
    const QByteArray bytes = document.toByteArray(2);
    QSaveFile output(m_filePath);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        error = QObject::tr("Cannot update the project:\n%1").arg(output.errorString());
        if (!QFile::remove(created.filePath))
            error += QObject::tr("\nCannot remove the unregistered resource file: %1").arg(created.filePath);
        if (type == ResourceType::Font) {
            const QString imagePath = directory.absoluteFilePath(QStringLiteral("fonts/%1.png").arg(created.name));
            if (!QFile::remove(imagePath)) error += QObject::tr("\nCannot remove the unregistered font texture: %1").arg(imagePath);
        }
        return false;
    }
    children->append(created);
    m_resources[type] = nodes;
    m_sourceBytes = bytes;
    ++m_resourceCount;
    resource = created;
    return true;
}

static QString updateResourceNode(QList<ResourceNode> &nodes, const QString &path, const QString &thumbnail)
{
    for (ResourceNode &node : nodes) {
        if (node.isGroup) {
            const QString name = updateResourceNode(node.children, path, thumbnail);
            if (!name.isEmpty()) return name;
        } else if (node.filePath == path) { node.thumbnailPath = thumbnail; return node.name; }
    }
    return QString();
}
static void updateObjectNodes(QList<ResourceNode> &nodes, const QString &spriteName, const QString &thumbnail)
{
    for (ResourceNode &node : nodes) {
        if (node.isGroup) updateObjectNodes(node.children, spriteName, thumbnail);
        else if (node.spriteName == spriteName) node.thumbnailPath = thumbnail;
    }
}
void Project::updateThumbnail(ResourceType type, const QString &filePath, const QString &thumbnailPath)
{
    const QString name = updateResourceNode(m_resources[type], filePath, thumbnailPath);
    if (type == ResourceType::Sprite && !name.isEmpty()) updateObjectNodes(m_resources[ResourceType::Object], name, thumbnailPath);
}

static void updateObjectMetadataNode(QList<ResourceNode> &nodes, const QString &path, const QString &sprite,
                                     const QString &parent, const QString &thumbnail)
{
    for (ResourceNode &node : nodes) {
        if (node.isGroup) updateObjectMetadataNode(node.children, path, sprite, parent, thumbnail);
        else if (node.filePath == path) { node.spriteName = sprite; node.parentName = parent; node.thumbnailPath = thumbnail; }
    }
}
void Project::updateObjectMetadata(const QString &filePath, const QString &spriteName, const QString &parentName, const QString &thumbnailPath)
{ updateObjectMetadataNode(m_resources[ResourceType::Object], filePath, spriteName, parentName, thumbnailPath); }

const QList<ResourceNode> &Project::resources(ResourceType type) const
{
    static const QList<ResourceNode> EmptyResources;
    const auto entry = m_resources.constFind(type);
    return entry == m_resources.constEnd() ? EmptyResources : entry.value();
}

static const ResourceNode *findShaderNode(const QList<ResourceNode> &nodes, const QString &path)
{
    for (const ResourceNode &node : nodes) {
        if (node.isGroup) {
            if (const ResourceNode *found = findShaderNode(node.children, path)) return found;
        } else if (node.filePath == path) return &node;
    }
    return nullptr;
}
QString Project::shaderType(const QString &filePath) const
{
    const ResourceNode *node = findShaderNode(resources(ResourceType::Shader), filePath);
    return node && !node->value.isEmpty() ? node->value : QStringLiteral("GLSLES");
}
static QDomElement findShaderElement(QDomElement group, const QDir &directory, const QString &path)
{
    for (QDomElement element = group.firstChildElement(); !element.isNull(); element = element.nextSiblingElement()) {
        if (element.tagName() == QStringLiteral("shaders")) {
            QDomElement found = findShaderElement(element, directory, path); if (!found.isNull()) return found;
        } else if (element.tagName() == QStringLiteral("shader")) {
            QString reference = element.text().trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
            if (!reference.endsWith(QStringLiteral(".shader"), Qt::CaseInsensitive)) reference += QStringLiteral(".shader");
            if (QDir::cleanPath(directory.absoluteFilePath(reference)) == path) return element;
        }
    }
    return QDomElement();
}
static void updateShaderNode(QList<ResourceNode> &nodes, const QString &path, const QString &type)
{
    for (ResourceNode &node : nodes) {
        if (node.isGroup) updateShaderNode(node.children, path, type);
        else if (node.filePath == path) { node.value = type; return; }
    }
}
bool Project::setShaderType(const QString &filePath, const QString &type, QString &error)
{
    error.clear();
    if (type != QStringLiteral("GLSLES") && type != QStringLiteral("GLSL")
        && type != QStringLiteral("HLSL9") && type != QStringLiteral("HLSL11")) {
        error = QObject::tr("Unsupported shader language: %1").arg(type); return false;
    }
    if (!findShaderNode(resources(ResourceType::Shader), filePath)) {
        error = QObject::tr("The shader is no longer in this project."); return false;
    }
    if (shaderType(filePath) == type) return true;
    QFile source(m_filePath);
    if (!source.open(QIODevice::ReadOnly)) { error = source.errorString(); return false; }
    if (source.readAll() != m_sourceBytes || source.error() != QFile::NoError) {
        error = QObject::tr("The project changed outside the editor. Reopen it before changing the shader language."); return false;
    }
    source.close();
    QDomDocument document; if (!document.setContent(m_sourceBytes, false, &error)) return false;
    QDomElement shader = findShaderElement(document.documentElement().firstChildElement(QStringLiteral("shaders")),
                                          QFileInfo(m_filePath).absoluteDir(), filePath);
    if (shader.isNull()) { error = QObject::tr("The shader entry was not found in the project file."); return false; }
    shader.setAttribute(QStringLiteral("type"), type);
    const QByteArray bytes = document.toByteArray(2); QSaveFile output(m_filePath);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        error = output.errorString(); return false;
    }
    m_sourceBytes = bytes; updateShaderNode(m_resources[ResourceType::Shader], filePath, type); return true;
}
