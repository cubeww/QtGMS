#include "mainwindow.h"
#include "compiler/compilejob.h"
#include "compiler/builddirectory.h"
#include "compilepanel.h"
#include "gamerunner.h"
#include "editordialog.h"
#include "editorstandarddialogs.h"
#include "editortheme.h"
#include <QCloseEvent>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QProgressBar>
#include <QScopedValueRollback>
#include <QVBoxLayout>

class CompilerProgressDialog : public EditorDialog
{
    Q_OBJECT
public:
    CompilerProgressDialog(CompileJob *job, const QString &projectName, QWidget *parent)
        : EditorDialog(parent)
        , m_job(job)
    {
        setWindowTitle(tr("%1 - Compiling").arg(projectName));
        auto *layout = new QVBoxLayout(bodyWidget());
        layout->setContentsMargins(10, 6, 10, 8);
        layout->setSpacing(4);
        m_label = new QLabel(tr("Starting compiler..."));
        m_label->setTextFormat(Qt::PlainText);
        m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(m_label);
        m_progressBar = new QProgressBar;
        m_progressBar->setRange(0, 1);
        m_progressBar->setValue(0);
        EditorTheme::applyProgressDialogStyle(bodyWidget(), m_progressBar);
        layout->addWidget(m_progressBar);
        setFixedSize(460, 32 + 14 + m_label->sizeHint().height() + 4 + 24);
        connect(job, &CompileJob::progressChanged, this, [this](const QString &message, int completed, int total) {
            if (m_job->isInterruptionRequested())
                return;
            m_label->setText(m_label->fontMetrics().elidedText(message, Qt::ElideRight, m_label->width()));
            m_label->setToolTip(message);
            m_progressBar->setRange(0, total);
            m_progressBar->setValue(completed);
        });
        connect(job, &QThread::finished, this, &QDialog::accept);
    }
    void reject() override
    {
        m_job->requestInterruption();
        m_label->setText(tr("Cancelling..."));
        m_label->setToolTip(tr("Cancelling after the current compiler operation..."));
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        reject();
        event->ignore();
    }

private:
    CompileJob *m_job;
    QLabel *m_label;
    QProgressBar *m_progressBar;
};

bool MainWindow::compileProject()
{
    if (!m_project.isOpen() || m_treeBusy || m_building)
        return false;
    // Temporary projects already have backing files; flush edits without Save As.
    if (!saveResources())
        return false;
    if (!stopGame())
        return false;
    const QString config = m_configurationCombo->currentText();
    CompileResult result;
    {
        QScopedValueRollback<bool> busy(m_treeBusy, true);
        QScopedValueRollback<bool> building(m_building, true);
        updateBuildActions();
        CompileRequest request;
        request.project = m_project;
        request.configuration = m_configurationCombo->currentIndex();
        CompileJob job(request);
        CompilerProgressDialog dialog(&job, QFileInfo(m_project.filePath()).baseName(), this);
        m_compilePanel->setMessages({ tr("Compiling configuration: %1").arg(config),
            tr("Output: %1").arg(QDir::toNativeSeparators(BuildDirectory::path(m_project))) });
        m_compilePanel->show();
        connect(&job, &CompileJob::progress, m_compilePanel, &CompilePanel::appendMessage);
        job.start();
        dialog.exec();
        job.wait();
        result = job.result();
    }
    updateBuildActions();
    if (result.success) {
        m_compilePanel->appendMessage(
            tr("Build completed in %1 ms. Wrote %2 files.").arg(result.elapsedMilliseconds).arg(result.files.size()));
        m_compilePanel->appendMessage(result.files.first());
    } else {
        m_compilePanel->appendMessage(tr("ERROR: %1").arg(result.error));
        EditorMessageBox::warning(this, tr("Compilation Failed"), result.error);
    }
    return result.success;
}

void MainWindow::runProject()
{
    if (!compileProject())
        return;
    QString error;
    if (!m_gameRunner->start(BuildDirectory::executablePath(m_project), error)) {
        m_compilePanel->appendMessage(tr("ERROR: %1").arg(error));
        EditorMessageBox::warning(this, tr("Run Failed"), error);
    }
    updateBuildActions();
}

bool MainWindow::stopGame()
{
    if (m_gameRunner->stop())
        return true;
    EditorMessageBox::warning(this, tr("Stop Failed"), tr("The game process could not be stopped."));
    return false;
}

void MainWindow::cleanBuild()
{
    if (!m_project.isOpen() || m_treeBusy || m_building || !stopGame())
        return;
    QString error;
    m_compilePanel->show();
    if (!BuildDirectory::clean(m_project, error)) {
        m_compilePanel->appendMessage(tr("ERROR: %1").arg(error));
        EditorMessageBox::warning(this, tr("Clean Failed"), error);
        return;
    }
    m_compilePanel->appendMessage(tr("Cleaned: %1").arg(QDir::toNativeSeparators(BuildDirectory::path(m_project))));
}

void MainWindow::updateBuildActions()
{
    const bool available = m_project.isOpen() && !m_building;
    m_buildAction->setEnabled(available);
    m_runAction->setEnabled(available);
    m_cleanAction->setEnabled(available);
    m_stopAction->setEnabled(m_gameRunner->isRunning() && !m_building);
}

#include "mainwindowcompile.moc"
