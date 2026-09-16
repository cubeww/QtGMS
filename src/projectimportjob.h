#ifndef QTGMS_PROJECTIMPORTJOB_H
#define QTGMS_PROJECTIMPORTJOB_H

#include "project.h"
#include <QThread>

class ProjectImportJob : public QThread
{
    Q_OBJECT
public:
    explicit ProjectImportJob(const QString &path) : m_path(path) {}
    const Project &project() const { return m_project; }
    QString error() const { return m_error; }
    bool wasCancelled() const { return m_cancelled; }

signals:
    void progressChanged(const QString &name, int percent);

protected:
    void run() override;

private:
    void importFiles();
    static QString archiveError(int status);
    QString m_path;
    Project m_project;
    QString m_error;
    bool m_cancelled = false;
};

#endif
