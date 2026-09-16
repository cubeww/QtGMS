#ifndef QTGMS_DATAWRITER_H
#define QTGMS_DATAWRITER_H

#include <QByteArray>
#include <QHash>
#include <QStringList>
#include <QVector>
#include <functional>

struct CompileError
{
    QString message;
    explicit CompileError(const QString &text)
        : message(text)
    {
    }
};

// VM16 uses little-endian scalars and absolute file offsets, except CODE's
// bytecode pointer, which is relative to its own field.
class DataWriter
{
public:
    QByteArray bytes;
    int position() const { return bytes.size(); }
    void u8(quint8 value);
    void u16(qint64 value);
    void u32(qint64 value);
    void u64(quint64 value);
    void f32(float value);
    void f64(double value);
    void patch(int offset, quint32 value);
    quint32 at(int offset) const;
    void align(int boundary);
    int reserve();
    int stringId(const QString &text);
    void string(const QString &text);
    void chunk(const char *tag, const std::function<void()> &write, int alignment = 16);
    void list(int count, const std::function<void(int)> &write);
    void strings();
    void finish();

private:
    QStringList m_strings;
    QHash<QString, int> m_stringIds;
    QVector<QVector<int>> m_stringPatches;
};
#endif
