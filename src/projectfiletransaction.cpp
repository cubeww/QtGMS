#include "projectfiletransaction.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QObject>

ProjectFileTransaction::ProjectFileTransaction(const QString &directory)
    : m_directory(directory), m_backup(m_directory.filePath(QStringLiteral(".qtgms-tree-XXXXXX")))
{ m_backup.setAutoRemove(false); }

ProjectFileTransaction::~ProjectFileTransaction()
{
    // Callers report rollback errors. Leave remaining backups available if an
    // unexpected early return occurs after a mutation.
    if (!m_finished && m_originals.isEmpty()) m_backup.remove();
}

bool ProjectFileTransaction::containsPath(const QString &path) const
{
    QString current = QFileInfo(path).absoluteFilePath();
    if (!current.startsWith(m_directory.absolutePath() + QLatin1Char('/'), Qt::CaseInsensitive)) return false;
    while (current.compare(m_directory.absolutePath(), Qt::CaseInsensitive)) {
        if (QFileInfo(current).isSymLink()) return false;
        const QString parent = QFileInfo(current).absolutePath();
        if (parent == current) return false;
        current = parent;
    }
    return true;
}

bool ProjectFileTransaction::remember(const QString &path, QString &error)
{
    if (!m_backup.isValid() || !containsPath(path)) { error = QObject::tr("Cannot edit a linked path or a path outside the project: %1").arg(path); return false; }
    if (m_originals.contains(path)) return true;
    if (QFileInfo::exists(path)) {
        const QString backup = QDir(m_backup.path()).filePath(QString::number(m_originals.size()));
        if (!QFileInfo(path).isFile() || !QFile::copy(path, backup)) { error = QObject::tr("Cannot back up %1.").arg(path); return false; }
        m_originals[path] = backup;
    } else m_originals[path] = QString();
    QString parent = QFileInfo(path).absolutePath();
    QStringList missing;
    while (!QFileInfo::exists(parent)) { missing.prepend(parent); parent = QFileInfo(parent).absolutePath(); }
    for (const auto &directory : missing) {
        if (!QDir().mkdir(directory)) { error = QObject::tr("Cannot create %1.").arg(directory); return false; }
        m_createdDirectories.append(directory);
    }
    return true;
}

bool ProjectFileTransaction::write(const QString &path, const QByteArray &bytes, QString &error)
{
    if (!remember(path, error)) return false;
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) return true;
    error = QObject::tr("Cannot save %1:\n%2").arg(path, file.errorString()); return false;
}

bool ProjectFileTransaction::remove(const QString &path, QString &error)
{
    if (!remember(path, error)) return false;
    if (!QFileInfo::exists(path) || QFile::remove(path)) return true;
    error = QObject::tr("Cannot remove %1.").arg(path); return false;
}

bool ProjectFileTransaction::copy(const QString &source, const QString &destination, QString &error, bool overwrite)
{
    const QFileInfo sourceInfo(source), destinationInfo(destination);
    if (destinationInfo.exists() && !overwrite) { error = QObject::tr("The destination already exists: %1").arg(destination); return false; }
    if (!sourceInfo.isFile()) { error = QObject::tr("Cannot copy %1 to %2.").arg(source, destination); return false; }
    if (!remember(destination, error)) return false;
    if (destinationInfo.exists()) {
        if (sourceInfo.canonicalFilePath().compare(destinationInfo.canonicalFilePath(), Qt::CaseInsensitive) == 0) return true;
        if (!QFile::remove(destination)) { error = QObject::tr("Cannot remove %1.").arg(destination); return false; }
    }
    if (QFile::copy(source, destination)) return true;
    error = QObject::tr("Cannot copy %1 to %2.").arg(source, destination); return false;
}

bool ProjectFileTransaction::move(const QString &source, const QString &destination, QString &error)
{
    if (source == destination) return true;
    const bool caseChange = source.compare(destination, Qt::CaseInsensitive) == 0;
    if (QFileInfo::exists(destination) && !caseChange) { error = QObject::tr("The destination already exists: %1").arg(destination); return false; }
    if (!remember(source, error)) return false;
    if (!caseChange && !remember(destination, error)) return false;
    // The temporary hop also handles case-only changes on Windows.
    const QString hop = QDir(m_backup.path()).filePath(QStringLiteral("moving"));
    if (!QFile::rename(source, hop)) { error = QObject::tr("Cannot move %1.").arg(source); return false; }
    if (!QFile::rename(hop, destination)) {
        QFile::rename(hop, source); error = QObject::tr("Cannot move %1 to %2.").arg(source, destination); return false;
    }
    return true;
}

void ProjectFileTransaction::rollback(QString &error)
{
    bool restored = true;
    for (auto it = m_originals.cbegin(); it != m_originals.cend(); ++it) {
        bool ok = true;
        if (it.value().isEmpty()) ok = !QFileInfo::exists(it.key()) || QFile::remove(it.key());
        else {
            QFile source(it.value());
            QSaveFile destination(it.key());
            ok = source.open(QIODevice::ReadOnly) && destination.open(QIODevice::WriteOnly);
            while (ok && !source.atEnd()) {
                const QByteArray block = source.read(1024 * 1024);
                ok = source.error() == QFile::NoError && destination.write(block) == block.size();
            }
            ok = ok && destination.commit();
        }
        if (!ok) { restored = false; error += QObject::tr("\nCould not restore %1. Recovery files: %2").arg(it.key(), m_backup.path()); }
    }
    for (int i = m_createdDirectories.size() - 1; i >= 0; --i) QDir().rmdir(m_createdDirectories.at(i));
    if (restored) m_backup.remove();
    m_finished = true;
}

bool ProjectFileTransaction::finish(QString &error)
{
    m_finished = true;
    if (!m_backup.remove()) error = QObject::tr("Changes were saved. Temporary recovery files remain at %1.").arg(m_backup.path());
    return true;
}
