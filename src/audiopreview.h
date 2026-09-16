#ifndef QTGMS_AUDIOPREVIEW_H
#define QTGMS_AUDIOPREVIEW_H
#include <QByteArray>
#include <QObject>

class QTimer;
struct AudioPlaybackData;

class AudioPreview : public QObject
{
    Q_OBJECT
public:
    explicit AudioPreview(QObject *parent = nullptr);
    ~AudioPreview() override;
    static bool canDecode(const QByteArray &audio, QString &error);
    bool play(const QByteArray &audio, double volume, QString &error);
    void stop();
    void setVolume(double volume);
    bool isPlaying() const { return m_playback != nullptr; }
signals:
    void playingChanged(bool playing);
private:
    AudioPlaybackData *m_playback = nullptr;
    QTimer *m_timer;
};
#endif
