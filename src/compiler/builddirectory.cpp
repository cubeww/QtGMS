#include "builddirectory.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

QString BuildDirectory::path(const Project &project)
{
    return QFileInfo(project.filePath()).absoluteDir().filePath(QStringLiteral("build"));
}

QString BuildDirectory::executablePath(const Project &project)
{
    QString name = QFileInfo(project.filePath()).completeBaseName();
    if (name.endsWith(QStringLiteral(".project"), Qt::CaseInsensitive))
        name.chop(8);
    return QDir(path(project)).filePath(name + QStringLiteral(".exe"));
}

bool BuildDirectory::validate(const Project &project, QString &error)
{
    if (!project.isOpen()) {
        error = QObject::tr("Open a project first.");
        return false;
    }
    const QString directory = path(project);
    const QString root = QFileInfo(project.filePath()).absoluteDir().canonicalPath();
    if (root.isEmpty() || QFileInfo(directory).absoluteDir().canonicalPath() != root) {
        error = QObject::tr("Cannot resolve the project's build directory.");
        return false;
    }
    QStringList pending { directory };
    while (!pending.isEmpty()) {
        const QString current = pending.takeLast();
        const DWORD attributes
            = GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(current).utf16()));
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            error = QObject::tr("Build output contains a linked path: %1").arg(current);
            return false;
        }
        const QFileInfo info(current);
        if (info.isDir()) {
            for (const auto &entry :
                QDir(current).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
                pending.append(entry.absoluteFilePath());
        } else if (current == directory && info.exists()) {
            error = QObject::tr("The build path is a file: %1").arg(directory);
            return false;
        }
    }
    return true;
}

bool BuildDirectory::clean(const Project &project, QString &error)
{
    if (!validate(project, error))
        return false;
    const QString directory = path(project);
    if (!QFileInfo::exists(directory))
        return true;
    const QString canonical = QFileInfo(directory).canonicalFilePath();
    if (QFileInfo(QCoreApplication::applicationFilePath())
            .canonicalFilePath()
            .startsWith(canonical + QLatin1Char('/'), Qt::CaseInsensitive)) {
        error = QObject::tr("Cannot clean the directory containing the running editor.");
        return false;
    }
    if (!QDir(directory).removeRecursively()) {
        error = QObject::tr("Cannot completely clean %1. A file may still be in use.").arg(directory);
        return false;
    }
    return true;
}
