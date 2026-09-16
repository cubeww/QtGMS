#include "audiopreview.h"
#include "backend.h"
#include <QTimer>
#include <atomic>
#include <cstring>

struct AudioPlaybackData {
    QByteArray audio;
    ma_decoder decoder;
    ma_device device;
    std::atomic<bool> finished{false};
    ma_uint64 silentFrames = 0;
};

static void readAudioFrames(ma_device *device, void *output, const void *, ma_uint32 frames)
{
    auto *data = static_cast<AudioPlaybackData *>(device->pUserData);
    ma_uint64 read = 0;
    ma_decoder_read_pcm_frames(&data->decoder, output, frames, &read);
    if (read < frames) {
        std::memset(static_cast<float *>(output) + read * 2, 0, (frames - read) * 2 * sizeof(float));
        // Drain queued WinMM buffers before the UI thread closes the device.
        if (read == 0) data->silentFrames += frames;
        if (data->silentFrames >= device->sampleRate / 5) data->finished.store(true);
    }
}

AudioPreview::AudioPreview(QObject *parent) : QObject(parent), m_timer(new QTimer(this))
{
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (m_playback && m_playback->finished.load()) stop();
    });
}
AudioPreview::~AudioPreview() { stop(); }
bool AudioPreview::canDecode(const QByteArray &audio, QString &error)
{
    ma_decoder decoder;
    const ma_result result = ma_decoder_init_memory(audio.constData(), audio.size(), nullptr, &decoder);
    if (result != MA_SUCCESS) { error = tr("Cannot decode audio: %1").arg(QString::fromLatin1(ma_result_description(result))); return false; }
    ma_decoder_uninit(&decoder);
    return true;
}
bool AudioPreview::play(const QByteArray &audio, double volume, QString &error)
{
    stop();
    auto *data = new AudioPlaybackData;
    data->audio = audio;
    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 2, 44100);
    ma_result result = ma_decoder_init_memory(data->audio.constData(), data->audio.size(), &decoderConfig, &data->decoder);
    if (result != MA_SUCCESS) { delete data; error = tr("Cannot decode audio: %1").arg(QString::fromLatin1(ma_result_description(result))); return false; }
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32; config.playback.channels = 2; config.sampleRate = 44100;
    config.dataCallback = readAudioFrames; config.pUserData = data;
    config.periodSizeInMilliseconds = 20; config.periods = 3;
    result = ma_device_init(nullptr, &config, &data->device);
    if (result != MA_SUCCESS) {
        ma_decoder_uninit(&data->decoder); delete data;
        error = tr("Cannot open the audio output device: %1").arg(QString::fromLatin1(ma_result_description(result))); return false;
    }
    ma_device_set_master_volume(&data->device, float(qBound(0.0, volume, 1.0)));
    result = ma_device_start(&data->device);
    if (result != MA_SUCCESS) {
        ma_device_uninit(&data->device); ma_decoder_uninit(&data->decoder); delete data;
        error = tr("Cannot start audio playback: %1").arg(QString::fromLatin1(ma_result_description(result))); return false;
    }
    m_playback = data;
    m_timer->start();
    emit playingChanged(true);
    return true;
}
void AudioPreview::stop()
{
    m_timer->stop();
    if (!m_playback) return;
    ma_device_uninit(&m_playback->device);
    ma_decoder_uninit(&m_playback->decoder);
    delete m_playback; m_playback = nullptr;
    emit playingChanged(false);
}
void AudioPreview::setVolume(double volume)
{
    if (m_playback) ma_device_set_master_volume(&m_playback->device, float(qBound(0.0, volume, 1.0)));
}
