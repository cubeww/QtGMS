#include "extensionpackage.h"
#include "actionxml.h"
#include "extensiondocument.h"

#include "sevenziparchive.h"
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSet>
#include <QVector>
#include <limits>

static QString archiveError(SRes status)
{
    if (status == SZ_ERROR_UNSUPPORTED)
        return QObject::tr("The extension package uses unsupported compression or encryption.");
    if (status == SZ_ERROR_MEM)
        return QObject::tr("Not enough memory to unpack the extension package (maximum decoder allocation: 512 MiB).");
    if (status == SZ_ERROR_CRC)
        return QObject::tr("The extension package is damaged (CRC mismatch).");
    return QObject::tr("Cannot read the GMEZ extension package (archive error %1).").arg(status);
}

bool ExtensionPackage::load(const QString &path, QString &error)
{
    m_resourcePath.clear();
    if (!m_directory.isValid()) {
        error = QObject::tr("Cannot create a temporary extension import directory.");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    SevenZipArchive archive(file);
    const SRes opened = archive.open();
    if (opened != SZ_OK) {
        error = archiveError(opened);
        return false;
    }
    const auto &directory = archive.directory();
    if (directory.NumFiles > 100000) {
        error = QObject::tr("The extension package contains too many files.");
        return false;
    }
    QStringList paths;
    QSet<QString> names;
    QStringList resources;
    int resourceDepth = std::numeric_limits<int>::max();
    // Validate every name before writing anything. Never apply archive links,
    // permissions or reparse points to the temporary directory.
    for (UInt32 i = 0; i < directory.NumFiles; ++i) {
        const size_t length = SzArEx_GetFileNameUtf16(&directory, i, nullptr);
        if (length == 0 || length > 32768) {
            error = QObject::tr("Invalid filename in the extension package.");
            return false;
        }
        QVector<UInt16> buffer(int(length), UInt16(0));
        SzArEx_GetFileNameUtf16(&directory, i, buffer.data());
        QString name = QString::fromUtf16(buffer.constData(), int(length - 1));
        name.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (SzArEx_IsDir(&directory, i) && name.endsWith(QLatin1Char('/')))
            name.chop(1);
        const UInt32 attributes = SzBitWithVals_Check(&directory.Attribs, i) ? directory.Attribs.Vals[i] : 0;
        if (!SevenZipArchive::validPath(name) || names.contains(name.toCaseFolded()) || (attributes & 0x400)
            || ((attributes >> 16) & 0170000) == 0120000) {
            error = QObject::tr("Invalid, duplicate or linked path in the extension package: %1").arg(name);
            return false;
        }
        names.insert(name.toCaseFolded());
        paths.append(name);
        if (!SzArEx_IsDir(&directory, i) && name.endsWith(QStringLiteral(".extension.gmx"), Qt::CaseInsensitive)) {
            const int depth = name.count(QLatin1Char('/'));
            if (depth < resourceDepth) {
                resourceDepth = depth;
                resources.clear();
            }
            if (depth == resourceDepth)
                resources.append(name);
        }
    }
    if (resources.size() != 1) {
        error = QObject::tr("A GMEZ package must contain one extension definition.");
        return false;
    }
    const QDir destination(m_directory.path());
    for (UInt32 i = 0; i < directory.NumFiles; ++i) {
        const QString target = destination.filePath(paths.at(int(i)));
        const bool isDirectory = SzArEx_IsDir(&directory, i);
        if (!QDir().mkpath(isDirectory ? target : QFileInfo(target).absolutePath())) {
            error = QObject::tr("Cannot create the extension directory: %1").arg(target);
            return false;
        }
        if (isDirectory)
            continue;
        const char *data = nullptr;
        size_t size = 0;
        const SRes extracted = archive.extract(i, data, size);
        if (extracted != SZ_OK) {
            error = paths.at(int(i)) + QStringLiteral(": ") + archiveError(extracted);
            return false;
        }
        QFile output(target);
        if (!output.open(QIODevice::WriteOnly) || output.write(data, qint64(size)) != qint64(size) || !output.flush()) {
            error = QObject::tr("Cannot unpack %1:\n%2").arg(paths.at(int(i)), output.errorString());
            return false;
        }
    }
    const QString resource = destination.filePath(resources.first());
    QFile definition(resource);
    QDomDocument xml;
    if (!definition.open(QIODevice::ReadOnly) || !xml.setContent(&definition, false, &error)
        || xml.documentElement().tagName() != QStringLiteral("extension")) {
        error = QObject::tr("Invalid extension definition: %1\n%2").arg(resources.first(), error);
        return false;
    }
    QString name = QFileInfo(resource).fileName();
    name.chop(QStringLiteral(".extension.gmx").size());
    const QDir content(QFileInfo(resource).absoluteDir().filePath(name));
    const auto validateFile = [&](QString reference, bool required) {
        reference.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (!SevenZipArchive::validPath(reference)) {
            error = QObject::tr("Invalid extension file reference: %1").arg(reference);
            return false;
        }
        if (required && !QFileInfo(content.filePath(reference)).isFile()) {
            error = QObject::tr("The extension package is missing a required file: %1").arg(reference);
            return false;
        }
        return true;
    };
    for (const auto &entry :
        ActionXml::elements(xml.documentElement().firstChildElement(QStringLiteral("files")), QStringLiteral("file"))) {
        const bool placeholder = ActionXml::text(entry, QStringLiteral("kind")).toInt() == 4;
        if (!validateFile(ActionXml::text(entry, QStringLiteral("filename")), !placeholder))
            return false;
        for (const auto &proxy :
            ActionXml::elements(entry.firstChildElement(QStringLiteral("ProxyFiles")), QStringLiteral("ProxyFile")))
            if (!validateFile(ActionXml::text(proxy, QStringLiteral("Name")), true))
                return false;
    }
    m_resourcePath = resource;
    return true;
}
