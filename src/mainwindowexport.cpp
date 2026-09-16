#include "mainwindow.h"
#include "projectexportjob.h"
#include "projectarchivedialog.h"
#include "editorstandarddialogs.h"
#include "compilepanel.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QStandardPaths>

void MainWindow::exportProject()
{
    if (!m_project.isOpen() || m_treeBusy || m_building) return;
    QString name = QFileInfo(m_project.filePath()).fileName();
    name.chop(QStringLiteral(".project.gmx").size());
    const QDir directory(m_project.isTemporary()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QFileInfo(m_project.filePath()).absolutePath());
    // Add the extension before QFileDialog's overwrite confirmation.
    QFileDialog picker(this, tr("Export Project"), directory.filePath(name + QStringLiteral(".gmz")),
                       tr("GameMaker Compressed Projects (*.gmz)"));
    picker.setAcceptMode(QFileDialog::AcceptSave);
    picker.setFileMode(QFileDialog::AnyFile);
    picker.setDefaultSuffix(QStringLiteral("gmz"));
    if (picker.exec() != QDialog::Accepted || picker.selectedFiles().isEmpty()) return;
    const QString destination = picker.selectedFiles().first();
    if (!destination.endsWith(QStringLiteral(".gmz"), Qt::CaseInsensitive)) {
        EditorMessageBox::warning(this, tr("Export Project"), tr("Choose a filename ending in .gmz."));
        return;
    }
    // Includes open modeless code actions; temporary projects have backing files
    // and can be exported without first choosing a permanent GMX location.
    if (!saveResources()) return;
    QScopedValueRollback<bool> busy(m_treeBusy, true);
    ProjectExportJob job(m_project, destination);
    ProjectArchiveDialog dialog(&job, tr("Export Project"), tr("Collecting project files..."), this);
    connect(&job, &ProjectExportJob::progressChanged, &dialog, [&dialog](const QString &name, int percent) {
        dialog.setProgress(name.isEmpty() ? tr("Finishing export...") : name, percent);
    });
    job.start();
    dialog.exec();
    job.wait();
    if (job.succeeded()) {
        m_compilePanel->appendMessage(tr("Project exported to: %1").arg(QDir::toNativeSeparators(destination)));
        EditorMessageBox::information(this, tr("Export Project"),
            tr("Project exported to: %1").arg(QDir::toNativeSeparators(destination)));
    } else if (!job.wasCancelled()) {
        EditorMessageBox::warning(this, tr("Export Failed"), job.error());
    }
}
