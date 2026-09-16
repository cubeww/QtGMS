#ifndef QTGMS_BUILDDIRECTORY_H
#define QTGMS_BUILDDIRECTORY_H
#include "project.h"

class BuildDirectory
{
public:
    static QString path(const Project &project);
    static bool validate(const Project &project, QString &error);
    static bool clean(const Project &project, QString &error);
};
#endif
