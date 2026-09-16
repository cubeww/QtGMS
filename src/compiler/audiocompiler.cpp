#include "audiocompiler.h"
#include "datawriter.h"
#include "backend.h"
#include <vorbis/vorbisenc.h>
#include <lame.h>
#include <QVector>

struct AudioEncoderState
{
    ma_decoder decoder;
    vorbis_info info;
    vorbis_comment comment;
    vorbis_dsp_state dsp;
    vorbis_block block;
    ogg_stream_state stream;
    lame_t mp3 = nullptr;
    bool decoded = false, analyzed = false, blocked = false, streamed = false;
    AudioEncoderState()
    {
        vorbis_info_init(&info);
        vorbis_comment_init(&comment);
    }
    ~AudioEncoderState()
    {
        if (mp3)
            lame_close(mp3);
        if (streamed)
            ogg_stream_clear(&stream);
        if (blocked)
            vorbis_block_clear(&block);
        if (analyzed)
            vorbis_dsp_clear(&dsp);
        vorbis_comment_clear(&comment);
        vorbis_info_clear(&info);
        if (decoded)
            ma_decoder_uninit(&decoder);
    }
};

static DataWriter createWaveHeader(int channels, int sampleRate, int bitDepth)
{
    DataWriter wave;
    wave.bytes.append("RIFF", 4);
    wave.u32(0);
    wave.bytes.append("WAVEfmt ", 8);
    wave.u32(16);
    wave.u16(1);
    wave.u16(channels);
    wave.u32(sampleRate);
    wave.u32(sampleRate * channels * bitDepth / 8);
    wave.u16(channels * bitDepth / 8);
    wave.u16(bitDepth);
    wave.bytes.append("data", 4);
    wave.u32(0);
    return wave;
}

static void finishWave(DataWriter &wave)
{
    wave.patch(40, wave.position() - 44);
    wave.align(2);
    wave.patch(4, wave.position() - 8);
}

QByteArray compileAudio(
    const QByteArray &source, AudioEncoding encoding, int channels, int sampleRate, int bitRate, int bitDepth)
{
    if (source.isEmpty()) {
        // Legacy Runner indexes AUDO even for empty sound resources. Supply one
        // silent PCM frame so these resources have valid, playable data.
        DataWriter silent = createWaveHeader(channels, sampleRate, 16);
        for (int channel = 0; channel < channels; ++channel)
            silent.u16(0);
        finishWave(silent);
        return compileAudio(silent.bytes, encoding, channels, sampleRate, bitRate, bitDepth);
    }
    AudioEncoderState state;
    const ma_decoder_config config
        = ma_decoder_config_init(encoding == AudioEncoding::Vorbis ? ma_format_f32 : ma_format_s16, channels, sampleRate);
    const ma_result result = ma_decoder_init_memory(source.constData(), source.size(), &config, &state.decoder);
    if (result != MA_SUCCESS)
        throw CompileError(
            QStringLiteral("Audio decoding failed: %1").arg(QString::fromLatin1(ma_result_description(result))));
    state.decoded = true;
    channels = state.decoder.outputChannels;
    sampleRate = state.decoder.outputSampleRate;
    if (channels < 1 || channels > 2 || sampleRate <= 0)
        throw CompileError(QStringLiteral("Only mono/stereo sound is supported."));
    if (encoding == AudioEncoding::Wave) {
        DataWriter wave = createWaveHeader(channels, sampleRate, bitDepth);
        QVector<ma_int16> samples(4096 * channels);
        for (;;) {
            ma_uint64 frames = 0;
            const auto status = ma_decoder_read_pcm_frames(&state.decoder, samples.data(), 4096, &frames);
            if (status != MA_SUCCESS && status != MA_AT_END)
                throw CompileError(QStringLiteral("Failed reading audio frames."));
            if (!frames)
                break;
            for (int i = 0; i < int(frames) * channels; ++i) {
                if (bitDepth == 8)
                    wave.u8((int(samples.at(i)) + 32768) >> 8);
                else
                    wave.u16(samples.at(i));
            }
        }
        finishWave(wave);
        return wave.bytes;
    }
    if (encoding == AudioEncoding::Mp3) {
        state.mp3 = lame_init();
        if (!state.mp3)
            throw CompileError(QStringLiteral("Cannot initialize the MP3 encoder."));
        // Match the legacy compiler's libmp3lame CBR path. Metadata is omitted;
        // the final LAME seek/duration tag replaces the reserved first frame.
        lame_set_write_id3tag_automatic(state.mp3, 0);
        if (lame_set_num_channels(state.mp3, channels) < 0
            || lame_set_in_samplerate(state.mp3, sampleRate) < 0
            || lame_set_out_samplerate(state.mp3, sampleRate) < 0
            || lame_set_mode(state.mp3, channels == 1 ? MONO : JOINT_STEREO) < 0
            || lame_set_VBR(state.mp3, vbr_off) < 0
            || lame_set_brate(state.mp3, bitRate) < 0
            || lame_set_quality(state.mp3, 3) < 0
            || lame_init_params(state.mp3) < 0)
            throw CompileError(QStringLiteral("MP3 does not support the selected sample rate / bitrate."));
        QVector<ma_int16> samples(4096 * channels);
        QByteArray buffer(16384, '\0'), output;
        for (;;) {
            ma_uint64 frames = 0;
            const auto status = ma_decoder_read_pcm_frames(&state.decoder, samples.data(), 4096, &frames);
            if (status != MA_SUCCESS && status != MA_AT_END)
                throw CompileError(QStringLiteral("Failed reading audio frames."));
            if (!frames)
                break;
            unsigned char *encoded = reinterpret_cast<unsigned char *>(buffer.data());
            const int count = channels == 2
                ? lame_encode_buffer_interleaved(state.mp3, samples.data(), int(frames), encoded, buffer.size())
                : lame_encode_buffer(state.mp3, samples.data(), samples.data(), int(frames), encoded, buffer.size());
            if (count < 0)
                throw CompileError(QStringLiteral("MP3 encoding failed (%1).").arg(count));
            output.append(buffer.constData(), count);
        }
        const int count = lame_encode_flush(state.mp3, reinterpret_cast<unsigned char *>(buffer.data()), buffer.size());
        if (count < 0)
            throw CompileError(QStringLiteral("Cannot finish MP3 encoding (%1).").arg(count));
        output.append(buffer.constData(), count);
        const size_t tagSize = lame_get_lametag_frame(state.mp3, reinterpret_cast<unsigned char *>(buffer.data()), buffer.size());
        if (tagSize > size_t(buffer.size()) || tagSize > size_t(output.size()))
            throw CompileError(QStringLiteral("Invalid MP3 seek tag size."));
        if (tagSize)
            output.replace(0, int(tagSize), buffer.constData(), int(tagSize));
        return output;
    }
    const float quality = qMin(bitRate / 64, 6) / 10.0f;
    if (vorbis_encode_init_vbr(&state.info, channels, sampleRate, quality) != 0)
        throw CompileError(QStringLiteral("Vorbis does not support the selected sample rate / quality."));
    if (vorbis_analysis_init(&state.dsp, &state.info) != 0)
        throw CompileError(QStringLiteral("Cannot initialize Vorbis analysis."));
    state.analyzed = true;
    if (vorbis_block_init(&state.dsp, &state.block) != 0)
        throw CompileError(QStringLiteral("Cannot initialize Vorbis block."));
    state.blocked = true;
    if (ogg_stream_init(&state.stream, 1) != 0)
        throw CompileError(QStringLiteral("Cannot initialize Ogg stream."));
    state.streamed = true;
    ogg_packet header, comment, codebook;
    vorbis_analysis_headerout(&state.dsp, &state.comment, &header, &comment, &codebook);
    ogg_stream_packetin(&state.stream, &header);
    ogg_stream_packetin(&state.stream, &comment);
    ogg_stream_packetin(&state.stream, &codebook);
    QByteArray output;
    ogg_page page;
    const auto appendPage = [&] {
        output.append(reinterpret_cast<const char *>(page.header), page.header_len);
        output.append(reinterpret_cast<const char *>(page.body), page.body_len);
    };
    while (ogg_stream_flush(&state.stream, &page))
        appendPage();
    QVector<float> samples(4096 * channels);
    for (;;) {
        ma_uint64 frames = 0;
        const auto status = ma_decoder_read_pcm_frames(&state.decoder, samples.data(), 4096, &frames);
        if (status != MA_SUCCESS && status != MA_AT_END)
            throw CompileError(QStringLiteral("Failed reading audio frames."));
        if (frames) {
            float **buffer = vorbis_analysis_buffer(&state.dsp, int(frames));
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < int(frames); ++i)
                    buffer[c][i] = samples.at(i * channels + c);
        }
        vorbis_analysis_wrote(&state.dsp, int(frames));
        while (vorbis_analysis_blockout(&state.dsp, &state.block) == 1) {
            vorbis_analysis(&state.block, nullptr);
            vorbis_bitrate_addblock(&state.block);
            ogg_packet packet;
            while (vorbis_bitrate_flushpacket(&state.dsp, &packet)) {
                ogg_stream_packetin(&state.stream, &packet);
                while (ogg_stream_pageout(&state.stream, &page))
                    appendPage();
            }
        }
        if (!frames)
            break;
    }
    while (ogg_stream_flush(&state.stream, &page))
        appendPage();
    return output;
}
