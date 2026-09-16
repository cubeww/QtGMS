#ifndef QTGMS_SEVENZIPARCHIVE_H
#define QTGMS_SEVENZIPARCHIVE_H

#include <7z.h>
#include <QString>
#include <functional>
#include <memory>

class QFile;

class SevenZipArchive
{
public:
    explicit SevenZipArchive(QFile &file, const std::function<bool()> &cancelled = std::function<bool()>());
    ~SevenZipArchive();
    SRes open();
    const CSzArEx &directory() const;
    SRes extract(UInt32 index, const char *&data, size_t &size);
    static bool validPath(const QString &path);

private:
    class Data;
    std::unique_ptr<Data> m_data;
};

#endif
