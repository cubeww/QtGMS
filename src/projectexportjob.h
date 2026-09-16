#ifndef QTGMS_PROJECTEXPORTJOB_H
#define QTGMS_PROJECTEXPORTJOB_H

#include "project.h"
#include <QThread>

class ProjectExportJob : public QThread
{
    Q_OBJECT
public:
    ProjectExportJob(const Project &project, const QString &destination)
        : m_project(project), m_destination(destination) {}
    bool succeeded() const { return m_succeeded; }
    bool wasCancelled() const { return m_cancelled; }
    QString error() const { return m_error; }

signals:
    void progressChanged(const QString &name, int percent);

protected:
    void run() override;

private:
    void exportFiles();
    Project m_project;
    QString m_destination;
    QString m_error;
    bool m_succeeded = false;
    bool m_cancelled = false;
};

#endif
