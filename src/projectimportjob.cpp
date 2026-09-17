#include "projectimportjob.h"
#include "projectloader.h"
#include "sevenziparchive.h"
#include "legacyprojectimporter.h"
#include "gm82projectimporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTemporaryDir>
#include <QVector>
#include <limits>
#include <new>
#include <cstring>

QString ProjectImportJob::archiveError(int status)
{
    if (status == SZ_ERROR_UNSUPPORTED)
        return tr("The GMZ package uses unsupported compression or encryption.");
    if (status == SZ_ERROR_MEM)
        return tr("Not enough memory to unpack the GMZ package (maximum decoder allocation: 512 MiB).");
    if (status == SZ_ERROR_CRC)
        return tr("The GMZ package is damaged (CRC mismatch).");
    return tr("Cannot read the GMZ package (archive error %1).").arg(status);
}

static bool projectPathsInside(const QList<ResourceNode> &resources, const QDir &directory, QString &invalidPath)
{
    for (const ResourceNode &resource : resources) {
        if (resource.isGroup) {
            if (!projectPathsInside(resource.children, directory, invalidPath)) return false;
        } else if (!SevenZipArchive::validPath(directory.relativeFilePath(resource.filePath))) {
            invalidPath = resource.filePath;
            return false;
        }
    }
    return true;
}

void ProjectImportJob::run()
{
    try {
        if (m_path.endsWith(QStringLiteral(".gmz"), Qt::CaseInsensitive)) importFiles();
        else importLegacyProject();
    } catch (const QString &error) {
        m_error = error;
    } catch (const std::bad_alloc &) {
        m_error = tr("Not enough memory to import the project.");
    }
    m_cancelled = isInterruptionRequested();
    if (m_cancelled) m_project = Project();
}

bool ProjectImportJob::supportsFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("gmz") || suffix == QStringLiteral("gmk")
        || suffix == QStringLiteral("gm81") || suffix == QStringLiteral("gm82");
}

void ProjectImportJob::importLegacyProject()
{
    emit progressChanged(tr("Reading legacy project..."), 0);
    Project temporary;
    if (!Project::createTemporary(temporary, m_error)) return;
    const auto cancelled = [this] { return isInterruptionRequested(); };
    const auto progress = [this](const QString &name, int percent) { emit progressChanged(name, percent); };
    QStringList warnings;
    if (m_path.endsWith(QStringLiteral(".gm82"), Qt::CaseInsensitive)) {
        Gm82ProjectImporter importer(m_path, temporary.filePath(), cancelled, progress);
        importer.importProject();
        warnings = importer.warnings();
    } else {
        LegacyProjectImporter importer(m_path, temporary.filePath(), m_legacyTextEncoding, cancelled, progress);
        importer.importProject();
        warnings = importer.warnings();
    }
    if (isInterruptionRequested()) return;
    QString manifest = QFileInfo(temporary.filePath()).absoluteDir().filePath(
        QFileInfo(m_path).completeBaseName() + QStringLiteral(".project.gmx"));
    if (manifest.compare(temporary.filePath(), Qt::CaseInsensitive) == 0) manifest = temporary.filePath();
    if (manifest != temporary.filePath() && !QFile::rename(temporary.filePath(), manifest)) {
        m_error = tr("Cannot name the imported project: %1").arg(manifest);
        return;
    }
    Project imported;
    ProjectLoader loader;
    if (!loader.load(manifest, imported, m_error)) return;
    imported.m_temporaryDirectory = temporary.m_temporaryDirectory;
    imported.m_imported = true;
    imported.m_warnings.append(warnings);
    m_project = imported;
}

void ProjectImportJob::importFiles()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) { m_error = file.errorString(); return; }
    SevenZipArchive archive(file, [this] { return isInterruptionRequested(); });
    const SRes opened = archive.open();
    if (opened != SZ_OK) { m_error = archiveError(opened); return; }
    const auto &entries = archive.directory();
    if (entries.NumFiles > 100000) { m_error = tr("The GMZ package contains too many files."); return; }
    QStringList paths;
    QStringList manifests;
    QHash<QString, UInt32> names;
    QSet<QString> fileNames;
    int manifestDepth = std::numeric_limits<int>::max();
    // Validate the entire directory before extracting into fresh private storage.
    // Links and file attributes are never applied to the filesystem.
    for (UInt32 index = 0; index < entries.NumFiles; ++index) {
        if (isInterruptionRequested()) return;
        const size_t length = SzArEx_GetFileNameUtf16(&entries, index, nullptr);
        if (!length || length > 32768) { m_error = tr("Invalid filename in the GMZ package."); return; }
        QVector<UInt16> buffer(int(length), UInt16(0));
        SzArEx_GetFileNameUtf16(&entries, index, buffer.data());
        QString name = QString::fromUtf16(buffer.constData(), int(length - 1));
        name.replace(QLatin1Char('\\'), QLatin1Char('/'));
        const bool isDirectory = SzArEx_IsDir(&entries, index);
        if (isDirectory && name.endsWith(QLatin1Char('/'))) name.chop(1);
        const UInt32 attributes = SzBitWithVals_Check(&entries.Attribs, index) ? entries.Attribs.Vals[index] : 0;
        const QString key = name.toCaseFolded();
        if (!SevenZipArchive::validPath(name) || (attributes & 0x400)
            || ((attributes >> 16) & 0170000) == 0120000) {
            m_error = tr("Invalid, duplicate or linked path in the GMZ package: %1").arg(name);
            return;
        }
        paths.append(name);
        const auto previous = names.constFind(key);
        if (previous != names.cend()) {
            if (isDirectory != bool(SzArEx_IsDir(&entries, previous.value()))
                || SzArEx_GetFileSize(&entries, index) != SzArEx_GetFileSize(&entries, previous.value())) {
                m_error = tr("Invalid, duplicate or linked path in the GMZ package: %1").arg(name);
                return;
            }
            // Windows paths are case-insensitive. Compare duplicate file data
            // during extraction before accepting either spelling as one file.
            continue;
        }
        names.insert(key, index);
        if (!isDirectory) {
            fileNames.insert(key);
            if (name.endsWith(QStringLiteral(".project.gmx"), Qt::CaseInsensitive)) {
                const int depth = name.count(QLatin1Char('/'));
                if (depth < manifestDepth) { manifests.clear(); manifestDepth = depth; }
                if (depth == manifestDepth) manifests.append(name);
            }
        }
    }
    if (manifests.size() != 1) {
        m_error = tr("A GMZ package must contain one project definition.");
        return;
    }
    for (const QString &path : paths) {
        QString parent = path.toCaseFolded();
        while (parent.contains(QLatin1Char('/'))) {
            parent.truncate(parent.lastIndexOf(QLatin1Char('/')));
            if (fileNames.contains(parent)) {
                m_error = tr("A file occupies a directory path in the GMZ package: %1").arg(path);
                return;
            }
        }
    }
    QSharedPointer<QTemporaryDir> temporary(new QTemporaryDir(QDir::tempPath() + QStringLiteral("/QtGMS-XXXXXX")));
    if (!temporary->isValid()) { m_error = temporary->errorString(); return; }
    const QDir destination(temporary->path());
    for (UInt32 index = 0; index < entries.NumFiles; ++index) {
        if (isInterruptionRequested()) return;
        const QString name = paths.at(int(index));
        emit progressChanged(name, int(double(index) / entries.NumFiles * 95));
        const QString target = destination.filePath(name);
        const bool isDirectory = SzArEx_IsDir(&entries, index);
        if (!QDir().mkpath(isDirectory ? target : QFileInfo(target).absolutePath())) {
            m_error = tr("Cannot create the project directory: %1").arg(target);
            return;
        }
        if (isDirectory) continue;
        const char *data = nullptr;
        size_t size = 0;
        const SRes extracted = archive.extract(index, data, size);
        if (extracted != SZ_OK) { m_error = name + QStringLiteral(": ") + archiveError(extracted); return; }
        if (isInterruptionRequested()) return;
        const UInt32 firstIndex = names.value(name.toCaseFolded());
        if (firstIndex != index) {
            QFile original(destination.filePath(paths.at(int(firstIndex))));
            bool identical = original.open(QIODevice::ReadOnly) && original.size() == qint64(size);
            size_t offset = 0;
            while (identical && offset < size) {
                if (isInterruptionRequested()) return;
                const qint64 count = qint64(qMin(size - offset, size_t(64 * 1024)));
                const QByteArray bytes = original.read(count);
                identical = bytes.size() == count && std::memcmp(bytes.constData(), data + offset, size_t(count)) == 0;
                offset += size_t(count);
            }
            if (!identical) {
                m_error = tr("Invalid, duplicate or linked path in the GMZ package: %1").arg(name);
                return;
            }
            continue;
        }
        QFile output(target);
        if (!output.open(QIODevice::WriteOnly) || output.write(data, qint64(size)) != qint64(size) || !output.flush()) {
            m_error = tr("Cannot unpack %1:\n%2").arg(name, output.errorString());
            return;
        }
    }
    if (isInterruptionRequested()) return;
    emit progressChanged(tr("Reading project..."), 95);
    const QString manifest = destination.filePath(manifests.first());
    Project imported;
    ProjectLoader loader;
    if (!loader.load(manifest, imported, m_error)) return;
    const QDir root = QFileInfo(manifest).absoluteDir();
    QString invalidPath;
    for (ResourceType type : { ResourceType::Sprite, ResourceType::Sound, ResourceType::Background,
            ResourceType::Path, ResourceType::Script, ResourceType::Shader, ResourceType::Font,
            ResourceType::Timeline, ResourceType::Object, ResourceType::Room,
            ResourceType::IncludedFile, ResourceType::Extension }) {
        if (!projectPathsInside(imported.resources(type), root, invalidPath)) break;
    }
    for (const ProjectConfiguration &configuration : imported.configurations())
        if (!SevenZipArchive::validPath(root.relativeFilePath(configuration.filePath))) invalidPath = configuration.filePath;
    if (!imported.informationFilePath().isEmpty()
        && !SevenZipArchive::validPath(root.relativeFilePath(imported.informationFilePath())))
        invalidPath = imported.informationFilePath();
    if (!invalidPath.isEmpty()) { m_error = tr("The imported project references a file outside its directory: %1").arg(invalidPath); return; }
    if (isInterruptionRequested()) return;
    imported.m_temporaryDirectory = temporary;
    imported.m_imported = true;
    m_project = imported;
}
