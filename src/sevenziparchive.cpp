#include "sevenziparchive.h"
#include <7zCrc.h>
#include <QDir>
#include <QFile>
#include <QRegExp>
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
    std::function<bool()> cancelled;
};

static SRes readArchive(ISeekInStreamPtr stream, void *buffer, size_t *size)
{
    const auto *input = reinterpret_cast<const ArchiveInput *>(stream);
    if (input->cancelled && input->cancelled()) { *size = 0; return SZ_ERROR_PROGRESS; }
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

class SevenZipArchive::Data
{
public:
    Data(QFile &file, const std::function<bool()> &cancelled)
        : m_input { { readArchive, seekArchive }, &file, cancelled }
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

    ~Data()
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

SevenZipArchive::SevenZipArchive(QFile &file, const std::function<bool()> &cancelled)
    : m_data(new Data(file, cancelled)) {}
SevenZipArchive::~SevenZipArchive() = default;
SRes SevenZipArchive::open() { return m_data->open(); }
const CSzArEx &SevenZipArchive::directory() const { return m_data->directory(); }
SRes SevenZipArchive::extract(UInt32 index, const char *&data, size_t &size)
{ return m_data->extract(index, data, size); }

bool SevenZipArchive::validPath(const QString &path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(QLatin1Char(':'))
        || path.contains(QRegExp(QStringLiteral("[<>\\\"|?*]")))) return false;
    for (const QString &part : path.split(QLatin1Char('/')))
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..")) return false;
    for (const QChar character : path)
        if (character.unicode() < 32)
            return false;
    static const QRegExp reserved(QStringLiteral("(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?"), Qt::CaseInsensitive);
    for (const QString &part : path.split(QLatin1Char('/')))
        if (part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' ')) || reserved.exactMatch(part))
            return false;
    return true;
}
