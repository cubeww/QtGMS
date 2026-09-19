#include "datawriter.h"
#include <QIODevice>
#include <QtEndian>
#include <cstring>
#include <limits>

DataWriter::DataWriter(QIODevice &device)
    : m_device(&device)
{
    if (!device.isReadable() || !device.isWritable() || device.isSequential()
        || device.pos() != 0 || device.size() != 0)
        throw CompileError(QStringLiteral("Binary output requires an empty, seekable read/write file."));
}

int DataWriter::position() const
{
    return m_device ? int(m_device->pos()) : m_bytes.size();
}

void DataWriter::append(const char *data, int size)
{
    // Relocations throughout the VM16 writer currently use signed 32-bit
    // offsets. Reject overflow independently of the memory/disk backend.
    if (size < 0 || size > std::numeric_limits<int>::max() - position())
        throw CompileError(QStringLiteral("Compiled binary exceeds the 2 GiB file offset limit."));
    if (m_device) {
        if (m_device->write(data, size) != size)
            throw CompileError(QStringLiteral("Cannot write compiled data: %1").arg(m_device->errorString()));
    } else {
        m_bytes.append(data, size);
    }
}

static void seekOutput(QIODevice &device, int offset)
{
    if (!device.seek(offset))
        throw CompileError(QStringLiteral("Cannot seek compiled data: %1").arg(device.errorString()));
}

void DataWriter::u8(quint8 value)
{
    const char data = char(value);
    append(&data, 1);
}
void DataWriter::u16(qint64 value)
{
    uchar data[2];
    qToLittleEndian(quint16(value), data);
    append(reinterpret_cast<char *>(data), 2);
}
void DataWriter::u32(qint64 value)
{
    uchar data[4];
    qToLittleEndian(quint32(value), data);
    append(reinterpret_cast<char *>(data), 4);
}
void DataWriter::u64(quint64 value)
{
    uchar data[8];
    qToLittleEndian(value, data);
    append(reinterpret_cast<char *>(data), 8);
}
void DataWriter::f32(float value)
{
    quint32 bits;
    std::memcpy(&bits, &value, 4);
    u32(bits);
}
void DataWriter::f64(double value)
{
    quint64 bits;
    std::memcpy(&bits, &value, 8);
    u64(bits);
}
void DataWriter::patch(int offset, quint32 value)
{
    const int end = position();
    if (offset < 0 || offset > end - 4)
        throw CompileError(QStringLiteral("Invalid binary relocation."));
    if (m_device) {
        seekOutput(*m_device, offset);
        u32(value);
        seekOutput(*m_device, end);
    } else {
        qToLittleEndian(value, reinterpret_cast<uchar *>(m_bytes.data() + offset));
    }
}
quint32 DataWriter::at(int offset) const
{
    const int end = position();
    if (offset < 0 || offset > end - 4)
        throw CompileError(QStringLiteral("Invalid binary address."));
    if (!m_device)
        return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(m_bytes.constData() + offset));
    uchar data[4];
    seekOutput(*m_device, offset);
    if (m_device->read(reinterpret_cast<char *>(data), 4) != 4)
        throw CompileError(QStringLiteral("Cannot read compiled data: %1").arg(m_device->errorString()));
    seekOutput(*m_device, end);
    return qFromLittleEndian<quint32>(data);
}
void DataWriter::align(int boundary)
{
    while (position() % boundary)
        u8(0);
}
int DataWriter::reserve()
{
    const int offset = position();
    u32(0);
    return offset;
}
int DataWriter::stringId(const QString &text)
{
    auto found = m_stringIds.constFind(text);
    if (found != m_stringIds.cend())
        return found.value();
    const int index = m_strings.size();
    m_strings.append(text);
    m_stringIds.insert(text, index);
    m_stringPatches.append(QVector<int>());
    return index;
}
void DataWriter::string(const QString &text)
{
    const int id = stringId(text);
    m_stringPatches[id].append(reserve());
}
void DataWriter::chunk(const char *tag, const std::function<void()> &write, int alignment)
{
    append(tag, 4);
    const int size = reserve();
    write();
    align(alignment);
    patch(size, position() - size - 4);
}
void DataWriter::list(int count, const std::function<void(int)> &write)
{
    u32(count);
    const int table = position();
    for (int i = 0; i < count; ++i)
        u32(0);
    for (int i = 0; i < count; ++i) {
        patch(table + i * 4, position());
        write(i);
    }
}
void DataWriter::strings()
{
    chunk(
        "STRG",
        [this] {
            list(m_strings.size(), [this](int index) {
                const QByteArray utf8 = m_strings.at(index).toUtf8();
                u32(utf8.size());
                for (int patchOffset : m_stringPatches.at(index))
                    patch(patchOffset, position());
                append(utf8);
                u8(0);
            });
        },
        128);
    m_strings.clear();
    m_stringIds.clear();
    m_stringPatches.clear();
}
void DataWriter::finish()
{
    patch(4, position() - 8);
}
