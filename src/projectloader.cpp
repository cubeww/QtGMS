#include "projectloader.h"

#include <QFile>
#include <QFileInfo>
#include <QObject>

struct ResourceFormat {
    ResourceType type;
    const char *groupTag;
    const char *itemTag;
    const char *suffix;
};

static const ResourceFormat ResourceFormats[] = {
    {ResourceType::Sprite, "sprites", "sprite", ".sprite.gmx"},
    {ResourceType::Sound, "sounds", "sound", ".sound.gmx"},
    {ResourceType::Background, "backgrounds", "background", ".background.gmx"},
    {ResourceType::Path, "paths", "path", ".path.gmx"},
    {ResourceType::Script, "scripts", "script", ".gml"},
    {ResourceType::Shader, "shaders", "shader", ".shader"},
    {ResourceType::Font, "fonts", "font", ".font.gmx"},
    {ResourceType::Timeline, "timelines", "timeline", ".timeline.gmx"},
    {ResourceType::Object, "objects", "object", ".object.gmx"},
    {ResourceType::Room, "rooms", "room", ".room.gmx"},
    {ResourceType::IncludedFile, "datafiles", "datafile", ""},
    {ResourceType::Extension, "NewExtensions", "extension", ".extension.gmx"},
    {ResourceType::Macro, "constants", "constant", ""}
};

static QString normalizedPath(QString path)
{
    return path.replace(QLatin1Char('\\'), QLatin1Char('/'));
}

static QString xmlError(const QString &filePath, const QXmlStreamReader &xml)
{
    return QObject::tr("%1\nLine %2, column %3: %4")
        .arg(QDir::toNativeSeparators(filePath)).arg(xml.lineNumber())
        .arg(xml.columnNumber()).arg(xml.errorString());
}

QString ProjectLoader::resourcePath(const QString &reference, const QString &suffix) const
{
    QString path = normalizedPath(reference.trimmed());
    if (!suffix.isEmpty() && !path.endsWith(suffix, Qt::CaseInsensitive))
        path += suffix;
    return QDir::cleanPath(m_directory.absoluteFilePath(path));
}

void ProjectLoader::checkResource(ResourceNode &resource)
{
    ++m_project.m_resourceCount;
    resource.isMissing = !QFileInfo(resource.filePath).isFile();
    if (resource.isMissing)
        m_project.m_warnings.append(QObject::tr("Missing resource: %1")
            .arg(QDir::toNativeSeparators(resource.filePath)));
}

QList<ResourceNode> ProjectLoader::readResources(ResourceType type, const QString &groupTag,
                                                const QString &itemTag, const QString &suffix,
                                                const QString &dataDirectory, int depth)
{
    QList<ResourceNode> resources;
    if (depth > 128) {
        m_xml.raiseError(QObject::tr("Resource groups are nested too deeply."));
        return resources;
    }
    while (m_xml.readNextStartElement()) {
        ResourceNode resource;
        resource.type = type;
        if (m_xml.name() == groupTag) {
            resource.isGroup = true;
            resource.name = m_xml.attributes().value(QStringLiteral("name")).toString();
            if (resource.name.isEmpty()) {
                m_xml.raiseError(QObject::tr("A resource group has no name."));
                break;
            }
            const QString directory = type == ResourceType::IncludedFile
                ? dataDirectory + QLatin1Char('/') + normalizedPath(resource.name) : QString();
            resource.children = readResources(type, groupTag, itemTag, suffix, directory, depth + 1);
        } else if (m_xml.name() == itemTag) {
            if (type == ResourceType::Macro) {
                resource.name = m_xml.attributes().value(QStringLiteral("name")).toString();
                resource.value = m_xml.readElementText();
                ++m_project.m_resourceCount;
            } else if (type == ResourceType::IncludedFile) {
                while (m_xml.readNextStartElement()) {
                    if (m_xml.name() == QStringLiteral("name"))
                        resource.name = m_xml.readElementText();
                    else
                        m_xml.skipCurrentElement();
                }
                // GMX datafile locations follow the nested datafiles/name groups.
                resource.filePath = resourcePath(dataDirectory + QLatin1Char('/')
                                                   + resource.name, QString());
                checkResource(resource);
            } else {
                resource.value = m_xml.attributes().value(QStringLiteral("type")).toString();
                const QString reference = normalizedPath(m_xml.readElementText().trimmed());
                resource.name = QFileInfo(reference).fileName();
                if (resource.name.endsWith(suffix, Qt::CaseInsensitive))
                    resource.name.chop(suffix.size());
                resource.filePath = resourcePath(reference, suffix);
                checkResource(resource);
            }
            if (resource.name.isEmpty()) {
                m_xml.raiseError(QObject::tr("A resource has no name or file path."));
                break;
            }
        } else {
            m_xml.skipCurrentElement();
            continue;
        }
        resources.append(resource);
    }
    return resources;
}

void ProjectLoader::readConfiguration(ProjectConfiguration &configuration)
{
    QFile file(configuration.filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_project.m_warnings.append(QObject::tr("Cannot read configuration %1: %2")
            .arg(QDir::toNativeSeparators(file.fileName()), file.errorString()));
        return;
    }
    QXmlStreamReader xml(&file);
    if (!xml.readNextStartElement() || xml.name() != QStringLiteral("Config"))
        xml.raiseError(QObject::tr("Expected a Config root element."));
    while (!xml.hasError() && xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("Options")) {
            while (xml.readNextStartElement()) {
                const QString key = xml.name().toString();
                configuration.options.insert(key, xml.readElementText());
            }
        } else if (xml.name() == QStringLiteral("ConfigConstants")) {
            while (xml.readNextStartElement()) {
                if (xml.name() != QStringLiteral("constants")) {
                    xml.skipCurrentElement();
                    continue;
                }
                while (xml.readNextStartElement()) {
                    ResourceNode macro;
                    macro.type = ResourceType::Macro;
                    macro.name = xml.attributes().value(QStringLiteral("name")).toString();
                    macro.value = xml.readElementText();
                    if (macro.name.isEmpty())
                        xml.raiseError(QObject::tr("A configuration macro has no name."));
                    else
                        configuration.macros.append(macro);
                }
            }
        } else {
            xml.skipCurrentElement();
        }
    }
    while (!xml.atEnd())
        xml.readNext();
    if (xml.hasError()) {
        configuration.options.clear();
        configuration.macros.clear();
        m_project.m_warnings.append(xmlError(configuration.filePath, xml));
    }
}

void ProjectLoader::readConfigurations()
{
    while (m_xml.readNextStartElement()) {
        if (m_xml.name() != QStringLiteral("Config")) {
            m_xml.skipCurrentElement();
            continue;
        }
        ProjectConfiguration configuration;
        const QString reference = m_xml.readElementText().trimmed();
        configuration.filePath = resourcePath(reference, QStringLiteral(".config.gmx"));
        configuration.name = QFileInfo(normalizedPath(reference)).fileName();
        if (configuration.name.endsWith(QStringLiteral(".config.gmx"), Qt::CaseInsensitive))
            configuration.name.chop(11);
        if (configuration.name.isEmpty()) {
            m_xml.raiseError(QObject::tr("A configuration has no name."));
            return;
        }
        for (const ProjectConfiguration &existing : m_project.m_configurations) {
            if (existing.name == configuration.name) {
                m_xml.raiseError(QObject::tr("Duplicate configuration: %1").arg(configuration.name));
                return;
            }
        }
        readConfiguration(configuration);
        m_project.m_resourceCount += configuration.macros.size();
        m_project.m_configurations.append(configuration);
    }
}

void ProjectLoader::readThumbnailMetadata(QList<ResourceNode> &resources)
{
    for (ResourceNode &resource : resources) {
        if (resource.isGroup)
            readThumbnailMetadata(resource.children);
        else if (!resource.isMissing)
            readThumbnailMetadata(resource);
    }
}

void ProjectLoader::readThumbnailMetadata(ResourceNode &resource)
{
    QFile file(resource.filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_project.m_warnings.append(QObject::tr("Cannot read thumbnail metadata %1: %2")
            .arg(QDir::toNativeSeparators(resource.filePath), file.errorString()));
        return;
    }
    const QString root = resource.type == ResourceType::Sprite ? QStringLiteral("sprite")
        : resource.type == ResourceType::Background ? QStringLiteral("background") : QStringLiteral("object");
    QXmlStreamReader xml(&file);
    QString imagePath;
    QString spriteName;
    if (!xml.readNextStartElement() || xml.name() != root)
        xml.raiseError(QObject::tr("Expected a %1 root element.").arg(root));
    while (!xml.hasError() && xml.readNextStartElement()) {
        if (resource.type == ResourceType::Sprite && xml.name() == QStringLiteral("frames")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QStringLiteral("frame")
                    && xml.attributes().value(QStringLiteral("index")) == QStringLiteral("0"))
                    imagePath = xml.readElementText().trimmed();
                else
                    xml.skipCurrentElement();
            }
        } else if (resource.type == ResourceType::Background && xml.name() == QStringLiteral("data")) {
            imagePath = xml.readElementText().trimmed();
        } else if (resource.type == ResourceType::Object && xml.name() == QStringLiteral("spriteName")) {
            spriteName = xml.readElementText().trimmed();
        } else {
            xml.skipCurrentElement();
        }
    }
    while (!xml.atEnd())
        xml.readNext();
    if (xml.hasError()) {
        m_project.m_warnings.append(xmlError(resource.filePath, xml));
        return;
    }
    resource.spriteName = spriteName;
    if (!imagePath.isEmpty()) {
        resource.thumbnailPath = QDir::cleanPath(QFileInfo(resource.filePath).absoluteDir()
            .absoluteFilePath(normalizedPath(imagePath)));
        if (!QFileInfo(resource.thumbnailPath).isFile())
            m_project.m_warnings.append(QObject::tr("Missing thumbnail image: %1")
                .arg(QDir::toNativeSeparators(resource.thumbnailPath)));
    }
}

static void collectSpriteThumbnails(const QList<ResourceNode> &resources,
                                    QMap<QString, QString> &thumbnails)
{
    for (const ResourceNode &resource : resources) {
        if (resource.isGroup)
            collectSpriteThumbnails(resource.children, thumbnails);
        else
            thumbnails.insert(resource.name, resource.thumbnailPath);
    }
}

void ProjectLoader::resolveObjectThumbnails(QList<ResourceNode> &resources,
                                            const QMap<QString, QString> &spriteThumbnails)
{
    for (ResourceNode &resource : resources) {
        if (resource.isGroup) {
            resolveObjectThumbnails(resource.children, spriteThumbnails);
        } else if (!resource.spriteName.isEmpty() && resource.spriteName != QStringLiteral("<undefined>")) {
            const auto sprite = spriteThumbnails.constFind(resource.spriteName);
            if (sprite != spriteThumbnails.constEnd())
                resource.thumbnailPath = sprite.value();
            else
                m_project.m_warnings.append(QObject::tr("Object %1 references an unknown sprite: %2")
                    .arg(resource.name, resource.spriteName));
        }
    }
}

bool ProjectLoader::load(const QString &filePath, Project &project, QString &error)
{
    error.clear();
    m_project = Project();
    m_xml.clear();
    m_project.m_filePath = QFileInfo(filePath).absoluteFilePath();
    m_directory = QFileInfo(m_project.m_filePath).absoluteDir();
    QFile file(m_project.m_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QObject::tr("Cannot open %1:\n%2")
            .arg(QDir::toNativeSeparators(file.fileName()), file.errorString());
        return false;
    }
    m_project.m_sourceBytes = file.readAll();
    m_xml.addData(m_project.m_sourceBytes);
    if (!m_xml.readNextStartElement() || m_xml.name() != QStringLiteral("assets"))
        m_xml.raiseError(QObject::tr("This is not a GameMaker Studio GMX project (expected assets)."));
    while (!m_xml.hasError() && m_xml.readNextStartElement()) {
        if (m_xml.name() == QStringLiteral("Configs")) {
            readConfigurations();
        } else if (m_xml.name() == QStringLiteral("audiogroups")) {
            QStringList groups;
            while (m_xml.readNextStartElement()) {
                if (m_xml.name() == QStringLiteral("audiogroup"))
                    groups.append(m_xml.attributes().value(QStringLiteral("name")).toString());
                m_xml.skipCurrentElement();
            }
            if (!groups.isEmpty()) m_project.m_audioGroups = groups;
        } else if (m_xml.name() == QStringLiteral("help")) {
            while (m_xml.readNextStartElement()) {
                if (m_xml.name() == QStringLiteral("rtf"))
                    m_project.m_informationFilePath = resourcePath(m_xml.readElementText(), QString());
                else
                    m_xml.skipCurrentElement();
            }
        } else {
            bool recognized = false;
            for (const ResourceFormat &format : ResourceFormats) {
                if (m_xml.name() != QLatin1String(format.groupTag))
                    continue;
                const QString directory = format.type == ResourceType::IncludedFile
                    ? m_xml.attributes().value(QStringLiteral("name")).toString() : QString();
                if (format.type == ResourceType::IncludedFile && directory.isEmpty()) {
                    m_xml.raiseError(QObject::tr("The included files group has no directory name."));
                    recognized = true;
                    break;
                }
                m_project.m_resources[format.type].append(readResources(format.type,
                    QString::fromLatin1(format.groupTag), QString::fromLatin1(format.itemTag),
                    QString::fromLatin1(format.suffix), directory));
                recognized = true;
                break;
            }
            if (!recognized)
                m_xml.skipCurrentElement();
        }
    }
    // Consume the document end as well, to reject truncated or trailing XML.
    while (!m_xml.atEnd())
        m_xml.readNext();
    if (m_xml.hasError()) {
        error = xmlError(m_project.m_filePath, m_xml);
        return false;
    }
    // Resolve object references only after every sprite group has been read.
    for (ResourceType type : {ResourceType::Sprite, ResourceType::Background, ResourceType::Object})
        readThumbnailMetadata(m_project.m_resources[type]);
    QMap<QString, QString> spriteThumbnails;
    collectSpriteThumbnails(m_project.resources(ResourceType::Sprite), spriteThumbnails);
    resolveObjectThumbnails(m_project.m_resources[ResourceType::Object], spriteThumbnails);
    project = m_project;
    return true;
}
