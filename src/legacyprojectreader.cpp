#include "legacyprojectreader.h"

#include <QObject>
#include <QTextCodec>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <utility>

static const int MaxLegacyBlockSize = 512 * 1024 * 1024;

LegacyProjectReader::LegacyProjectReader(const QString &path, const std::function<bool()> &cancelled)
    : m_file(path), m_textCodec(QTextCodec::codecForLocale()), m_cancelled(cancelled)
{
    if (!m_file.open(QIODevice::ReadOnly)) fail(m_file.errorString());
}

void LegacyProjectReader::checkCancelled() const
{
    if (m_cancelled && m_cancelled()) throw QString();
}

void LegacyProjectReader::fail(const QString &message) const
{
    throw QObject::tr("%1 (file offset %2%3): %4").arg(m_context).arg(m_file.pos())
        .arg(m_inBlock ? QObject::tr(", block offset %1").arg(m_blockPosition) : QString()).arg(message);
}

void LegacyProjectReader::read(char *destination, int size)
{
    checkCancelled();
    if (size < 0 || size > MaxLegacyBlockSize) fail(QObject::tr("Invalid or excessively large data block."));
    if (m_inBlock) {
        if (size > m_block.size() - m_blockPosition) fail(QObject::tr("Unexpected end of a resource block."));
        std::memcpy(destination, m_block.constData() + m_blockPosition, size_t(size));
        m_blockPosition += size;
    } else {
        const qint64 position = m_file.pos();
        if (m_file.read(destination, size) != size) fail(QObject::tr("Unexpected end of the project file."));
        if (m_encoded) {
            for (int i = 0; i < size; ++i)
                destination[i] = char((int(m_decode[static_cast<unsigned char>(destination[i])]) - (position + i)) & 255);
        }
    }
}

QByteArray LegacyProjectReader::bytes(int size)
{
    if (size < 0 || size > MaxLegacyBlockSize
        || size > (m_inBlock ? m_block.size() - m_blockPosition : m_file.size() - m_file.pos()))
        fail(QObject::tr("Invalid data block length: %1.").arg(size));
    QByteArray result(size, Qt::Uninitialized);
    read(result.data(), size);
    return result;
}

int LegacyProjectReader::integer()
{
    unsigned char data[4];
    read(reinterpret_cast<char *>(data), 4);
    return qFromLittleEndian<qint32>(data);
}

int LegacyProjectReader::count(int maximum)
{
    const int value = integer();
    if (value < 0 || value > maximum) fail(QObject::tr("Invalid item count: %1.").arg(value));
    return value;
}

bool LegacyProjectReader::boolean()
{
    const int value = integer();
    if (value != 0 && value != 1) fail(QObject::tr("Invalid boolean value: %1.").arg(value));
    return value != 0;
}

double LegacyProjectReader::real()
{
    unsigned char data[8];
    read(reinterpret_cast<char *>(data), 8);
    const quint64 bits = qFromLittleEndian<quint64>(data);
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    if (!std::isfinite(value)) fail(QObject::tr("Invalid floating point value."));
    return value;
}

QByteArray LegacyProjectReader::blob() { return bytes(count(MaxLegacyBlockSize)); }

QString LegacyProjectReader::string()
{
    const QByteArray data = bytes(count(64 * 1024 * 1024));
    return m_textCodec->toUnicode(data);
}

void LegacyProjectReader::setTextEncoding(const QByteArray &encoding)
{
    m_textCodec = encoding.isEmpty() ? QTextCodec::codecForLocale() : QTextCodec::codecForName(encoding);
    if (!m_textCodec)
        fail(QObject::tr("Unsupported text encoding: %1").arg(QString::fromLatin1(encoding)));
}

void LegacyProjectReader::skip(qint64 size)
{
    checkCancelled();
    if (size < 0 || size > (m_inBlock ? m_block.size() - m_blockPosition : m_file.size() - m_file.pos()))
        fail(QObject::tr("Invalid data block length: %1.").arg(size));
    if (m_inBlock) m_blockPosition += int(size);
    else if (!m_file.seek(m_file.pos() + size)) fail(m_file.errorString());
}

int LegacyProjectReader::version(std::initializer_list<int> versions)
{
    const int value = integer();
    for (int expected : versions) if (value == expected) return value;
    fail(QObject::tr("Unsupported legacy format version: %1.").arg(value));
    return 0;
}

QByteArray LegacyProjectReader::compressed()
{
    return decompress(blob());
}

QByteArray LegacyProjectReader::decompress(QByteArray data) const
{
    checkCancelled();
    // GM stores a zlib stream; Qt's wrapper additionally expects a big-endian
    // allocation hint. The actual uncompressed length is not stored in GMK.
    const quint32 hint = quint32(qMin<qint64>(MaxLegacyBlockSize, qMax<qint64>(1024, qint64(data.size()) * 2)));
    char header[4];
    qToBigEndian(hint, reinterpret_cast<uchar *>(header));
    data.prepend(header, 4);
    QByteArray result = qUncompress(data);
    if (result.isNull() || result.size() > MaxLegacyBlockSize)
        fail(QObject::tr("Damaged or excessively large compressed resource."));
    checkCancelled();
    return result;
}

void LegacyProjectReader::beginBlock()
{
    if (m_inBlock) fail(QObject::tr("Unexpected nested resource block."));
    m_block = compressed();
    m_blockPosition = 0;
    m_inBlock = true;
}

void LegacyProjectReader::endBlock()
{
    m_inBlock = false;
    m_block.clear();
    m_blockPosition = 0;
}

void LegacyProjectReader::setEncoding(int seed)
{
    if (seed < 0) fail(QObject::tr("Invalid GM7 encoding seed."));
    unsigned char permutation[256];
    for (int i = 0; i < 256; ++i) permutation[i] = static_cast<unsigned char>(i);
    const int stride = 6 + seed % 250;
    const int offset = seed / 250;
    for (int i = 1; i <= 10000; ++i) {
        const int index = 1 + (i * stride + offset) % 254;
        std::swap(permutation[index], permutation[index + 1]);
    }
    for (int i = 0; i < 256; ++i) m_decode[permutation[i]] = static_cast<unsigned char>(i);
    m_encoded = true;
}
