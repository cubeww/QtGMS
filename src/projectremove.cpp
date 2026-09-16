#include "project.h"
#include "projectloader.h"
#include "resourcereferences.h"
#include "actionxml.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <algorithm>

static QString removalPath(const QDir &directory, QString reference)
{
    return QDir::cleanPath(directory.absoluteFilePath(reference.replace(QLatin1Char('\\'), QLatin1Char('/'))));
}

static QString removalPathKey(const QString &path)
{
    return QDir::cleanPath(path).toLower();
}

static bool readRemovalFile(const QString &path, QByteArray &bytes, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = QObject::tr("Cannot read %1:\n%2").arg(path, file.errorString()); return false; }
    bytes = file.readAll();
    if (file.error() == QFile::NoError) return true;
    error = QObject::tr("Cannot read %1:\n%2").arg(path, file.errorString()); return false;
}

static bool writeRemovalFile(const QString &path, const QByteArray &bytes, QString &error)
{
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) return true;
    error = QObject::tr("Cannot write %1:\n%2").arg(path, file.errorString()); return false;
}

static bool removeManifestResource(QDomElement parent, const QDir &directory, QString dataDirectory,
                                   ResourceType type, const QString &path)
{
    if (parent.tagName() == QStringLiteral("datafiles"))
        dataDirectory = dataDirectory.isEmpty() ? parent.attribute(QStringLiteral("name")) : dataDirectory + QLatin1Char('/') + parent.attribute(QStringLiteral("name"));
    for (auto child = parent.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
        if (child.tagName() == ResourceReferences::tag(type)) {
            QString reference;
            if (type == ResourceType::IncludedFile)
                reference = dataDirectory + QLatin1Char('/') + ActionXml::text(child, QStringLiteral("name"));
            else {
                reference = child.text().trimmed();
                const QString suffix = ResourceReferences::suffix(type);
                if (!reference.endsWith(suffix, Qt::CaseInsensitive)) reference += suffix;
            }
            if (removalPathKey(removalPath(directory, reference)) == removalPathKey(path)) {
                parent.removeChild(child); return true;
            }
        }
        if (removeManifestResource(child, directory, dataDirectory, type, path)) return true;
    }
    return false;
}

static bool isFileOption(const QString &key)
{
    return key.endsWith(QStringLiteral("_icon")) || key.endsWith(QStringLiteral("_splash_screen"))
        || key.endsWith(QStringLiteral("_runner_header")) || key.endsWith(QStringLiteral("_runner_finished"))
        || key.endsWith(QStringLiteral("_license")) || key.endsWith(QStringLiteral("_nsis_file"));
}

static QStringList removalAssets(const ResourceNode &resource, const QDomDocument &xml)
{
    QStringList paths;
    const QDir directory = QFileInfo(resource.filePath).absoluteDir();
    const auto root = xml.documentElement();
    auto append = [&](const QDir &base, const QString &reference) {
        if (!reference.isEmpty()) paths.append(removalPath(base, reference));
    };
    if (resource.type == ResourceType::Sprite)
        for (auto frame : ActionXml::elements(root.firstChildElement(QStringLiteral("frames")), QStringLiteral("frame"))) append(directory, frame.text());
    if (resource.type == ResourceType::Background) append(directory, ActionXml::text(root, QStringLiteral("data")));
    if (resource.type == ResourceType::Sound) append(QDir(directory.filePath(QStringLiteral("audio"))), ActionXml::text(root, QStringLiteral("data")));
    if (resource.type == ResourceType::Font) {
        append(directory, resource.name + QStringLiteral(".png"));
        append(directory, ActionXml::text(root, QStringLiteral("image")));
    }
    if (resource.type == ResourceType::Extension) {
        QDirIterator files(directory.filePath(resource.name), QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (files.hasNext()) paths.append(files.next());
    }
    return paths;
}

static void clearRemovedAssetReferences(QDomDocument &xml, ResourceType type, const QString &path,
                                         const QDir &projectDirectory, const QSet<QString> &removed)
{
    const QDir directory = QFileInfo(path).absoluteDir();
    auto root = xml.documentElement();
    auto clear = [&](QDomElement parent, const QString &tag, const QDir &base) {
        const QString value = ActionXml::text(parent, tag);
        if (!value.isEmpty() && removed.contains(removalPathKey(removalPath(base, value)))) ActionXml::setText(parent, tag, QString());
    };
    if (type == ResourceType::Sprite) {
        bool changed = false;
        const auto frames = root.firstChildElement(QStringLiteral("frames"));
        for (auto frame : ActionXml::elements(frames, QStringLiteral("frame")))
            if (!frame.text().isEmpty() && removed.contains(removalPathKey(removalPath(directory, frame.text())))) {
                frame.parentNode().removeChild(frame); changed = true;
            }
        if (changed) {
            QMap<int, QDomElement> ordered;
            for (auto frame : ActionXml::elements(frames, QStringLiteral("frame"))) ordered[frame.attribute(QStringLiteral("index")).toInt()] = frame;
            int index = 0;
            for (auto it = ordered.begin(); it != ordered.end(); ++it) it.value().setAttribute(QStringLiteral("index"), index++);
        }
    }
    if (type == ResourceType::Background) clear(root, QStringLiteral("data"), directory);
    if (type == ResourceType::Font) clear(root, QStringLiteral("image"), directory);
    if (type == ResourceType::Sound) {
        clear(root, QStringLiteral("data"), QDir(directory.filePath(QStringLiteral("audio"))));
        clear(root, QStringLiteral("origname"), directory);
    }
    if (type == ResourceType::GameSettings) {
        auto options = root.firstChildElement(QStringLiteral("Options"));
        for (auto option = options.firstChildElement(); !option.isNull(); option = option.nextSiblingElement())
            if (isFileOption(option.tagName())) clear(options, option.tagName(), projectDirectory);
    }
}

bool Project::removeResource(ResourceType type, const QString &path, QString &error)
{
    error.clear();
    ResourceNode resource;
    bool found = false;
    for (const auto &node : ActionXml::resourceList(*this, type))
        if (removalPathKey(node.filePath) == removalPathKey(path)) { resource = node; found = true; break; }
    if (!found || ResourceReferences::tag(type).isEmpty()) { error = QObject::tr("This resource does not belong to the project."); return false; }
    const QDir directory = QFileInfo(m_filePath).absoluteDir();
    auto inside = [&](const QString &candidate) {
        QString current = QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
        if (!current.startsWith(directory.absolutePath() + QLatin1Char('/'), Qt::CaseInsensitive)) return false;
        while (removalPathKey(current) != removalPathKey(directory.absolutePath())) {
            if (QFileInfo(current).isSymLink()) return false;
            const QString parent = QFileInfo(current).absolutePath();
            if (parent == current) return false;
            current = parent;
        }
        return true;
    };
    if (!inside(path)) { error = QObject::tr("Cannot delete a linked resource or a resource outside the project: %1").arg(path); return false; }
    QByteArray manifestBytes;
    QDomDocument manifest;
    if (!readRemovalFile(m_filePath, manifestBytes, error) || !manifest.setContent(manifestBytes, false, &error)) return false;
    if (manifestBytes != m_sourceBytes) { error = QObject::tr("The project changed outside the editor. Reopen it before deleting resources."); return false; }
    if (!removeManifestResource(manifest.documentElement(), directory, QString(), type, path)) { error = QObject::tr("The resource entry could not be located in the project file."); return false; }
    if (type == ResourceType::IncludedFile) {
        const auto groups = manifest.elementsByTagName(QStringLiteral("datafiles"));
        for (int i = 0; i < groups.size(); ++i) {
            auto group = groups.at(i).toElement();
            group.setAttribute(QStringLiteral("number"), group.elementsByTagName(QStringLiteral("datafile")).size());
        }
    }

    QMap<QString, QDomDocument> documents;
    QMap<QString, ResourceType> documentTypes;
    QMap<QString, QByteArray> originals, writes;
    QList<ResourceNode> allResources;
    for (auto it = m_resources.cbegin(); it != m_resources.cend(); ++it) allResources.append(ActionXml::resourceList(*this, it.key()));
    const auto xmlTypes = QList<ResourceType>{ResourceType::Sprite, ResourceType::Sound, ResourceType::Background, ResourceType::Path, ResourceType::Font, ResourceType::Timeline, ResourceType::Object, ResourceType::Room, ResourceType::Extension};
    for (const auto &node : allResources) {
        if (!xmlTypes.contains(node.type) || !QFileInfo::exists(node.filePath)) continue;
        QByteArray bytes; QDomDocument xml;
        if (!readRemovalFile(node.filePath, bytes, error) || !xml.setContent(bytes, false, &error)) return false;
        originals[node.filePath] = bytes; documents[node.filePath] = xml; documentTypes[node.filePath] = node.type;
    }
    for (const auto &configuration : m_configurations) {
        QByteArray bytes; QDomDocument xml;
        if (!readRemovalFile(configuration.filePath, bytes, error) || !xml.setContent(bytes, false, &error)) return false;
        originals[configuration.filePath] = bytes; documents[configuration.filePath] = xml; documentTypes[configuration.filePath] = ResourceType::GameSettings;
    }

    QSet<QString> retained;
    for (const auto &node : allResources) {
        if (removalPathKey(node.filePath) == removalPathKey(path)) continue;
        retained.insert(removalPathKey(node.filePath));
        for (const auto &asset : removalAssets(node, documents.value(node.filePath))) retained.insert(removalPathKey(asset));
    }
    for (auto it = documents.cbegin(); it != documents.cend(); ++it) if (documentTypes.value(it.key()) == ResourceType::GameSettings) {
        const auto options = it.value().documentElement().firstChildElement(QStringLiteral("Options"));
        for (auto option = options.firstChildElement(); !option.isNull(); option = option.nextSiblingElement())
            if (isFileOption(option.tagName()) && !option.text().isEmpty()) retained.insert(removalPathKey(removalPath(directory, option.text())));
    }
    QMap<QString, QString> files;
    QSet<QString> removed;
    QStringList candidates = removalAssets(resource, documents.value(path));
    candidates.prepend(path);
    for (const auto &candidate : candidates) {
        const QString key = removalPathKey(candidate);
        if (key != removalPathKey(path) && retained.contains(key)) continue;
        removed.insert(key);
        if (!QFileInfo::exists(candidate)) continue;
        if (!inside(candidate) || !QFileInfo(candidate).isFile()) { error = QObject::tr("Cannot delete a linked file or a file outside the project: %1").arg(candidate); return false; }
        files[key] = candidate;
    }
    int roomIndex = -1;
    const auto rooms = ActionXml::resourceList(*this, ResourceType::Room);
    if (type == ResourceType::Room)
        for (int i = 0; i < rooms.size(); ++i) if (removalPathKey(rooms.at(i).filePath) == removalPathKey(path)) roomIndex = i;
    for (auto it = documents.begin(); it != documents.end(); ++it) {
        if (removed.contains(removalPathKey(it.key()))) continue;
        const QByteArray before = it.value().toByteArray(2);
        ResourceReferences::remove(it.value(), documentTypes.value(it.key()), type, resource.name, roomIndex);
        clearRemovedAssetReferences(it.value(), documentTypes.value(it.key()), it.key(), directory, removed);
        if (it.value().toByteArray(2) != before) {
            if (!inside(it.key())) { error = QObject::tr("Cannot update references outside the project: %1").arg(it.key()); return false; }
            writes[it.key()] = it.value().toByteArray(2);
        }
    }
    originals[m_filePath] = manifestBytes; writes[m_filePath] = manifest.toByteArray(2);

    QTemporaryDir staging(directory.filePath(QStringLiteral(".qtgms-delete-XXXXXX")));
    if (!staging.isValid()) { error = QObject::tr("Cannot create temporary storage for resource deletion."); return false; }
    staging.setAutoRemove(false);
    QStringList written;
    QMap<QString, QString> moved, backups;
    // Retain XML preimages on disk as well, so failed recovery never loses them.
    for (auto it = writes.cbegin(); it != writes.cend(); ++it) {
        const QString backup = QDir(staging.path()).filePath(QStringLiteral("original-%1.xml").arg(backups.size()));
        if (!writeRemovalFile(backup, originals.value(it.key()), error)) { staging.remove(); return false; }
        backups[it.key()] = backup;
    }
    auto rollback = [&] {
        bool restored = true;
        for (auto it = moved.cbegin(); it != moved.cend(); ++it) if (!QFile::rename(it.value(), it.key())) {
            error += QObject::tr("\nCannot restore %1; its file is retained at %2.").arg(it.key(), it.value()); restored = false;
        }
        for (const auto &file : written) {
            QString restoreError;
            if (!writeRemovalFile(file, originals.value(file), restoreError)) {
                error += QLatin1Char('\n') + restoreError + QObject::tr("\nOriginal contents retained at %1.").arg(backups.value(file)); restored = false;
            }
        }
        if (restored) staging.remove();
    };
    for (auto it = writes.cbegin(); it != writes.cend(); ++it) {
        QByteArray current;
        if (!readRemovalFile(it.key(), current, error) || current != originals.value(it.key())) {
            if (error.isEmpty()) error = QObject::tr("A resource changed outside the editor: %1").arg(it.key());
            rollback(); return false;
        }
        if (!writeRemovalFile(it.key(), it.value(), error)) { rollback(); return false; }
        written.append(it.key());
    }
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const QString destination = QDir(staging.path()).filePath(QString::number(moved.size()));
        if (!QFile::rename(it.value(), destination)) {
            error = QObject::tr("Cannot remove %1. Close any application using the file.").arg(it.value()); rollback(); return false;
        }
        moved[it.value()] = destination;
    }
    Project updated; ProjectLoader loader;
    if (!loader.load(m_filePath, updated, error)) { rollback(); return false; }
    updated.m_temporaryDirectory = m_temporaryDirectory; updated.m_imported = m_imported; *this = updated;
    if (!staging.remove()) error = QObject::tr("The resource was deleted, but temporary files could not be cleaned up: %1").arg(staging.path());
    // Only remove empty extension directories; shared files always remain in place.
    if (type == ResourceType::Extension) {
        const QString content = QFileInfo(path).absoluteDir().filePath(resource.name);
        QStringList directories;
        QDirIterator children(content, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (children.hasNext()) directories.append(children.next());
        std::sort(directories.begin(), directories.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
        directories.append(content);
        for (const auto &empty : directories) if (inside(empty)) QDir().rmdir(empty);
    }
    return true;
}
