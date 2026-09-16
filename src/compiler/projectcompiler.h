#ifndef QTGMS_PROJECTCOMPILER_H
#define QTGMS_PROJECTCOMPILER_H
#include "project.h"
#include "compileprofile.h"
#include <functional>

struct CompileRequest
{
    Project project;
    int configuration = 0;
};
struct CompileResult
{
    bool success = false;
    QString error;
    QStringList files;
    qint64 elapsedMilliseconds = 0;
    QVector<CompileStageTiming> timings;
};
class ProjectCompiler
{
public:
    static CompileResult compile(
        const CompileRequest &request, const std::function<void(const QString &, int, int)> &progress);
};
#endif
