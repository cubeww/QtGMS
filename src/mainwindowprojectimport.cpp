#include "mainwindow.h"
#include "projectimportjob.h"
#include "projectarchivedialog.h"
#include "editorstandarddialogs.h"
#include "compilepanel.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QStandardPaths>

void MainWindow::importProject()
{
    if (m_treeBusy || m_building) return;
    const QString directory = m_project.isOpen() && !m_project.isTemporary()
        ? QFileInfo(m_project.filePath()).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Project"), directory,
        tr("GameMaker Compressed Projects (*.gmz)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".gmz"), Qt::CaseInsensitive)) {
        EditorMessageBox::warning(this, tr("Cannot Import Project"), tr("Only GMZ projects can currently be imported."));
        return;
    }
    Project imported;
    {
        QScopedValueRollback<bool> busy(m_treeBusy, true);
        ProjectImportJob job(path);
        ProjectArchiveDialog dialog(&job, tr("Import Project"), tr("Reading GMZ package..."), this);
        connect(&job, &ProjectImportJob::progressChanged, &dialog, &ProjectArchiveDialog::setProgress);
        job.start();
        dialog.exec();
        job.wait();
        if (job.wasCancelled()) return;
        if (!job.project().isOpen()) {
            EditorMessageBox::critical(this, tr("Cannot Import Project"), job.error());
            return;
        }
        imported = job.project();
    }
    // Do not close the current project until the package is fully loaded.
    // The imported Project keeps its temporary directory alive across this copy.
    if (!prepareToCloseProject()) return;
    m_project = imported;
    updateProject();
    QStringList messages;
    messages.append(tr("Imported temporary project: %1").arg(QDir::toNativeSeparators(path)));
    messages.append(tr("Use Save As to choose a permanent location for this project."));
    messages.append(tr("Indexed %1 resources and %2 configurations.")
        .arg(m_project.resourceCount()).arg(m_project.configurations().size()));
    messages.append(m_project.warnings());
    m_compilePanel->setMessages(messages);
    m_compilePanel->show();
}
