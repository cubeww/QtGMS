#ifndef QTGMS_GAMERUNNER_H
#define QTGMS_GAMERUNNER_H
#include <QObject>
#include <QProcess>

class GameRunner : public QObject
{
    Q_OBJECT
public:
    explicit GameRunner(QObject *parent = nullptr);
    ~GameRunner() override;
    bool isRunning() const;
    bool start(const QString &executablePath, QString &error);
    bool stop();
signals:
    void runningChanged();
    void message(const QString &text);

private:
    QProcess m_process;
    QByteArray m_output;
    bool m_stopping = false;
    void readOutput(bool flush = false);
};
#endif
