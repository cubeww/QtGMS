#ifndef QTGMS_COMPILEJOB_H
#define QTGMS_COMPILEJOB_H
#include "projectcompiler.h"
#include <QThread>

class CompileJob : public QThread
{
    Q_OBJECT
public:
    explicit CompileJob(const CompileRequest &request, QObject *parent = nullptr)
        : QThread(parent)
        , m_request(request)
    {
    }
    CompileResult result() const { return m_result; }
signals:
    void progress(const QString &message);
    void progressChanged(const QString &message, int completed, int total);

protected:
    void run() override;

private:
    CompileRequest m_request;
    CompileResult m_result;
};
#endif
