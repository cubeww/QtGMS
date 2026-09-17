#include "mainwindow.h"
#include "projectimportjob.h"
#include "projectarchivedialog.h"
#include "editorstandarddialogs.h"
#include "compilepanel.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QSettings>
#include <QStandardPaths>
#include <QTextCodec>

void MainWindow::importProject()
{
    if (m_treeBusy || m_building) return;
    const QString directory = m_project.isOpen() && !m_project.isTemporary()
        ? QFileInfo(m_project.filePath()).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Project"), directory,
        tr("GameMaker Projects (*.gmz *.gmk *.gm81 *.gm82);;GameMaker Compressed Projects (*.gmz);;GameMaker 7/8 Projects (*.gmk);;GameMaker 8.1 Projects (*.gm81);;GameMaker 8.2 Projects (*.gm82)"));
    if (!path.isEmpty()) importProjectFile(path);
}

void MainWindow::importProjectFile(const QString &path)
{
    if (m_treeBusy || m_building) return;
    if (!ProjectImportJob::supportsFile(path)) {
        EditorMessageBox::warning(this, tr("Cannot Import Project"), tr("Choose a GMZ, GMK, GM81 or GM82 project."));
        return;
    }
    Project imported;
    {
        QScopedValueRollback<bool> busy(m_treeBusy, true);
        QByteArray legacyTextEncoding;
        if (path.endsWith(QStringLiteral(".gmk"), Qt::CaseInsensitive)) {
            const QStringList labels = {
                tr("System default (%1)").arg(QString::fromLatin1(QTextCodec::codecForLocale()->name())),
                tr("Japanese (Shift-JIS / CP932)"),
                tr("Simplified Chinese (GBK / CP936)"),
                tr("Traditional Chinese (Big5 / CP950)"),
                tr("Korean (CP949)"),
                tr("Western European (Windows-1252)"),
                tr("Cyrillic (Windows-1251)"),
                QStringLiteral("UTF-8")
            };
            const QList<QByteArray> encodings = {QByteArray(), QByteArray("Shift-JIS"), QByteArray("GBK"),
                QByteArray("Big5"), QByteArray("CP949"), QByteArray("Windows-1252"), QByteArray("Windows-1251"), QByteArray("UTF-8")};
            QSettings settings;
            const QString key = QStringLiteral("projects/legacyTextEncoding");
            const int selected = qMax(0, encodings.indexOf(settings.value(key).toByteArray()));
            EditorInputDialog encodingDialog(this);
            encodingDialog.setWindowTitle(tr("Import Project"));
            encodingDialog.setLabelText(tr("Text encoding for this GMK project:\nChoose the encoding used when the project was created."));
            encodingDialog.setComboBoxItems(labels);
            encodingDialog.setComboBoxEditable(false);
            encodingDialog.setTextValue(labels.at(selected));
            if (encodingDialog.exec() != QDialog::Accepted) return;
            legacyTextEncoding = encodings.at(labels.indexOf(encodingDialog.textValue()));
            settings.setValue(key, legacyTextEncoding);
        }
        ProjectImportJob job(path, legacyTextEncoding);
        ProjectArchiveDialog dialog(&job, tr("Import Project"), tr("Reading project..."), this);
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
