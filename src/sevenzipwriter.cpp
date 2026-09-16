#include "sevenzipwriter.h"

#include <7zCrc.h>
#include <LzmaEnc.h>
#include <LzFind.h>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QVector>
#include <cstdlib>
#include <limits>
#include <memory>

struct ArchiveFile
{
    QString name;
    qint64 size;
    QDateTime modified;
    quint32 checksum = 0;
};

static void appendLittleEndian(QByteArray &bytes, quint64 value, int count)
{
    for (int i = 0; i < count; ++i) {
        bytes.append(char(value & 255));
        value >>= 8;
    }
}

static void appendArchiveNumber(QByteArray &bytes, quint64 value)
{
    quint8 prefix = 0;
    for (int count = 0; count < 8; ++count) {
        if (value < (quint64(1) << (7 + 7 * count))) {
            bytes.append(char(prefix | (value >> (8 * count))));
            appendLittleEndian(bytes, value, count);
            return;
        }
        prefix |= quint8(0x80 >> count);
    }
    bytes.append(char(255));
    appendLittleEndian(bytes, value, 8);
}

static void appendProperty(QByteArray &header, char id, const QByteArray &data)
{
    header.append(id);
    appendArchiveNumber(header, quint64(data.size()));
    header.append(data);
}

static void *allocateEncoderMemory(ISzAllocPtr, size_t size) { return std::malloc(size); }
static void freeEncoderMemory(ISzAllocPtr, void *memory) { std::free(memory); }
static const ISzAlloc EncoderAllocator = { allocateEncoderMemory, freeEncoderMemory };

// The SDK provides compression streams, but no C archive writer. This is the
// 7z container framing from the SDK's 7zFormat.txt: one LZMA folder, with a
// substream and CRC for each nonempty file, and an uncompressed UTF-16 header.
struct ArchiveInputStream
{
    ISeqInStream stream;
    QDir directory;
    QVector<ArchiveFile> *files;
    const std::function<bool(const QString &, int)> *progress;
    QFile input;
    QString error;
    int index = 0;
    qint64 completed = 0;
    qint64 total = 0;
    qint64 remaining = 0;
    quint32 checksum = CRC_INIT_VAL;

    bool report() const
    {
        return (*progress)(index < files->size() ? files->at(index).name : QString(),
                           total ? int(double(completed) / double(total) * 99) : 0);
    }
};

static SRes readArchiveInput(ISeqInStreamPtr stream, void *buffer, size_t *size)
{
    auto *input = reinterpret_cast<ArchiveInputStream *>(const_cast<ISeqInStream *>(stream));
    const size_t requested = *size;
    *size = 0;
    if (!input->report()) return SZ_ERROR_PROGRESS;
    if (!requested) return SZ_OK;
    while (input->index < input->files->size()) {
        ArchiveFile &entry = (*input->files)[input->index];
        if (!entry.size) { ++input->index; continue; }
        if (!input->input.isOpen()) {
            input->input.setFileName(input->directory.filePath(entry.name));
            if (!input->input.open(QIODevice::ReadOnly)) {
                input->error = input->input.fileName() + QStringLiteral(": ") + input->input.errorString();
                return SZ_ERROR_READ;
            }
            if (input->input.size() != entry.size || QFileInfo(input->input).lastModified() != entry.modified)
                return SZ_ERROR_READ;
            input->remaining = entry.size;
            input->checksum = CRC_INIT_VAL;
        }
        const qint64 count = input->input.read(static_cast<char *>(buffer),
            qMin(input->remaining, qint64(qMin(requested, size_t(1024 * 1024)))));
        if (count <= 0) {
            input->error = input->input.fileName() + QStringLiteral(": ") + input->input.errorString();
            return SZ_ERROR_READ;
        }
        input->checksum = CrcUpdate(input->checksum, buffer, size_t(count));
        input->remaining -= count;
        input->completed += count;
        *size = size_t(count);
        if (!input->remaining) {
            if (input->input.size() != entry.size || QFileInfo(input->input).lastModified() != entry.modified)
                return SZ_ERROR_READ;
            entry.checksum = CRC_GET_DIGEST(input->checksum);
            input->input.close();
            ++input->index;
        }
        return SZ_OK;
    }
    return SZ_OK;
}

struct ArchiveOutputStream
{
    ISeqOutStream stream;
    QSaveFile *output;
};

static size_t writeArchiveOutput(ISeqOutStreamPtr stream, const void *buffer, size_t size)
{
    const auto *output = reinterpret_cast<const ArchiveOutputStream *>(stream);
    const qint64 written = output->output->write(static_cast<const char *>(buffer), qint64(size));
    return written < 0 ? 0 : size_t(written);
}

struct ArchiveProgress
{
    ICompressProgress progress;
    ArchiveInputStream *input;
};

static SRes reportArchiveProgress(ICompressProgressPtr progress, UInt64, UInt64)
{
    return reinterpret_cast<const ArchiveProgress *>(progress)->input->report() ? SZ_OK : SZ_ERROR_PROGRESS;
}

bool SevenZipWriter::write(const QString &destination, const QString &directory,
                           const QStringList &names, QString &error,
                           const std::function<bool(const QString &, int)> &progress)
{
    error.clear();
    static const bool initialized = [] { CrcGenerateTable(); LzFindPrepare(); return true; }();
    Q_UNUSED(initialized);
    QVector<ArchiveFile> files;
    qint64 total = 0;
    int streamCount = 0;
    for (const QString &name : names) {
        const QFileInfo info(QDir(directory).filePath(name));
        if (!info.isFile() || info.size() > std::numeric_limits<qint64>::max() - total) {
            error = tr("Cannot read project file: %1").arg(info.filePath());
            return false;
        }
        ArchiveFile entry;
        entry.name = name;
        entry.size = info.size();
        entry.modified = info.lastModified();
        files.append(entry);
        total += entry.size;
        if (entry.size) ++streamCount;
    }
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly) || output.write(QByteArray(32, '\0')) != 32) {
        error = output.errorString();
        return false;
    }
    QByteArray properties(LZMA_PROPS_SIZE, '\0');
    if (streamCount) {
        ArchiveInputStream input;
        input.stream.Read = readArchiveInput;
        input.directory = QDir(directory);
        input.files = &files;
        input.progress = &progress;
        input.total = total;
        ArchiveOutputStream encoded = { { writeArchiveOutput }, &output };
        ArchiveProgress status = { { reportArchiveProgress }, &input };
        const auto destroyEncoder = [](CLzmaEnc *encoder) {
            LzmaEnc_Destroy(encoder, &EncoderAllocator, &EncoderAllocator);
        };
        std::unique_ptr<CLzmaEnc, decltype(destroyEncoder)> encoder(LzmaEnc_Create(&EncoderAllocator), destroyEncoder);
        if (!encoder) { error = tr("Not enough memory to export the project."); return false; }
        CLzmaEncProps settings;
        LzmaEncProps_Init(&settings);
        settings.level = 3;
        settings.dictSize = 4 * 1024 * 1024;
        settings.numThreads = 1;
        settings.reduceSize = UInt64(total);
        SRes result = LzmaEnc_SetProps(encoder.get(), &settings);
        SizeT propertiesSize = LZMA_PROPS_SIZE;
        if (result == SZ_OK)
            result = LzmaEnc_WriteProperties(encoder.get(), reinterpret_cast<Byte *>(properties.data()), &propertiesSize);
        if (result == SZ_OK)
            result = LzmaEnc_Encode(encoder.get(), &encoded.stream, &input.stream, &status.progress,
                                   &EncoderAllocator, &EncoderAllocator);
        if (result != SZ_OK) {
            error = !input.error.isEmpty() ? input.error
                : result == SZ_ERROR_WRITE ? output.errorString()
                : tr("Cannot compress the project (error %1). A source file may have changed during export.").arg(result);
            return false;
        }
    }
    const quint64 packedSize = quint64(output.pos() - 32);
    QByteArray header;
    header.append(char(0x01)); // Header
    if (streamCount) {
        header.append(char(0x04)); // MainStreamsInfo
        header.append(char(0x06)); // PackInfo
        appendArchiveNumber(header, 0);
        appendArchiveNumber(header, 1);
        header.append(char(0x09)); // Size
        appendArchiveNumber(header, packedSize);
        header.append(char(0));
        header.append(char(0x07)); // UnPackInfo
        header.append(char(0x0b)); // Folder
        appendArchiveNumber(header, 1);
        header.append(char(0)); // Inline folder
        appendArchiveNumber(header, 1); // One coder
        header.append(QByteArray::fromHex("23030101")); // LZMA with properties
        appendArchiveNumber(header, quint64(properties.size()));
        header.append(properties);
        header.append(char(0x0c)); // CodersUnPackSize
        appendArchiveNumber(header, quint64(total));
        header.append(char(0));
        header.append(char(0x08)); // SubStreamsInfo
        header.append(char(0x0d)); // NumUnPackStream
        appendArchiveNumber(header, quint64(streamCount));
        if (streamCount > 1) {
            header.append(char(0x09));
            int remaining = streamCount;
            for (const ArchiveFile &entry : files)
                if (entry.size && --remaining) appendArchiveNumber(header, quint64(entry.size));
        }
        header.append(char(0x0a)); // CRC, all defined
        header.append(char(1));
        for (const ArchiveFile &entry : files)
            if (entry.size) appendLittleEndian(header, entry.checksum, 4);
        header.append(char(0)); // End SubStreamsInfo
        header.append(char(0)); // End MainStreamsInfo
    }
    header.append(char(0x05)); // FilesInfo
    appendArchiveNumber(header, quint64(files.size()));
    if (streamCount != files.size()) {
        QByteArray empty((files.size() + 7) / 8, '\0');
        for (int i = 0; i < files.size(); ++i)
            if (!files.at(i).size) empty[i / 8] = char(quint8(empty.at(i / 8)) | (0x80 >> (i % 8)));
        appendProperty(header, char(0x0e), empty);
        // Every empty entry is a regular file, not a directory.
        appendProperty(header, char(0x0f), QByteArray((files.size() - streamCount + 7) / 8, char(255)));
    }
    QByteArray fileNames(1, '\0'); // Inline names
    QByteArray times = QByteArray::fromHex("0100"); // All defined, inline
    for (const ArchiveFile &entry : files) {
        for (QChar character : entry.name) appendLittleEndian(fileNames, character.unicode(), 2);
        appendLittleEndian(fileNames, 0, 2);
        appendLittleEndian(times, quint64(entry.modified.toMSecsSinceEpoch() + Q_INT64_C(11644473600000)) * 10000, 8);
    }
    appendProperty(header, char(0x11), fileNames);
    appendProperty(header, char(0x14), times);
    header.append(char(0)); // End FilesInfo
    header.append(char(0)); // End Header
    QByteArray start;
    appendLittleEndian(start, packedSize, 8);
    appendLittleEndian(start, quint64(header.size()), 8);
    appendLittleEndian(start, CrcCalc(header.constData(), size_t(header.size())), 4);
    QByteArray signature = QByteArray::fromHex("377abcaf271c0004");
    appendLittleEndian(signature, CrcCalc(start.constData(), size_t(start.size())), 4);
    signature.append(start);
    if (!progress(QString(), 99)) return false;
    if (output.write(header) != header.size() || !output.seek(0)
        || output.write(signature) != signature.size() || !output.commit()) {
        error = output.errorString();
        return false;
    }
    return true;
}
