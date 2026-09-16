#ifndef QTGMS_PROJECTARCHIVEDIALOG_H
#define QTGMS_PROJECTARCHIVEDIALOG_H

#include "editordialog.h"

class QThread;
class QLabel;
class QPushButton;
class QProgressBar;

class ProjectArchiveDialog : public EditorDialog
{
    Q_OBJECT
public:
    ProjectArchiveDialog(QThread *job, const QString &title, const QString &message, QWidget *parent);
    void setProgress(const QString &message, int percent);
    void reject() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QThread *m_job;
    QLabel *m_label;
    QProgressBar *m_progress;
    QPushButton *m_cancelButton;
};

#endif
