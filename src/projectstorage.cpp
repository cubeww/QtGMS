#include "project.h"
#include "projectloader.h"
#include "richtextdocument.h"
#include "actionxml.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>

static bool writeProjectFile(const QString &path, const QByteArray &bytes, QString &error)
{
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        error = QObject::tr("Cannot write %1:\n%2").arg(path, output.errorString());
        return false;
    }
    return true;
}

bool Project::createTemporary(Project &project, QString &error)
{
    error.clear();
    QSharedPointer<QTemporaryDir> temporary(new QTemporaryDir(QDir::tempPath() + QStringLiteral("/QtGMS-XXXXXX")));
    if (!temporary->isValid()) { error = temporary->errorString(); return false; }
    const QDir directory(temporary->path());
    // Keep the complete GMX defaults and their assets together. The official
    // editor reads configuration fields that our Windows compiler does not use.
    const QString templatePath = QStringLiteral(":/templates/project");
    QDirIterator files(templatePath, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        QFile input(files.next());
        if (!input.open(QIODevice::ReadOnly)) {
            error = input.errorString();
            return false;
        }
        const QString output = directory.filePath(input.fileName().mid(templatePath.size() + 1));
        if (!QDir().mkpath(QFileInfo(output).absolutePath())) {
            error = QObject::tr("Cannot create the project template directory: %1").arg(output);
            return false;
        }
        const QByteArray bytes = input.readAll();
        if (input.error() != QFile::NoError) {
            error = input.errorString();
            return false;
        }
        if (!writeProjectFile(output, bytes, error))
            return false;
    }
    const QByteArray manifest(
        "<assets>\n"
        "  <Configs name=\"configs\"><Config>Configs\\Default</Config></Configs>\n"
        "  <sprites name=\"sprites\"/>\n"
        "  <sounds name=\"sound\"/>\n"
        "  <backgrounds name=\"background\"/>\n"
        "  <paths name=\"paths\"/>\n"
        "  <scripts name=\"scripts\"/>\n"
        "  <shaders name=\"shaders\"/>\n"
        "  <fonts name=\"fonts\"/>\n"
        "  <timelines name=\"timelines\"/>\n"
        "  <objects name=\"objects\"/>\n"
        "  <rooms name=\"rooms\"/>\n"
        "  <datafiles name=\"datafiles\" number=\"0\"/>\n"
        "  <NewExtensions/>\n"
        "  <help><rtf>help.rtf</rtf></help>\n"
        "</assets>\n");
    const QString configurationPath = directory.filePath(QStringLiteral("Configs/Default.config.gmx"));
    QFile configurationFile(configurationPath);
    if (!configurationFile.open(QIODevice::ReadOnly)) {
        error = configurationFile.errorString();
        return false;
    }
    QDomDocument configXml;
    if (!configXml.setContent(&configurationFile, false, &error))
        return false;
    configurationFile.close();
    auto options = configXml.documentElement().firstChildElement(QStringLiteral("Options"));
    const auto guid = QUuid::createUuid();
    ActionXml::setText(options, QStringLiteral("option_gameguid"), guid.toString().toUpper());
    ActionXml::setText(options, QStringLiteral("option_gameid"), QString::number(guid.data1 & 0x7fffffffu));
    const QByteArray configuration = configXml.toByteArray(2);
    const QString path = directory.filePath(QStringLiteral("Project1.project.gmx"));
    if (!writeProjectFile(configurationPath, configuration, error)
        || !writeProjectFile(directory.filePath(QStringLiteral("help.rtf")), RichTextDocument::emptyRtf(), error)
        || !writeProjectFile(path, manifest, error)) return false;
    Project created;
    ProjectLoader loader;
    if (!loader.load(path, created, error)) return false;
    created.m_temporaryDirectory = temporary;
    project = created;
    return true;
}

static bool createProjectDirectory(const QString &path, QStringList &createdDirectories)
{
    if (QFileInfo(path).isDir()) return true;
    if (QFileInfo::exists(path)) return false;
    const QString parent = QFileInfo(path).absolutePath();
    if (parent == path || !createProjectDirectory(parent, createdDirectories) || !QDir().mkdir(path)) return false;
    createdDirectories.append(path);
    return true;
}

bool Project::saveTemporaryAs(const QString &filePath, QString &error)
{
    error.clear();
    if (!isTemporary()) { error = QObject::tr("This project already has a permanent location."); return false; }
    if (!filePath.endsWith(QStringLiteral(".project.gmx"), Qt::CaseInsensitive)) {
        error = QObject::tr("Choose a filename ending in .project.gmx."); return false;
    }
    const QString target = QFileInfo(filePath).absoluteFilePath();
    const QDir destination = QFileInfo(target).absoluteDir();
    if (!destination.exists()) { error = QObject::tr("Choose an existing project directory."); return false; }
    const QString sourcePath = QFileInfo(m_filePath).absolutePath();
    const QString parentPath = QFileInfo(destination.absolutePath()).canonicalFilePath();
    if (parentPath.compare(sourcePath, Qt::CaseInsensitive) == 0
        || parentPath.startsWith(sourcePath + QLatin1Char('/'), Qt::CaseInsensitive)) {
        error = QObject::tr("Choose a location outside the temporary project directory."); return false;
    }
    QFile manifest(m_filePath);
    if (!manifest.open(QIODevice::ReadOnly)) { error = manifest.errorString(); return false; }
    if (manifest.readAll() != m_sourceBytes) {
        error = QObject::tr("The project file changed outside the editor. Reopen it before saving."); return false;
    }
    manifest.close();
    const QDir source(sourcePath);
    QStringList sourceFiles;
    QStringList directories;
    QDirIterator files(sourcePath, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                       QDirIterator::Subdirectories);
    while (files.hasNext()) {
        files.next();
        const QFileInfo info = files.fileInfo();
        if (info.absoluteFilePath() == m_filePath) continue;
        const QString relativePath = source.relativeFilePath(info.absoluteFilePath());
        // Build products and caches do not belong in a newly saved project.
        if (relativePath.compare(QStringLiteral("build"), Qt::CaseInsensitive) == 0
            || relativePath.startsWith(QStringLiteral("build/"), Qt::CaseInsensitive)) continue;
        const QString targetPath = destination.filePath(relativePath);
        if (info.isSymLink()) { error = QObject::tr("Cannot save a temporary project containing a linked file: %1").arg(info.filePath()); return false; }
        if (info.isDir()) {
            if (QFileInfo::exists(targetPath) && !QFileInfo(targetPath).isDir()) {
                error = QObject::tr("A file occupies the resource directory: %1").arg(targetPath); return false;
            }
            directories.append(targetPath);
        } else {
            if (QFileInfo::exists(targetPath) || targetPath.compare(target, Qt::CaseInsensitive) == 0) {
                error = QObject::tr("A resource file already exists at the destination: %1\nChoose another project directory.").arg(targetPath); return false;
            }
            sourceFiles.append(info.absoluteFilePath());
        }
    }
    QStringList createdDirectories;
    QStringList createdFiles;
    for (const QString &directory : directories) {
        if (!createProjectDirectory(directory, createdDirectories)) {
            error = QObject::tr("Cannot create resource directory: %1").arg(directory); break;
        }
    }
    if (error.isEmpty()) {
        for (const QString &path : sourceFiles) {
            const QString output = destination.filePath(source.relativeFilePath(path));
            if (!QFile::copy(path, output)) { error = QObject::tr("Cannot copy project file: %1").arg(path); break; }
            createdFiles.append(output);
        }
    }
    // Publish the manifest last. Roll back only files created by this attempt.
    if (!error.isEmpty() || !writeProjectFile(target, m_sourceBytes, error)) {
        for (const QString &path : createdFiles) {
            if (!QFile::remove(path)) error += QObject::tr("\nCannot remove copied resource: %1").arg(path);
        }
        for (int i = createdDirectories.size() - 1; i >= 0; --i) QDir().rmdir(createdDirectories.at(i));
        return false;
    }
    Project saved;
    ProjectLoader loader;
    if (!loader.load(target, saved, error)) return false;
    *this = saved;
    return true;
}


bool Project::ensureInformationFile(QString &error)
{
    if (!isOpen()) { error = QObject::tr("Open a project before editing Game Information."); return false; }
    if (!m_informationFilePath.isEmpty()) {
        if (QFileInfo::exists(m_informationFilePath)) return true;
        error = QObject::tr("The Game Information file is missing: %1").arg(m_informationFilePath); return false;
    }
    // Projects without a help entry get a new RTF file, registered only after
    // the file is written. Never replace an unregistered existing file.
    QFile source(m_filePath); if (!source.open(QIODevice::ReadOnly)) { error = source.errorString(); return false; }
    if (source.readAll() != m_sourceBytes || source.error() != QFile::NoError) { error = QObject::tr("The project changed outside the editor. Reopen it before adding Game Information."); return false; } source.close();
    const QDir directory = QFileInfo(m_filePath).absoluteDir(); QString path = directory.filePath(QStringLiteral("help.rtf"));
    for (int i = 1; QFileInfo::exists(path); ++i) path = directory.filePath(QStringLiteral("help%1.rtf").arg(i));
    QDomDocument xml; if (!xml.setContent(m_sourceBytes, false, &error)) return false;
    ActionXml::setText(ActionXml::child(xml.documentElement(), QStringLiteral("help")), QStringLiteral("rtf"), QFileInfo(path).fileName());
    const QByteArray bytes = xml.toByteArray(2);
    if (!writeProjectFile(path, RichTextDocument::emptyRtf(), error)) return false;
    if (!writeProjectFile(m_filePath, bytes, error)) { if (!QFile::remove(path)) error += QObject::tr("\nCannot remove the unregistered information file: %1").arg(path); return false; }
    m_sourceBytes = bytes; m_informationFilePath = path; return true;
}
