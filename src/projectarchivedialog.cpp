#include "projectarchivedialog.h"
#include "editortheme.h"
#include <QCloseEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

ProjectArchiveDialog::ProjectArchiveDialog(QThread *job, const QString &title,
                                         const QString &message, QWidget *parent)
    : EditorDialog(parent), m_job(job)
{
    setWindowTitle(title);
    auto *layout = new QVBoxLayout(bodyWidget());
    layout->setContentsMargins(10, 6, 10, 8);
    layout->setSpacing(4);
    m_label = new QLabel(message);
    m_label->setTextFormat(Qt::PlainText);
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_label);
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    EditorTheme::applyProgressDialogStyle(bodyWidget(), m_progress);
    layout->addWidget(m_progress);
    m_cancelButton = new QPushButton(tr("Cancel"));
    layout->addWidget(m_cancelButton, 0, Qt::AlignRight);
    connect(m_cancelButton, &QPushButton::clicked, this, &ProjectArchiveDialog::reject);
    connect(job, &QThread::finished, this, &QDialog::accept);
    setFixedWidth(460);
}

void ProjectArchiveDialog::setProgress(const QString &message, int percent)
{
    if (m_job->isInterruptionRequested()) return;
    m_label->setText(m_label->fontMetrics().elidedText(message, Qt::ElideMiddle, m_label->width()));
    m_label->setToolTip(message);
    m_progress->setValue(percent);
}

void ProjectArchiveDialog::reject()
{
    m_job->requestInterruption();
    m_label->setText(tr("Cancelling..."));
    m_cancelButton->setEnabled(false);
}

void ProjectArchiveDialog::closeEvent(QCloseEvent *event)
{
    reject();
    event->ignore();
}
