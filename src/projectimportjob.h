#ifndef QTGMS_PROJECTIMPORTJOB_H
#define QTGMS_PROJECTIMPORTJOB_H

#include "project.h"
#include <QThread>

class ProjectImportJob : public QThread
{
    Q_OBJECT
public:
    ProjectImportJob(const QString &path, const QByteArray &legacyTextEncoding)
        : m_path(path), m_legacyTextEncoding(legacyTextEncoding) {}
    const Project &project() const { return m_project; }
    QString error() const { return m_error; }
    bool wasCancelled() const { return m_cancelled; }
    static bool supportsFile(const QString &path);

signals:
    void progressChanged(const QString &name, int percent);

protected:
    void run() override;

private:
    void importFiles();
    void importLegacyProject();
    static QString archiveError(int status);
    QString m_path;
    QByteArray m_legacyTextEncoding;
    Project m_project;
    QString m_error;
    bool m_cancelled = false;
};

#endif
