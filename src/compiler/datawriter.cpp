#include "datawriter.h"
#include <QtEndian>
#include <cstring>

void DataWriter::u8(quint8 value)
{
    bytes.append(char(value));
}
void DataWriter::u16(qint64 value)
{
    uchar data[2];
    qToLittleEndian(quint16(value), data);
    bytes.append(reinterpret_cast<char *>(data), 2);
}
void DataWriter::u32(qint64 value)
{
    uchar data[4];
    qToLittleEndian(quint32(value), data);
    bytes.append(reinterpret_cast<char *>(data), 4);
}
void DataWriter::u64(quint64 value)
{
    uchar data[8];
    qToLittleEndian(value, data);
    bytes.append(reinterpret_cast<char *>(data), 8);
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
    if (offset < 0 || offset > bytes.size() - 4)
        throw CompileError(QStringLiteral("Invalid binary relocation."));
    qToLittleEndian(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}
quint32 DataWriter::at(int offset) const
{
    if (offset < 0 || offset > bytes.size() - 4)
        throw CompileError(QStringLiteral("Invalid binary address."));
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData() + offset));
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
    bytes.append(tag, 4);
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
                bytes.append(utf8);
                u8(0);
            });
        },
        128);
}
void DataWriter::finish()
{
    patch(4, position() - 8);
}
