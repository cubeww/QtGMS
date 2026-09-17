#ifndef QTGMS_LEGACYPROJECTREADER_H
#define QTGMS_LEGACYPROJECTREADER_H

#include <QByteArray>
#include <QFile>
#include <QString>
#include <functional>
#include <initializer_list>

class QTextCodec;

class LegacyProjectReader
{
public:
    LegacyProjectReader(const QString &path, const std::function<bool()> &cancelled);
    int integer();
    int count(int maximum = 1000000);
    bool boolean();
    double real();
    QString string();
    QByteArray bytes(int size);
    QByteArray blob();
    QByteArray compressed();
    QByteArray decompress(QByteArray data) const;
    void skip(qint64 size);
    int version(std::initializer_list<int> versions);
    void beginBlock();
    void endBlock();
    void setEncoding(int seed);
    void setTextEncoding(const QByteArray &encoding);
    void setContext(const QString &context) { m_context = context; }
    void checkCancelled() const;
    void fail(const QString &message) const;

private:
    void read(char *destination, int size);
    QFile m_file;
    QByteArray m_block;
    int m_blockPosition = 0;
    bool m_inBlock = false;
    bool m_encoded = false;
    QTextCodec *m_textCodec = nullptr;
    unsigned char m_decode[256] = {};
    QString m_context;
    std::function<bool()> m_cancelled;
};

#endif
