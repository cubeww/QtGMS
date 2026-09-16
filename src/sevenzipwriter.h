#ifndef QTGMS_SEVENZIPWRITER_H
#define QTGMS_SEVENZIPWRITER_H

#include <QCoreApplication>
#include <QStringList>
#include <functional>

class SevenZipWriter
{
    Q_DECLARE_TR_FUNCTIONS(SevenZipWriter)
public:
    // Names are relative to directory. Returning false from progress cancels
    // the operation; QSaveFile preserves any existing destination archive.
    static bool write(const QString &destination, const QString &directory,
                      const QStringList &names, QString &error,
                      const std::function<bool(const QString &, int)> &progress);
};

#endif
