#include "gamerunner.h"
#include "projectfiletransaction.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

GameRunner::GameRunner(QObject *parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { readOutput(); });
    connect(&m_process, &QProcess::stateChanged, this, [this] { emit runningChanged(); });
    connect(&m_process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            readOutput(true);
            emit message(m_stopping                 ? tr("Game stopped.")
                    : status == QProcess::CrashExit ? tr("Runner crashed (exit code %1).").arg(code)
                                                    : tr("Game exited (code %1).").arg(code));
        });
}

GameRunner::~GameRunner()
{
    stop();
}
bool GameRunner::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

bool GameRunner::start(const QString &directory, QString &error)
{
    if (isRunning()) {
        error = tr("A game is already running.");
        return false;
    }
    const QDir output(directory),
        runtime(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runtime/windows")));
    if (!QFileInfo::exists(output.filePath(QStringLiteral("data.win")))) {
        error = tr("Compile the project before running it.");
        return false;
    }
    ProjectFileTransaction transaction(directory);
    for (const auto &name :
        { QStringLiteral("Runner.exe"), QStringLiteral("d3dx9_43.dll"), QStringLiteral("d3dcompiler_43.dll") }) {
        QFile source(runtime.filePath(name));
        if (!source.open(QIODevice::ReadOnly)) {
            error = tr("Cannot read runtime file %1: %2").arg(source.fileName(), source.errorString());
            transaction.rollback(error);
            return false;
        }
        const QByteArray bytes = source.readAll();
        if (source.error() != QFile::NoError || !transaction.write(output.filePath(name), bytes, error)) {
            if (error.isEmpty())
                error = source.errorString();
            transaction.rollback(error);
            return false;
        }
    }
    if (!transaction.finish(error))
        return false;
    m_output.clear();
    m_stopping = false;
    m_process.setWorkingDirectory(directory);
    // Keep the legacy Runner's game argument ASCII. QProcess supplies the
    // Unicode executable path and working directory through the Windows API.
    m_process.start(output.filePath(QStringLiteral("Runner.exe")),
        { QStringLiteral("-game"), QStringLiteral("data.win") });
    if (!m_process.waitForStarted(3000)) {
        error = tr("Cannot start Runner: %1").arg(m_process.errorString());
        return false;
    }
    emit message(tr("Game started."));
    return true;
}

bool GameRunner::stop()
{
    if (!isRunning())
        return true;
    m_stopping = true;
    if (m_process.state() == QProcess::Starting && !m_process.waitForStarted(1000))
        return !isRunning();
    m_process.terminate();
    if (m_process.waitForFinished(1000))
        return true;
    m_process.kill();
    return m_process.waitForFinished(2000);
}

void GameRunner::readOutput(bool flush)
{
    m_output += m_process.readAllStandardOutput();
    int newline;
    while ((newline = m_output.indexOf('\n')) >= 0) {
        QByteArray line = m_output.left(newline);
        m_output.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        emit message(QString::fromLocal8Bit(line));
    }
    if ((flush || m_output.size() > 65536) && !m_output.isEmpty()) {
        emit message(QString::fromLocal8Bit(m_output));
        m_output.clear();
    }
}
