#ifndef QTGMS_RUNNERVERSION_H
#define QTGMS_RUNNERVERSION_H

#include <QMap>
#include <QString>

class RunnerVersion
{
public:
    static bool replace(const QString &executablePath, const QMap<QString, QString> &options, QString &error);
};

#endif
