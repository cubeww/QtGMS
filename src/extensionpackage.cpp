#include "extensionpackage.h"
#include "actionxml.h"
#include "extensiondocument.h"

#include <7z.h>
#include <7zCrc.h>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegExp>
#include <QSet>
#include <QVector>
#include <cstdlib>
#include <limits>

static const size_t MaxDecoderAllocation = 512 * 1024 * 1024;

static void *allocateArchiveMemory(ISzAllocPtr, size_t size)
{
    return size <= MaxDecoderAllocation ? std::malloc(size) : nullptr;
}

static void freeArchiveMemory(ISzAllocPtr, void *memory)
{
    std::free(memory);
}

static const ISzAlloc ArchiveAllocator = { allocateArchiveMemory, freeArchiveMemory };

struct ArchiveInput
{
    ISeekInStream stream;
    QFile *file;
};

static SRes readArchive(ISeekInStreamPtr stream, void *buffer, size_t *size)
{
    const auto *input = reinterpret_cast<const ArchiveInput *>(stream);
    const qint64 read = input->file->read(static_cast<char *>(buffer), qint64(*size));
    *size = read < 0 ? 0 : size_t(read);
    return read < 0 ? SZ_ERROR_READ : SZ_OK;
}

static SRes seekArchive(ISeekInStreamPtr stream, Int64 *position, ESzSeek origin)
{
    const auto *input = reinterpret_cast<const ArchiveInput *>(stream);
    const qint64 base = origin == SZ_SEEK_CUR ? input->file->pos() : origin == SZ_SEEK_END ? input->file->size() : 0;
    if ((*position < 0 && *position < -base)
        || (*position >= 0 && *position > std::numeric_limits<qint64>::max() - base)
        || !input->file->seek(base + *position))
        return SZ_ERROR_READ;
    *position = input->file->pos();
    return SZ_OK;
}

class SevenZipArchive
{
public:
    explicit SevenZipArchive(QFile &file)
        : m_input { { readArchive, seekArchive }, &file }
        , m_readBuffer(64 * 1024, '\0')
    {
        static const bool crcInitialized = [] {
            CrcGenerateTable();
            return true;
        }();
        Q_UNUSED(crcInitialized);
        SzArEx_Init(&m_archive);
        LookToRead2_CreateVTable(&m_stream, false);
        m_stream.realStream = &m_input.stream;
        m_stream.buf = reinterpret_cast<Byte *>(m_readBuffer.data());
        m_stream.bufSize = size_t(m_readBuffer.size());
        LookToRead2_INIT(&m_stream);
    }

    ~SevenZipArchive()
    {
        ISzAlloc_Free(&ArchiveAllocator, m_output);
        SzArEx_Free(&m_archive, &ArchiveAllocator);
    }

    SRes open() { return SzArEx_Open(&m_archive, &m_stream.vt, &ArchiveAllocator, &ArchiveAllocator); }
    const CSzArEx &directory() const { return m_archive; }
    SRes extract(UInt32 index, const char *&data, size_t &size)
    {
        size_t offset = 0;
        const SRes result = SzArEx_Extract(&m_archive, &m_stream.vt, index, &m_blockIndex, &m_output, &m_outputSize,
            &offset, &size, &ArchiveAllocator, &ArchiveAllocator);
        data = m_output ? reinterpret_cast<const char *>(m_output + offset) : "";
        return result;
    }

private:
    ArchiveInput m_input;
    QByteArray m_readBuffer;
    CLookToRead2 m_stream;
    CSzArEx m_archive;
    UInt32 m_blockIndex = UInt32(-1);
    Byte *m_output = nullptr;
    size_t m_outputSize = 0;
};

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

static bool validArchivePath(const QString &path)
{
    if (!ExtensionDocument::validFileName(path))
        return false;
    for (const QChar character : path)
        if (character.unicode() < 32)
            return false;
    static const QRegExp reserved(QStringLiteral("(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?"), Qt::CaseInsensitive);
    for (const QString &part : path.split(QLatin1Char('/')))
        if (part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' ')) || reserved.exactMatch(part))
            return false;
    return true;
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
        if (!validArchivePath(name) || names.contains(name.toCaseFolded()) || (attributes & 0x400)
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
        if (!validArchivePath(reference)) {
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
