#include "compilejob.h"
#include "datawriter.h"
void CompileJob::run()
{
    m_result = ProjectCompiler::compile(m_request, [this](const QString &message, int completed, int total) {
        if (isInterruptionRequested())
            throw CompileError(tr("Compilation cancelled."));
        emit progress(message);
        emit progressChanged(message, completed, total);
    });
    if (m_result.success)
        emit progressChanged(tr("Build completed."), 1, 1);
}
