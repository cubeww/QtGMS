#ifndef QTGMS_AUDIOCOMPILER_H
#define QTGMS_AUDIOCOMPILER_H
#include <QByteArray>
enum class AudioEncoding { Wave, Vorbis, Mp3 };
QByteArray compileAudio(
    const QByteArray &source, AudioEncoding encoding, int channels, int sampleRate, int bitRate, int bitDepth);
#endif
