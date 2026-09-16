#include "projectexportjob.h"
#include "sevenzipwriter.h"

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <new>

class ProjectExportFiles
{
public:
    ProjectExportFiles(const Project &project, QThread *job)
        : m_directory(QFileInfo(project.filePath()).absoluteDir()), m_job(job) {}

    QStringList names() const
    {
        QStringList result = m_names.values();
        result.sort(Qt::CaseSensitive);
        return result;
    }
    QString error() const { return m_error; }

    bool add(const QString &path)
    {
        if (m_job->isInterruptionRequested()) return false;
        const QFileInfo info(path);
        const QString name = m_directory.relativeFilePath(info.absoluteFilePath());
        if (m_names.contains(name.toCaseFolded())) return true;
        const QString canonicalRoot = m_directory.canonicalPath() + QLatin1Char('/');
        if (!info.isFile() || info.isSymLink()
            || !info.canonicalFilePath().startsWith(canonicalRoot, Qt::CaseInsensitive)
            || name.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(name)) {
            m_error = ProjectExportJob::tr("Cannot export missing, linked or external project file: %1").arg(path);
            return false;
        }
        if (name.section(QLatin1Char('/'), 0, 0).compare(QStringLiteral("build"), Qt::CaseInsensitive) == 0) {
            m_error = ProjectExportJob::tr("A project resource is inside the build directory: %1").arg(path);
            return false;
        }
        m_names.insert(name.toCaseFolded(), name);
        if (m_names.size() > 100000) {
            m_error = ProjectExportJob::tr("The project contains too many files to export.");
            return false;
        }
        return true;
    }

    bool addDirectory(const QString &path, int depth = 0)
    {
        if (m_job->isInterruptionRequested()) return false;
        if (m_directories.contains(path.toCaseFolded())) return true;
        const QFileInfo info(path);
        if (depth > 128 || info.isSymLink() || !info.isDir() || !QDir(path).isReadable()
            || !info.canonicalFilePath().startsWith(m_directory.canonicalPath() + QLatin1Char('/'), Qt::CaseInsensitive)) {
            m_error = ProjectExportJob::tr("Cannot export resource directory: %1").arg(path);
            return false;
        }
        m_directories.insert(path.toCaseFolded());
        const auto entries = QDir(path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &entry : entries) {
            // Source control metadata is not a resource, even in a resource folder.
            if (entry.fileName() == QStringLiteral(".git") || entry.fileName() == QStringLiteral(".svn")) continue;
            if (!(entry.isDir() ? addDirectory(entry.absoluteFilePath(), depth + 1) : add(entry.absoluteFilePath())))
                return false;
        }
        return true;
    }

    bool addResource(const QString &path, bool hasFileReferences = false)
    {
        if (!add(path)) return false;
        const QString relative = m_directory.relativeFilePath(path);
        if (relative.contains(QLatin1Char('/'))
            && !addDirectory(m_directory.filePath(relative.section(QLatin1Char('/'), 0, 0)))) return false;
        // Resource/configuration XML may refer to assets outside the usual
        // images/audio folders. Include those existing local file references too.
        if (!hasFileReferences) return true;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { m_error = path + QStringLiteral(": ") + file.errorString(); return false; }
        QDomDocument xml;
        if (!xml.setContent(&file, false, &m_error)) return false;
        QList<QDomElement> elements = { xml.documentElement() };
        for (int i = 0; i < elements.size(); ++i) {
            const auto element = elements.at(i);
            for (auto child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
                elements.append(child);
            if (!element.firstChildElement().isNull()) continue;
            QString reference = element.text().trimmed();
            reference.replace(QLatin1Char('\\'), QLatin1Char('/'));
            if (reference.isEmpty() || reference.contains(QLatin1Char('\n'))) continue;
            const QStringList candidates = { QFileInfo(path).absoluteDir().filePath(reference),
                m_directory.filePath(reference), QFileInfo(path).absoluteDir().filePath(QStringLiteral("audio/") + reference) };
            for (const QString &candidate : candidates) {
                if (QFileInfo(candidate).isFile()) {
                    if (!add(candidate)) return false;
                    break;
                }
            }
        }
        return true;
    }

    bool addResources(const QList<ResourceNode> &resources)
    {
        for (const ResourceNode &resource : resources) {
            const bool hasFiles = resource.type == ResourceType::Sprite || resource.type == ResourceType::Sound
                || resource.type == ResourceType::Background || resource.type == ResourceType::Font
                || resource.type == ResourceType::Extension;
            if (!(resource.isGroup ? addResources(resource.children) : addResource(resource.filePath, hasFiles))) return false;
        }
        return true;
    }

private:
    QDir m_directory;
    QThread *m_job;
    QHash<QString, QString> m_names;
    QSet<QString> m_directories;
    QString m_error;
};

void ProjectExportJob::run()
{
    try {
        exportFiles();
    } catch (const std::bad_alloc &) {
        m_error = tr("Not enough memory to export the project.");
    }
    m_cancelled = isInterruptionRequested();
}

void ProjectExportJob::exportFiles()
{
    const QDir directory = QFileInfo(m_project.filePath()).absoluteDir();
    ProjectExportFiles files(m_project, this);
    bool collected = files.add(m_project.filePath());
    for (const ProjectConfiguration &configuration : m_project.configurations())
        if (collected) collected = files.addResource(configuration.filePath, true);
    for (ResourceType type : { ResourceType::Sprite, ResourceType::Sound, ResourceType::Background,
             ResourceType::Path, ResourceType::Script, ResourceType::Shader, ResourceType::Font,
             ResourceType::Timeline, ResourceType::Object, ResourceType::Room,
             ResourceType::IncludedFile, ResourceType::Extension })
        if (collected) collected = files.addResources(m_project.resources(type));
    if (collected && !m_project.informationFilePath().isEmpty())
        collected = files.add(m_project.informationFilePath());
    if (!collected) { m_error = files.error(); return; }
    const QStringList names = files.names();
    const QFileInfo target(m_destination);
    // Never replace a source asset with the export, even if it already has
    // a .gmz extension (for example an Included File).
    for (const QString &name : names) {
        const QFileInfo source(directory.filePath(name));
        if (source.absoluteFilePath().compare(target.absoluteFilePath(), Qt::CaseInsensitive) == 0
            || (!target.canonicalFilePath().isEmpty()
                && source.canonicalFilePath().compare(target.canonicalFilePath(), Qt::CaseInsensitive) == 0)) {
            m_error = tr("The export destination is a project resource. Choose another filename.");
            return;
        }
    }
    QString previousName;
    int previousPercent = -1;
    m_succeeded = SevenZipWriter::write(m_destination, directory.absolutePath(), names, m_error,
        [this, &previousName, &previousPercent](const QString &name, int percent) {
            if (isInterruptionRequested()) return false;
            if (name != previousName || percent != previousPercent) {
                emit progressChanged(name, percent);
                previousName = name;
                previousPercent = percent;
            }
            return true;
        });
}
