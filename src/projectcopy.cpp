#include "project.h"
#include "extensionpackage.h"
#include "projectfiletransaction.h"
#include "resourcereferences.h"
#include "actionxml.h"
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegExp>
#include <QSet>
#include <QUuid>
#include <functional>

bool Project::copyResource(ResourceType type, const QString &source, const QList<int> &groupPath, ResourceNode &created, QString &error)
{
    error.clear();
    if (type == ResourceType::Extension && source.endsWith(QStringLiteral(".gmez"), Qt::CaseInsensitive)) {
        ExtensionPackage package;
        if (!package.load(source, error)) return false;
        return copyResource(type, package.resourcePath(), groupPath, created, error);
    }
    const QString tag = ResourceReferences::tag(type), suffix = ResourceReferences::suffix(type);
    if (!isOpen() || tag.isEmpty() || !QFileInfo(source).isFile() || (!suffix.isEmpty() && !source.endsWith(suffix, Qt::CaseInsensitive))) {
        error = QObject::tr("Select a valid resource file for this category."); return false;
    }
    const QDir directory = QFileInfo(m_filePath).absoluteDir(), sourceDirectory = QFileInfo(source).absoluteDir();
    QList<ResourceNode> tree = resources(type); auto *target = &tree;
    QString targetDirectory;
    const QString groupTag = type == ResourceType::Extension ? QStringLiteral("NewExtensions") : tag + QLatin1Char('s');
    QDomDocument manifest;
    if (!manifest.setContent(m_sourceBytes, false, &error)) return false;
    if (type == ResourceType::IncludedFile) targetDirectory = manifest.documentElement().firstChildElement(groupTag).attribute(QStringLiteral("name"), QStringLiteral("datafiles"));
    else targetDirectory = type == ResourceType::Extension ? QStringLiteral("extensions") : type == ResourceType::Sound || type == ResourceType::Background ? tag : groupTag;
    for (int index : groupPath) {
        if (index < 0 || index >= target->size() || !target->at(index).isGroup) { error = QObject::tr("The destination folder no longer exists."); return false; }
        if (type == ResourceType::IncludedFile) targetDirectory += QLatin1Char('/') + target->at(index).name;
        target = &(*target)[index].children;
    }
    QString originalName = QFileInfo(source).fileName(); if (!suffix.isEmpty()) originalName.chop(suffix.size());
    QSet<QString> names; bool duplicate = false; ResourceNode sourceNode;
    for (auto it = m_resources.cbegin(); it != m_resources.cend(); ++it) for (const auto &node : ActionXml::resourceList(*this, it.key())) {
        names.insert(node.name.toLower());
        if (node.type == type && node.filePath.compare(source, Qt::CaseInsensitive) == 0) { duplicate = true; sourceNode = node; }
    }
    for (const auto &config : m_configurations) for (const auto &macro : config.macros) names.insert(macro.name.toLower());
    QString base = type == ResourceType::IncludedFile ? QFileInfo(source).completeBaseName() : originalName;
    const QString extension = type == ResourceType::IncludedFile && !QFileInfo(source).suffix().isEmpty() ? QLatin1Char('.') + QFileInfo(source).suffix() : QString();
    if (type != ResourceType::IncludedFile) base.replace(QRegExp(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    if (base.isEmpty() || (type != ResourceType::IncludedFile && base.at(0).isDigit())) base.prepend(QLatin1Char('_'));
    base = base.left(96); if (duplicate) base += QStringLiteral("_copy");
    const QStringList reserved = QStringLiteral("if then else begin end while do for repeat until with switch case default break continue exit return var globalvar div mod and or xor not enum true false self other all noone global local pi undefined pointer_null pointer_invalid").split(QLatin1Char(' '));
    if (reserved.contains(base) || QRegExp(QStringLiteral("(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])"), Qt::CaseInsensitive).exactMatch(base)) base += QLatin1Char('_');
    int number = 0;
    do {
        created = ResourceNode(); created.type = type; created.name = base + (number ? QString::number(number) : QString()) + extension;
        created.filePath = directory.filePath(targetDirectory + QLatin1Char('/') + created.name + suffix); ++number;
    } while (names.contains(created.name.toLower()) || QFileInfo::exists(created.filePath)
             || QFileInfo::exists(directory.filePath(targetDirectory + QLatin1Char('/') + created.name + QStringLiteral(".png")))
             || (type == ResourceType::Extension && QFileInfo::exists(directory.filePath(targetDirectory + QLatin1Char('/') + created.name))));
    const bool xmlResource = type != ResourceType::Script && type != ResourceType::Shader && type != ResourceType::IncludedFile;
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) { error = input.errorString(); return false; }
    const QByteArray bytes = xmlResource ? input.readAll() : QByteArray();
    if (input.error() != QFile::NoError) { error = input.errorString(); return false; }
    input.close();
    QDomDocument xml;
    if (xmlResource && (!xml.setContent(bytes, false, &error) || xml.documentElement().tagName() != tag)) {
        error = QObject::tr("Invalid %1 resource: %2\n%3").arg(tag, source, error); return false;
    }
    QMap<QString, QString> copies;
    const QDir owner = QFileInfo(created.filePath).absoluteDir();
    auto copyAsset = [&](const QDir &from, const QString &reference, const QString &destination) {
        if (reference.isEmpty()) return;
        const QString asset = QDir::cleanPath(from.absoluteFilePath(QString(reference).replace(QLatin1Char('\\'), QLatin1Char('/'))));
        copies[destination] = asset;
    };
    auto root = xml.documentElement();
    if (type == ResourceType::Sprite) {
        int index = 0;
        for (auto frame : ActionXml::elements(root.firstChildElement(QStringLiteral("frames")), QStringLiteral("frame"))) {
            const QString reference = QStringLiteral("images/%1_%2.%3").arg(created.name).arg(index++).arg(QFileInfo(frame.text()).suffix());
            copyAsset(sourceDirectory, frame.text(), owner.filePath(reference));
            while (!frame.firstChild().isNull()) frame.removeChild(frame.firstChild()); frame.appendChild(xml.createTextNode(reference));
        }
    }
    if (type == ResourceType::Sound || type == ResourceType::Background) {
        const QString original = ActionXml::text(root, QStringLiteral("data"));
        if (!original.isEmpty()) {
            const QString relative = created.name + QLatin1Char('.') + QFileInfo(original).suffix();
            const QString asset = (type == ResourceType::Sound ? QStringLiteral("audio/") : QStringLiteral("images/")) + relative;
            copyAsset(type == ResourceType::Sound ? QDir(sourceDirectory.filePath(QStringLiteral("audio"))) : sourceDirectory, original, owner.filePath(asset));
            ActionXml::setText(root, QStringLiteral("data"), type == ResourceType::Sound ? relative : asset);
            if (type == ResourceType::Sound) ActionXml::setText(root, QStringLiteral("origname"), asset);
        }
    }
    if (type == ResourceType::Font) {
        const QString original = originalName + QStringLiteral(".png");
        if (QFileInfo::exists(sourceDirectory.filePath(original))) copyAsset(sourceDirectory, original, owner.filePath(created.name + QStringLiteral(".png")));
        ActionXml::setText(root, QStringLiteral("image"), created.name + QStringLiteral(".png"));
    }
    if (type == ResourceType::Extension) {
        const QDir content(sourceDirectory.filePath(originalName));
        QDirIterator files(content.absolutePath(), QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (files.hasNext()) { const QString file = files.next(); if (files.fileInfo().isSymLink()) { error = QObject::tr("Linked extension files cannot be copied."); return false; } copies[owner.filePath(created.name + QLatin1Char('/') + content.relativeFilePath(file))] = file; }
        ActionXml::setText(root, QStringLiteral("name"), created.name);
    }
    if (type == ResourceType::Room) for (const auto &container : {QStringLiteral("instances"), QStringLiteral("tiles")}) {
        auto parent = root.firstChildElement(container);
        for (auto entity = parent.firstChildElement(); !entity.isNull(); entity = entity.nextSiblingElement())
            if (entity.hasAttribute(QStringLiteral("name"))) entity.setAttribute(QStringLiteral("name"), QStringLiteral("inst_") + QUuid::createUuid().toString().mid(1, 8).toUpper());
    }
    if (type == ResourceType::Path && !duplicate) ActionXml::setText(root, QStringLiteral("backroom"), QStringLiteral("-1"));
    if (xmlResource) ResourceReferences::rename(xml, type, type, originalName, created.name);

    QDomElement entry;
    if (duplicate) {
        // Locate the manifest template using the same tree path as the loader.
        std::function<void(QDomElement, const QString &)> find = [&](QDomElement parent, const QString &dataDirectory) {
            for (auto child = parent.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
                if (child.tagName() == groupTag) find(child, dataDirectory + QLatin1Char('/') + child.attribute(QStringLiteral("name")));
                else if (child.tagName() == tag) {
                    QString ref = type == ResourceType::IncludedFile ? dataDirectory + QLatin1Char('/') + ActionXml::text(child, QStringLiteral("name")) : child.text().trimmed();
                    if (!suffix.isEmpty() && !ref.endsWith(suffix, Qt::CaseInsensitive)) ref += suffix;
                    if (QDir::cleanPath(directory.absoluteFilePath(ref.replace(QLatin1Char('\\'), QLatin1Char('/')))).compare(source, Qt::CaseInsensitive) == 0) entry = child.cloneNode(true).toElement();
                }
            }
        };
        auto parent = manifest.documentElement().firstChildElement(groupTag); find(parent, parent.attribute(QStringLiteral("name")));
    }
    if (entry.isNull()) entry = manifest.createElement(tag);
    if (type == ResourceType::IncludedFile) {
        ActionXml::setText(entry, QStringLiteral("name"), created.name);
        ActionXml::setText(entry, QStringLiteral("filename"), created.name);
        if (!duplicate) {
            ActionXml::setText(entry, QStringLiteral("exists"), QStringLiteral("-1"));
            ActionXml::setText(entry, QStringLiteral("size"), QString::number(QFileInfo(source).size()));
            ActionXml::setText(entry, QStringLiteral("exportAction"), QStringLiteral("2"));
            ActionXml::setText(entry, QStringLiteral("exportDir"), QString());
            ActionXml::setText(entry, QStringLiteral("overwrite"), QStringLiteral("-1"));
            ActionXml::setText(entry, QStringLiteral("freeData"), QStringLiteral("-1"));
            ActionXml::setText(entry, QStringLiteral("removeEnd"), QStringLiteral("0"));
            ActionXml::setText(entry, QStringLiteral("store"), QStringLiteral("0"));
            auto options = ActionXml::child(entry, QStringLiteral("ConfigOptions"));
            for (const auto &configuration : m_configurations) {
                auto config = manifest.createElement(QStringLiteral("Config")); config.setAttribute(QStringLiteral("name"), configuration.name);
                ActionXml::setText(config, QStringLiteral("CopyToMask"), QStringLiteral("9223372036854775807")); options.appendChild(config);
            }
        }
    } else {
        if (type == ResourceType::Shader) { created.value = duplicate ? sourceNode.value : QStringLiteral("GLSLES"); entry.setAttribute(QStringLiteral("type"), created.value); }
        QString reference = directory.relativeFilePath(created.filePath);
        if (xmlResource) reference.chop(suffix.size());
        while (!entry.firstChild().isNull()) entry.removeChild(entry.firstChild());
        entry.appendChild(manifest.createTextNode(reference.replace(QLatin1Char('/'), QLatin1Char('\\'))));
    }
    ProjectFileTransaction transaction(directory.absolutePath());
    for (auto it = copies.cbegin(); it != copies.cend(); ++it)
        if (!transaction.copy(it.value(), it.key(), error)) { transaction.rollback(error); return false; }
    if (QFileInfo::exists(created.filePath)) { error = QObject::tr("The destination already exists: %1").arg(created.filePath); transaction.rollback(error); return false; }
    if (!(xmlResource ? transaction.write(created.filePath, xml.toByteArray(2), error) : transaction.copy(source, created.filePath, error))) { transaction.rollback(error); return false; }
    target->append(created);
    QMap<QString, QDomElement> entries; entries[created.filePath] = entry;
    if (!writeResourceTree(type, tree, entries, error)) { transaction.rollback(error); return false; }
    for (const auto &node : ActionXml::resourceList(*this, type)) if (node.filePath == created.filePath) created = node;
    transaction.finish(error); return true;
}
