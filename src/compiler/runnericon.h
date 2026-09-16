#ifndef QTGMS_RUNNERICON_H
#define QTGMS_RUNNERICON_H
#include <QString>

class RunnerIcon
{
public:
    static bool replace(const QString &executablePath, const QString &iconPath, QString &error);
};
#endif
