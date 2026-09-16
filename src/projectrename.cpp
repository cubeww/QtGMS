#include "project.h"
#include "projectloader.h"
#include "resourcereferences.h"
#include "actionxml.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegExp>
#include <QSaveFile>
#include <QUuid>

static bool sameResourcePath(const QString &a, const QString &b)
{ return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0; }
static QString referencePath(const QDir &base, QString value)
{ return QDir::cleanPath(base.absoluteFilePath(value.replace(QLatin1Char('\\'), QLatin1Char('/')))); }
static bool readRenameFile(const QString &path, QByteArray &bytes, QString &error)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { error = QObject::tr("Cannot read %1:\n%2").arg(path, file.errorString()); return false; }
    bytes = file.readAll(); if (file.error() != QFile::NoError) { error = file.errorString(); return false; } return true;
}
static bool writeRenameFile(const QString &path, const QByteArray &bytes, QString &error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { error = QObject::tr("Cannot write %1:\n%2").arg(path, file.errorString()); return false; } return true;
}
bool Project::validateResourceName(ResourceType type, const QString &path, const QString &name, QString &error) const
{
    bool found = false;
    for (const auto &resource : ActionXml::resourceList(*this, type)) if (sameResourcePath(resource.filePath, path)) found = true;
    if (!found || ResourceReferences::tag(type).isEmpty()) { error = QObject::tr("This resource does not belong to the project."); return false; }
    const bool valid = type == ResourceType::IncludedFile
        ? !name.isEmpty() && name != QStringLiteral(".") && name != QStringLiteral("..") && !name.endsWith(QLatin1Char('.')) && !name.endsWith(QLatin1Char(' ')) && !name.contains(QRegExp(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")))
        : QRegExp(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*")).exactMatch(name);
    if (!valid || name.size() > 128 || QRegExp(QStringLiteral("(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])"), Qt::CaseInsensitive).exactMatch(name.section(QLatin1Char('.'), 0, 0))) {
        error = QObject::tr("Use a valid, non-reserved name (up to 128 characters). Resource identifiers must start with a letter or underscore and contain only letters, digits and underscores."); return false;
    }
    if (type != ResourceType::IncludedFile && QStringLiteral("if then else begin end while do for repeat until with switch case default break continue exit return var globalvar div mod and or xor not enum true false self other all noone global local pi undefined pointer_null pointer_invalid").split(QLatin1Char(' ')).contains(name)) {
        error = QObject::tr("This name is reserved by GML."); return false;
    }
    for (auto it = m_resources.cbegin(); it != m_resources.cend(); ++it) for (const auto &resource : ActionXml::resourceList(*this, it.key())) {
        if (resource.name.compare(name, Qt::CaseInsensitive) == 0 && !sameResourcePath(resource.filePath, path)) { error = QObject::tr("A resource or macro named %1 already exists.").arg(name); return false; }
    }
    for (const auto &configuration : m_configurations) for (const auto &macro : configuration.macros)
        if (macro.name.compare(name, Qt::CaseInsensitive) == 0) { error = QObject::tr("A configuration macro already uses this name."); return false; }
    return true;
}
static bool renameManifestReference(QDomElement element, const QDir &root, const QString &dataDirectory, ResourceType type,
                                    const QString &oldPath, const QString &newPath, const QString &newName)
{
    QString directory = dataDirectory; bool changed = false;
    if (element.tagName() == QStringLiteral("datafiles")) directory = directory.isEmpty() ? element.attribute(QStringLiteral("name")) : directory + QLatin1Char('/') + element.attribute(QStringLiteral("name"));
    if (element.tagName() == ResourceReferences::tag(type)) {
        if (type == ResourceType::IncludedFile) {
            if (sameResourcePath(referencePath(root, directory + QLatin1Char('/') + ActionXml::text(element, QStringLiteral("name"))), oldPath)) { ActionXml::setText(element, QStringLiteral("name"), newName); return true; }
        } else {
            QString reference = element.text().trimmed(); const QString suffix = ResourceReferences::suffix(type);
            const bool hasSuffix = reference.endsWith(suffix, Qt::CaseInsensitive);
            if (!hasSuffix) reference += suffix;
            if (sameResourcePath(referencePath(root, reference), oldPath)) {
                QString replacement = root.relativeFilePath(newPath); if (!hasSuffix) replacement.chop(suffix.size());
                while (!element.firstChild().isNull()) element.removeChild(element.firstChild());
                element.appendChild(element.ownerDocument().createTextNode(replacement.replace(QLatin1Char('/'), QLatin1Char('\\')))); return true;
            }
        }
    }
    for (auto child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) changed = renameManifestReference(child, root, directory, type, oldPath, newPath, newName) || changed;
    return changed;
}
struct ResourceRenameMove { QString source, destination; bool directory = false; };
static QString renamedAsset(const QMap<QString, QString> &assets, const QString &path)
{ for (auto it = assets.cbegin(); it != assets.cend(); ++it) if (sameResourcePath(it.key(), path)) return it.value(); return QString(); }
static void renameAssetText(QDomElement parent, const QString &tag, const QDir &base, const QMap<QString, QString> &assets)
{
    auto element = parent.firstChildElement(tag); if (element.isNull() || element.text().isEmpty()) return;
    const QString replacement = renamedAsset(assets, referencePath(base, element.text())); if (replacement.isEmpty()) return;
    ActionXml::setText(parent, tag, QString(base.relativeFilePath(replacement)).replace(QLatin1Char('/'), QLatin1Char('\\')));
}
static void renameResourceAssets(QDomDocument &xml, ResourceType type, const QString &path, const QDir &projectDirectory, const QMap<QString, QString> &assets)
{
    const QDir directory = QFileInfo(path).absoluteDir(); auto root = xml.documentElement();
    if (type == ResourceType::Sprite) for (auto frame : ActionXml::elements(root.firstChildElement(QStringLiteral("frames")), QStringLiteral("frame"))) {
        const QString replacement = renamedAsset(assets, referencePath(directory, frame.text())); if (replacement.isEmpty()) continue;
        while (!frame.firstChild().isNull()) frame.removeChild(frame.firstChild());
        frame.appendChild(xml.createTextNode(QString(directory.relativeFilePath(replacement)).replace(QLatin1Char('/'), QLatin1Char('\\'))));
    }
    if (type == ResourceType::Background) renameAssetText(root, QStringLiteral("data"), directory, assets);
    if (type == ResourceType::Font) renameAssetText(root, QStringLiteral("image"), directory, assets);
    if (type == ResourceType::GameSettings) {
        auto options = root.firstChildElement(QStringLiteral("Options"));
        for (auto option = options.firstChildElement(); !option.isNull(); option = option.nextSiblingElement()) {
            const QString key = option.tagName();
            if (key.endsWith(QStringLiteral("_icon")) || key.endsWith(QStringLiteral("_splash_screen")) || key.endsWith(QStringLiteral("_runner_header")) || key.endsWith(QStringLiteral("_runner_finished")) || key.endsWith(QStringLiteral("_license")) || key.endsWith(QStringLiteral("_nsis_file"))) renameAssetText(options, key, projectDirectory, assets);
        }
    }
    if (type == ResourceType::Sound) { renameAssetText(root, QStringLiteral("data"), QDir(directory.filePath(QStringLiteral("audio"))), assets); renameAssetText(root, QStringLiteral("origname"), directory, assets); }
}
bool Project::renameResource(ResourceType type, const QString &path, const QString &name, QString &newPath, QString &error)
{
    if (!validateResourceName(type, path, name, error)) return false;
    ResourceNode resource;
    for (const auto &node : ActionXml::resourceList(*this, type)) if (sameResourcePath(node.filePath, path)) resource = node;
    newPath = QFileInfo(path).absoluteDir().filePath(name + ResourceReferences::suffix(type));
    if (resource.name == name) return true;
    const QDir projectDirectory = QFileInfo(m_filePath).absoluteDir();
    auto inside = [&projectDirectory](const QString &candidate) {
        QString current = QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
        if (!current.startsWith(projectDirectory.absolutePath() + QLatin1Char('/'), Qt::CaseInsensitive)) return false;
        while (!sameResourcePath(current, projectDirectory.absolutePath())) { if (QFileInfo(current).isSymLink()) return false; const QString parent = QFileInfo(current).absolutePath(); if (parent == current) return false; current = parent; } return true;
    };
    QList<ResourceRenameMove> moves; QMap<QString, QString> assets;
    auto addMove = [&](const QString &source, const QString &destination, bool directory) {
        if (!inside(source) || !inside(destination) || !QFileInfo::exists(source)) { error = QObject::tr("Cannot rename a missing resource or a resource outside the project: %1").arg(source); return false; }
        if (QFileInfo::exists(destination) && !sameResourcePath(source, destination)) { error = QObject::tr("The destination already exists: %1").arg(destination); return false; }
        for (const auto &move : moves) { if (sameResourcePath(move.source, source)) return true; if (sameResourcePath(move.destination, destination)) { error = QObject::tr("Two files would have the same destination: %1").arg(destination); return false; } }
        ResourceRenameMove move; move.source = source; move.destination = destination; move.directory = directory; moves.append(move); return true;
    };
    if (!addMove(path, newPath, false)) return false;
    assets[path] = newPath;
    QByteArray manifestBytes; QDomDocument manifest;
    if (!readRenameFile(m_filePath, manifestBytes, error) || !manifest.setContent(manifestBytes, false, &error)) return false;
    if (manifestBytes != m_sourceBytes) { error = QObject::tr("The project changed outside the editor. Reopen it before renaming resources."); return false; }
    if (!renameManifestReference(manifest.documentElement(), projectDirectory, QString(), type, path, newPath, name)) { error = QObject::tr("The resource entry could not be located in the project file."); return false; }
    QMap<QString, QDomDocument> documents; QMap<QString, ResourceType> documentTypes; QMap<QString, QByteArray> originals, writes;
    for (auto owner : {ResourceType::Sprite, ResourceType::Sound, ResourceType::Background, ResourceType::Path, ResourceType::Font, ResourceType::Timeline, ResourceType::Object, ResourceType::Room, ResourceType::Extension}) {
        for (const auto &node : ActionXml::resourceList(*this, owner)) {
            if (node.isMissing && node.filePath != path) continue;
            QByteArray bytes; QDomDocument xml;
            if (!readRenameFile(node.filePath, bytes, error) || !xml.setContent(bytes, false, &error)) return false;
            originals[node.filePath] = bytes; documents[node.filePath] = xml; documentTypes[node.filePath] = owner;
        }
    }
    for (const auto &configuration : m_configurations) {
        QByteArray bytes; QDomDocument xml;
        if (!readRenameFile(configuration.filePath, bytes, error) || !xml.setContent(bytes, false, &error)) return false;
        originals[configuration.filePath] = bytes; documents[configuration.filePath] = xml; documentTypes[configuration.filePath] = ResourceType::GameSettings;
    }
    auto renamed = documents.value(path); const QDir ownerDirectory = QFileInfo(path).absoluteDir();
    auto moveAsset = [&](const QString &source, const QString &fileName) {
        if (!QFileInfo::exists(source) || !renamedAsset(assets, source).isEmpty()) return true;
        const QString destination = QFileInfo(source).absoluteDir().filePath(fileName);
        if (!addMove(source, destination, false)) return false; assets[source] = destination; return true;
    };
    if (type == ResourceType::Sprite) {
        int index = 0;
        for (auto frame : ActionXml::elements(renamed.documentElement().firstChildElement(QStringLiteral("frames")), QStringLiteral("frame"))) {
            const QString source = referencePath(ownerDirectory, frame.text());
            if (!moveAsset(source, name + QLatin1Char('_') + QString::number(index++) + QLatin1Char('.') + QFileInfo(source).suffix())) return false;
        }
    }
    if (type == ResourceType::Background || type == ResourceType::Sound) {
        const QString reference = ActionXml::text(renamed.documentElement(), QStringLiteral("data"));
        if (!reference.isEmpty()) {
            const QString source = referencePath(type == ResourceType::Sound ? QDir(ownerDirectory.filePath(QStringLiteral("audio"))) : ownerDirectory, reference);
            if (!moveAsset(source, name + QLatin1Char('.') + QFileInfo(source).suffix())) return false;
        }
    }
    if (type == ResourceType::Font) {
        const QString source = ownerDirectory.filePath(resource.name + QStringLiteral(".png"));
        if (!moveAsset(source, name + QStringLiteral(".png"))) return false;
        ActionXml::setText(renamed.documentElement(), QStringLiteral("image"), name + QStringLiteral(".png"));
    }
    if (type == ResourceType::Extension) {
        ActionXml::setText(renamed.documentElement(), QStringLiteral("name"), name);
        const QString source = ownerDirectory.filePath(resource.name), destination = ownerDirectory.filePath(name);
        if (QFileInfo::exists(source)) {
            if (!addMove(source, destination, true)) return false;
            QDirIterator files(source, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (files.hasNext()) { const QString file = files.next(); if (!inside(file)) { error = QObject::tr("Cannot move linked extension files: %1").arg(file); return false; } if (files.fileInfo().isFile()) assets[file] = QDir(destination).filePath(QDir(source).relativeFilePath(file)); }
        }
    }
    if (!renamed.isNull()) documents[path] = renamed;
    for (auto it = documents.begin(); it != documents.end(); ++it) {
        const auto owner = documentTypes.value(it.key()); const QByteArray before = it.value().toByteArray(2);
        ResourceReferences::rename(it.value(), owner, type, resource.name, name);
        renameResourceAssets(it.value(), owner, it.key(), projectDirectory, assets);
        if (it.value().toByteArray(2) != before || it.key() == path) {
            if (!inside(it.key())) { error = QObject::tr("Cannot update references outside the project: %1").arg(it.key()); return false; }
            writes[it.key()] = it.value().toByteArray(2);
        }
    }
    originals[m_filePath] = manifestBytes; writes[m_filePath] = manifest.toByteArray(2);
    QStringList written; QList<ResourceRenameMove> moved;
    auto performMove = [](const ResourceRenameMove &move) { return move.directory ? QDir().rename(move.source, move.destination) : QFile::rename(move.source, move.destination); };
    auto rollback = [&] {
        for (int i = moved.size() - 1; i >= 0; --i) { ResourceRenameMove reverse = moved.at(i); qSwap(reverse.source, reverse.destination); if (!performMove(reverse)) error += QObject::tr("\nCannot restore %1 from %2.").arg(reverse.destination, reverse.source); }
        for (const auto &file : written) { QString restoreError; if (!writeRenameFile(file, originals.value(file), restoreError)) error += QLatin1Char('\n') + restoreError; }
    };
    for (auto it = writes.cbegin(); it != writes.cend(); ++it) {
        QByteArray current; if (!readRenameFile(it.key(), current, error) || current != originals.value(it.key())) { if (error.isEmpty()) error = QObject::tr("A resource changed outside the editor: %1").arg(it.key()); rollback(); return false; }
        if (!writeRenameFile(it.key(), it.value(), error)) { rollback(); return false; } written.append(it.key());
    }
    for (const auto &move : moves) {
        QList<ResourceRenameMove> steps;
        if (sameResourcePath(move.source, move.destination)) {
            ResourceRenameMove first = move; first.destination = QFileInfo(move.source).absoluteDir().filePath(QStringLiteral(".qtgms-rename-%1").arg(QUuid::createUuid().toString().mid(1, 36)));
            ResourceRenameMove second = move; second.source = first.destination; steps << first << second;
        } else steps << move;
        for (const auto &step : steps) {
            if (!performMove(step)) { error = QObject::tr("Cannot rename %1 to %2. Close any application using the file.").arg(step.source, step.destination); rollback(); return false; } moved.append(step);
        }
    }
    Project updated; ProjectLoader loader;
    if (!loader.load(m_filePath, updated, error)) { rollback(); return false; }
    updated.m_temporaryDirectory = m_temporaryDirectory; updated.m_imported = m_imported; *this = updated; return true;
}
