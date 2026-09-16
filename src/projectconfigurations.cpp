#include "project.h"
#include "projectloader.h"
#include "actionxml.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegExp>
#include <QSaveFile>
#include <QSet>
#include <algorithm>

static bool readConfigurationBytes(const QString &path, QByteArray &bytes, QString &error)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { error = QObject::tr("Cannot read %1:\n%2").arg(path, file.errorString()); return false; }
    bytes = file.readAll(); if (file.error() != QFile::NoError) { error = file.errorString(); return false; } return true;
}
static bool writeConfigurationBytes(const QString &path, const QByteArray &bytes, QString &error)
{
    QSaveFile file(path);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = QObject::tr("Cannot write %1:\n%2").arg(path, file.errorString()); return false;
    }
    return true;
}
static void remapConfigurationList(QDomElement parent, const QString &prefix, bool numbered, const QList<ConfigurationEdit> &edits)
{
    if (parent.isNull()) return;
    QList<QDomElement> old;
    for (auto child = parent.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) old.append(child);
    if (old.isEmpty()) return;
    QList<QDomElement> result;
    for (int i = 0; i < edits.size(); ++i) {
        const int source = edits.at(i).sourceIndex;
        auto entry = numbered ? parent.firstChildElement(prefix + QString::number(source)) : (source < old.size() ? old.at(source) : QDomElement());
        if (entry.isNull()) entry = old.first();
        auto copy = entry.cloneNode(true).toElement(); if (numbered) copy.setTagName(prefix + QString::number(i)); result.append(copy);
    }
    for (auto entry : old) parent.removeChild(entry);
    for (auto entry : result) parent.appendChild(entry);
}
static void remapNamedConfigurations(QDomElement node, const QList<ProjectConfiguration> &before, const QList<ConfigurationEdit> &edits)
{
    if (node.tagName() == QStringLiteral("ConfigOptions")) {
        const auto old = ActionXml::elements(node, QStringLiteral("Config"));
        for (const auto &edit : edits) {
            QDomElement source;
            for (auto entry : old) if (entry.attribute(QStringLiteral("name")) == before.at(edit.sourceIndex).name) source = entry;
            if (source.isNull()) continue;
            auto copy = source.cloneNode(true).toElement(); copy.setAttribute(QStringLiteral("name"), edit.name); node.appendChild(copy);
        }
        for (auto entry : old) node.removeChild(entry);
        return;
    }
    for (auto child = node.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) remapNamedConfigurations(child, before, edits);
}
static void relocateConfigurationAssets(QDomNode node, const QString &oldPrefix, const QString &newPrefix)
{
    if (node.isText() || node.isCDATASection()) {
        QString value = node.nodeValue(); QString normalized = value; normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (normalized.startsWith(oldPrefix, Qt::CaseInsensitive)) node.setNodeValue(QString(newPrefix + normalized.mid(oldPrefix.size())).replace(QLatin1Char('/'), QLatin1Char('\\')));
    }
    for (auto child = node.firstChild(); !child.isNull(); child = child.nextSibling()) relocateConfigurationAssets(child, oldPrefix, newPrefix);
}
static void collectConfigurationAssetReferences(QDomNode node, const QDir &root, QSet<QString> &references)
{
    if (node.isText() || node.isCDATASection()) {
        QString value = node.nodeValue(); value.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (value.startsWith(QStringLiteral("Configs/"), Qt::CaseInsensitive)) references.insert(QDir::cleanPath(root.absoluteFilePath(value)).toLower());
    }
    for (auto child = node.firstChild(); !child.isNull(); child = child.nextSibling()) collectConfigurationAssetReferences(child, root, references);
}
bool Project::saveConfigurations(const QList<ConfigurationEdit> &edits, QString &error)
{
    if (m_configurations.isEmpty() || edits.isEmpty() || edits.first().sourceIndex != 0 || edits.first().added || edits.first().name != m_configurations.first().name) {
        error = QObject::tr("The default configuration must remain first and cannot be renamed or deleted."); return false;
    }
    QSet<QString> names; QSet<int> originals;
    for (const auto &edit : edits) {
        if (edit.sourceIndex < 0 || edit.sourceIndex >= m_configurations.size() || !QRegExp(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*")).exactMatch(edit.name)
            || QRegExp(QStringLiteral("(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])"), Qt::CaseInsensitive).exactMatch(edit.name)
            || names.contains(edit.name.toLower()) || (!edit.added && originals.contains(edit.sourceIndex))) {
            error = QObject::tr("Use unique configuration names containing letters, digits and underscores. Windows reserved filenames are not allowed."); return false;
        }
        names.insert(edit.name.toLower()); if (!edit.added) originals.insert(edit.sourceIndex);
    }
    const QDir root = QFileInfo(m_filePath).absoluteDir();
    auto inside = [&root](const QString &path) {
        const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        if (!absolute.startsWith(root.absolutePath() + QLatin1Char('/'), Qt::CaseInsensitive)) return false;
        QString ancestor = absolute;
        while (ancestor != root.absolutePath()) { if (QFileInfo(ancestor).isSymLink()) return false; const QString next = QFileInfo(ancestor).absolutePath(); if (next == ancestor) return false; ancestor = next; }
        return true;
    };
    QDomDocument manifest; QByteArray manifestBytes;
    if (!readConfigurationBytes(m_filePath, manifestBytes, error) || !manifest.setContent(manifestBytes, false, &error)) return false;
    if (manifestBytes != m_sourceBytes) { error = QObject::tr("The project file changed outside the editor. Reopen it before managing configurations."); return false; }
    QMap<QString, QByteArray> beforeFiles, writes; QSet<QString> remove, owned, referencedAssets;
    QStringList assetDirectories;
    for (const auto &configuration : m_configurations) {
        if (!inside(configuration.filePath)) { error = QObject::tr("Configuration files must be inside the project directory."); return false; }
        QByteArray bytes; if (!readConfigurationBytes(configuration.filePath, bytes, error)) return false;
        beforeFiles[configuration.filePath] = bytes; owned.insert(configuration.filePath.toLower()); remove.insert(configuration.filePath);
        QString directory = configuration.filePath; directory.chop(11); assetDirectories.append(directory);
        if (!QFileInfo::exists(directory)) continue;
        if (!inside(directory) || !QFileInfo(directory).isDir()) { error = QObject::tr("Invalid configuration asset directory: %1").arg(directory); return false; }
        QDirIterator iterator(directory, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString filePath = iterator.next();
            if (!inside(filePath)) { error = QObject::tr("Linked configuration assets cannot be moved: %1").arg(filePath); return false; }
            if (!iterator.fileInfo().isFile()) continue;
            if (!readConfigurationBytes(filePath, bytes, error)) return false;
            beforeFiles[filePath] = bytes; owned.insert(filePath.toLower()); remove.insert(filePath);
        }
    }
    auto addWrite = [&](const QString &path, const QByteArray &bytes) {
        if (!inside(path) || (QFileInfo::exists(path) && !owned.contains(path.toLower()) && !beforeFiles.contains(path))) {
            error = QObject::tr("A file already exists at the configuration destination: %1").arg(path); return false;
        }
        writes[path] = bytes; return true;
    };
    auto configs = ActionXml::child(manifest.documentElement(), QStringLiteral("Configs"));
    for (auto config : ActionXml::elements(configs, QStringLiteral("Config"))) configs.removeChild(config);
    for (const auto &edit : edits) {
        const auto &source = m_configurations.at(edit.sourceIndex);
        const QString destination = !edit.added && edit.name == source.name ? source.filePath : root.filePath(QStringLiteral("Configs/%1.config.gmx").arg(edit.name));
        QString directory = destination; directory.chop(11);
        QDomDocument config; if (!config.setContent(beforeFiles.value(source.filePath), false, &error)) return false;
        if (config.documentElement().tagName() != QStringLiteral("Config")) { error = QObject::tr("Invalid configuration file: %1").arg(source.filePath); return false; }
        const QString sourceDirectory = assetDirectories.at(edit.sourceIndex);
        relocateConfigurationAssets(config, root.relativeFilePath(sourceDirectory) + QLatin1Char('/'), root.relativeFilePath(directory) + QLatin1Char('/'));
        collectConfigurationAssetReferences(config, root, referencedAssets);
        if (!addWrite(destination, config.toByteArray(2))) return false;
        for (auto it = beforeFiles.cbegin(); it != beforeFiles.cend(); ++it) {
            if (it.key().startsWith(sourceDirectory + QLatin1Char('/'), Qt::CaseInsensitive))
                if (!addWrite(QDir(directory).filePath(QDir(sourceDirectory).relativeFilePath(it.key())), it.value())) return false;
        }
        auto entry = manifest.createElement(QStringLiteral("Config")); QString reference = root.relativeFilePath(destination); reference.chop(11);
        entry.appendChild(manifest.createTextNode(reference.replace(QLatin1Char('/'), QLatin1Char('\\')))); configs.appendChild(entry);
    }
    for (auto type : {ResourceType::Sprite, ResourceType::Background, ResourceType::Font, ResourceType::Sound, ResourceType::Extension}) {
        for (const auto &resource : ActionXml::resourceList(*this, type)) {
            if (!inside(resource.filePath)) { error = QObject::tr("Cannot update configuration references outside the project: %1").arg(resource.filePath); return false; }
            QByteArray bytes; QDomDocument xml;
            if (!readConfigurationBytes(resource.filePath, bytes, error) || !xml.setContent(bytes, false, &error)) return false;
            const QByteArray originalXml = xml.toByteArray(2); auto element = xml.documentElement();
            if (type == ResourceType::Sprite || type == ResourceType::Background) remapConfigurationList(element.firstChildElement(QStringLiteral("TextureGroups")), QStringLiteral("TextureGroup"), true, edits);
            if (type == ResourceType::Font) remapConfigurationList(element.firstChildElement(QStringLiteral("texgroups")), QStringLiteral("texgroup"), true, edits);
            if (type == ResourceType::Sound) for (const auto &container : QStringLiteral("volume|bitRates|sampleRates|types|bitDepths").split(QLatin1Char('|'))) remapConfigurationList(element.firstChildElement(container), QString(), false, edits);
            remapNamedConfigurations(element, m_configurations, edits);
            if (xml.toByteArray(2) != originalXml) { beforeFiles[resource.filePath] = bytes; writes[resource.filePath] = xml.toByteArray(2); }
        }
    }
    remapNamedConfigurations(manifest.documentElement(), m_configurations, edits);
    beforeFiles[m_filePath] = manifestBytes; writes[m_filePath] = manifest.toByteArray(2);
    // Do not delete a path now owned by another renamed configuration.
    for (const auto &path : writes.keys()) for (auto it = remove.begin(); it != remove.end();) {
        if (it->compare(path, Qt::CaseInsensitive) == 0) it = remove.erase(it); else ++it;
    }
    // A surviving configuration may deliberately share another configuration's asset.
    for (auto it = remove.begin(); it != remove.end();) {
        if (referencedAssets.contains(it->toLower()) && !it->endsWith(QStringLiteral(".config.gmx"), Qt::CaseInsensitive)) it = remove.erase(it); else ++it;
    }
    // Windows treats case-only renames as the same file. Keep the preimage under
    // the destination spelling too, so rollback restores it instead of deleting it.
    const auto originalPaths = beforeFiles.keys();
    for (const auto &path : writes.keys()) if (!beforeFiles.contains(path)) {
        for (const auto &original : originalPaths) if (original.compare(path, Qt::CaseInsensitive) == 0) { beforeFiles[path] = beforeFiles.value(original); break; }
    }
    QStringList changed;
    auto rollback = [&] {
        for (const auto &path : changed) {
            QString restoreError;
            if (beforeFiles.contains(path)) { if (!writeConfigurationBytes(path, beforeFiles.value(path), restoreError)) error += QLatin1Char('\n') + restoreError; }
            else if (QFileInfo::exists(path) && !QFile::remove(path)) error += QObject::tr("\nCannot remove incomplete configuration file: %1").arg(path);
        }
    };
    for (auto it = writes.cbegin(); it != writes.cend(); ++it) {
        if (beforeFiles.value(it.key()) == it.value() && beforeFiles.contains(it.key())) continue;
        if (!writeConfigurationBytes(it.key(), it.value(), error)) { rollback(); return false; } changed.append(it.key());
    }
    for (const auto &path : remove) {
        if (!QFile::remove(path)) { error = QObject::tr("Cannot delete configuration file: %1").arg(path); rollback(); return false; } changed.append(path);
    }
    Project updated; ProjectLoader loader;
    if (!loader.load(m_filePath, updated, error)) { rollback(); return false; }
    updated.m_temporaryDirectory = m_temporaryDirectory; updated.m_imported = m_imported; *this = updated;
    // Only remove empty directories after the file transaction has succeeded.
    for (const auto &directory : assetDirectories) {
        if (!inside(directory)) continue;
        QStringList emptyDirs; QDirIterator iterator(directory, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (iterator.hasNext()) emptyDirs.append(iterator.next());
        std::sort(emptyDirs.begin(), emptyDirs.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
        for (const auto &path : emptyDirs) if (inside(path)) QDir().rmdir(path);
        QDir().rmdir(directory);
    }
    return true;
}
