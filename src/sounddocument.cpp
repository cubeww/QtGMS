#include "sounddocument.h"
#include "audiopreview.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUuid>
#include <QtMath>

class SoundChangeCommand : public QUndoCommand
{
public:
    SoundChangeCommand(SoundDocument *document, const SoundState &state, const QString &description)
        : QUndoCommand(description), m_document(document), m_before(document->state()), m_after(state) {}
    void undo() override { m_document->applyState(m_before); }
    void redo() override { m_document->applyState(m_after); }
private:
    SoundDocument *m_document;
    SoundState m_before;
    SoundState m_after;
};

static void setSoundText(QDomDocument &document, QDomElement parent, const QString &tag, const QString &text)
{
    QDomElement element = parent.firstChildElement(tag);
    if (element.isNull()) { element = document.createElement(tag); parent.appendChild(element); }
    while (!element.firstChild().isNull()) element.removeChild(element.firstChild());
    element.appendChild(document.createTextNode(text));
}
static QString soundOption(const QDomElement &root, const QString &container, int index, const QString &defaultValue)
{
    const QDomElement parent = root.firstChildElement(container);
    QDomElement element = parent.firstChildElement();
    const QString initial = element.isNull() ? (parent.text().isEmpty() ? defaultValue : parent.text()) : element.text();
    for (int i = 0; i < index && !element.isNull(); ++i) element = element.nextSiblingElement();
    return element.isNull() ? initial : element.text();
}
static bool soundFlag(const QDomElement &root, const QString &name)
{
    const QString value = root.firstChildElement(name).text().trimmed();
    return value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 || value.toInt() != 0;
}
static void setSoundOption(QDomDocument &document, const QString &container, const QString &tag, int index, const QString &value)
{
    QDomElement root = document.documentElement();
    QDomElement parent = root.firstChildElement(container);
    if (parent.isNull()) { parent = document.createElement(container); root.appendChild(parent); }
    const QString initial = parent.firstChildElement().isNull() ? value : parent.firstChildElement().text();
    QDomElement element = parent.firstChildElement();
    for (int i = 0; i <= index; ++i) {
        if (element.isNull()) {
            element = document.createElement(tag); parent.appendChild(element);
            element.appendChild(document.createTextNode(initial));
        }
        if (i == index) {
            while (!element.firstChild().isNull()) element.removeChild(element.firstChild());
            element.appendChild(document.createTextNode(value));
        } else element = element.nextSiblingElement();
    }
}
static void writeSoundState(QDomDocument &document, const SoundState &state, int index, const QString &audioName)
{
    const QDomElement root = document.documentElement();
    setSoundText(document, root, QStringLiteral("extension"), state.extension);
    setSoundText(document, root, QStringLiteral("origname"), audioName.isEmpty() ? QString() : QStringLiteral("audio\\") + audioName);
    setSoundText(document, root, QStringLiteral("data"), audioName);
    setSoundText(document, root, QStringLiteral("compressed"), state.attributes == 0 ? QStringLiteral("0") : QStringLiteral("-1"));
    setSoundText(document, root, QStringLiteral("streamed"), state.attributes == 3 ? QStringLiteral("-1") : QStringLiteral("0"));
    setSoundText(document, root, QStringLiteral("uncompressOnLoad"), state.attributes == 2 ? QStringLiteral("-1") : QStringLiteral("0"));
    setSoundText(document, root, QStringLiteral("audioGroup"), QString::number(state.audioGroup));
    setSoundOption(document, QStringLiteral("volume"), QStringLiteral("volume"), index, QString::number(state.volume, 'g', 16));
    setSoundOption(document, QStringLiteral("bitRates"), QStringLiteral("bitRate"), index, QString::number(state.bitRate));
    setSoundOption(document, QStringLiteral("sampleRates"), QStringLiteral("sampleRate"), index, QString::number(state.sampleRate));
    setSoundOption(document, QStringLiteral("types"), QStringLiteral("type"), index, QString::number(state.channelType));
    setSoundOption(document, QStringLiteral("bitDepths"), QStringLiteral("bitDepth"), index, QString::number(state.bitDepth));
}
static bool readSoundAudio(const QString &path, QByteArray &audio, QString &error, SoundLoadTimings *timings = nullptr)
{
    QElapsedTimer timer;
    if (timings) timer.start();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > 256 * 1024 * 1024) { error = QObject::tr("The audio file exceeds the 256 MB limit of this 32-bit editor."); return false; }
    audio = file.readAll();
    if (file.error() != QFile::NoError) { error = file.errorString(); return false; }
    if (timings) { timings->readNanoseconds = timer.nsecsElapsed(); timer.restart(); }
    const bool decoded = AudioPreview::canDecode(audio, error);
    if (timings) timings->decoderProbeNanoseconds = timer.nsecsElapsed();
    return decoded;
}

SoundDocument::SoundDocument(QObject *parent) : QObject(parent), m_undoStack(this) { m_undoStack.setUndoLimit(100); }
QString SoundDocument::name() const
{ QString result = QFileInfo(m_filePath).fileName(); result.chop(QStringLiteral(".sound.gmx").size()); return result; }
void SoundDocument::applyState(const SoundState &state) { m_state = state; emit changed(); }
void SoundDocument::edit(const SoundState &state, const QString &description)
{ m_undoStack.push(new SoundChangeCommand(this, state, description)); }

bool SoundDocument::createEmpty(const QString &filePath, QString &error)
{
    error.clear();
    QDomDocument document; document.appendChild(document.createElement(QStringLiteral("sound")));
    for (const QString &tag : {QStringLiteral("kind"), QStringLiteral("effects"), QStringLiteral("pan")})
        setSoundText(document, document.documentElement(), tag, QStringLiteral("0"));
    setSoundText(document, document.documentElement(), QStringLiteral("preload"), QStringLiteral("-1"));
    writeSoundState(document, SoundState(), 0, QString());
    const QByteArray bytes = document.toByteArray(2);
    QTemporaryFile output(QFileInfo(filePath).absoluteDir().filePath(QStringLiteral(".qtgms-sound-XXXXXX")));
    if (!output.open() || output.write(bytes) != bytes.size() || !output.flush()) { error = output.errorString(); return false; }
    output.close();
    if (!output.rename(filePath)) { error = output.errorString(); return false; }
    output.setAutoRemove(false); return true;
}
bool SoundDocument::load(const QString &filePath, int configurationIndex, QString &error, SoundLoadTimings *timings)
{
    QElapsedTimer timer;
    if (timings) { *timings = SoundLoadTimings(); timer.start(); }
    error.clear();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    const QByteArray bytes = file.readAll();
    QDomDocument document;
    if (!document.setContent(bytes, false, &error)) return false;
    const qint64 xmlNanoseconds = timings ? timer.nsecsElapsed() : 0;
    const bool loaded = loadFromXml(filePath, document, bytes, configurationIndex, error, timings);
    if (timings) timings->metadataNanoseconds += xmlNanoseconds;
    return loaded;
}

bool SoundDocument::loadFromXml(const QString &filePath, const QDomDocument &document, const QByteArray &bytes,
                                int configurationIndex, QString &error, SoundLoadTimings *timings)
{
    QElapsedTimer timer;
    if (timings) { *timings = SoundLoadTimings(); timer.start(); }
    error.clear();
    const QDomElement root = document.documentElement();
    if (root.tagName() != QStringLiteral("sound")) { error = tr("Expected a sound resource."); return false; }
    const int index = qMax(0, configurationIndex);
    SoundState state;
    state.extension = root.firstChildElement(QStringLiteral("extension")).text();
    state.originalName = root.firstChildElement(QStringLiteral("origname")).text();
    state.attributes = soundFlag(root, QStringLiteral("streamed")) ? 3
        : soundFlag(root, QStringLiteral("uncompressOnLoad")) ? 2
        : soundFlag(root, QStringLiteral("compressed")) ? 1 : 0;
    state.volume = soundOption(root, QStringLiteral("volume"), index, QStringLiteral("1")).toDouble();
    state.bitRate = soundOption(root, QStringLiteral("bitRates"), index, QStringLiteral("192")).toInt();
    state.sampleRate = soundOption(root, QStringLiteral("sampleRates"), index, QStringLiteral("44100")).toInt();
    state.channelType = soundOption(root, QStringLiteral("types"), index, QStringLiteral("0")).toInt();
    state.bitDepth = soundOption(root, QStringLiteral("bitDepths"), index, QStringLiteral("16")).toInt();
    state.audioGroup = root.firstChildElement(QStringLiteral("audioGroup")).text().toInt();
    if (!qIsFinite(state.volume) || state.volume < 0 || state.volume > 1 || state.sampleRate < 1 || state.sampleRate > 192000
        || state.bitRate < 1 || state.bitRate > 512 || state.audioGroup < 0
        || state.channelType < 0 || state.channelType > 2 || (state.bitDepth != 8 && state.bitDepth != 16)) {
        error = tr("The sound contains invalid target options."); return false;
    }
    const QString data = root.firstChildElement(QStringLiteral("data")).text().trimmed();
    const QString extension = state.extension.toLower();
    if (!data.isEmpty() && extension != QStringLiteral(".wav") && extension != QStringLiteral(".mp3") && extension != QStringLiteral(".ogg")) {
        error = tr("This editor supports WAV, MP3 and OGG Vorbis sounds."); return false;
    }
    const QString audioPath = data.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(filePath).absoluteDir().absoluteFilePath(
        QStringLiteral("audio/") + QString(data).replace(QLatin1Char('\\'), QLatin1Char('/'))));
    if (timings) timings->metadataNanoseconds = timer.nsecsElapsed();
    if (!audioPath.isEmpty() && !readSoundAudio(audioPath, state.audio, error, timings)) return false;
    m_filePath = QFileInfo(filePath).absoluteFilePath(); m_configurationIndex = index;
    m_audioPath = audioPath; m_savedAudio = state.audio; m_sourceBytes = bytes; m_xml = document;
    m_undoStack.clear(); applyState(state); return true;
}
bool SoundDocument::importAudio(const QString &filePath, QString &error)
{
    error.clear();
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix != QStringLiteral("wav") && suffix != QStringLiteral("mp3") && suffix != QStringLiteral("ogg")) {
        error = tr("Choose a WAV, MP3 or OGG Vorbis file."); return false;
    }
    SoundState state = m_state;
    if (!readSoundAudio(filePath, state.audio, error)) return false;
    state.extension = QLatin1Char('.') + suffix; state.originalName = QFileInfo(filePath).fileName();
    edit(state, tr("Load sound")); return true;
}
void SoundDocument::relocate(const QString &oldDirectory, const QString &newDirectory)
{
    m_filePath = QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(m_filePath));
    if (!m_audioPath.isEmpty()) m_audioPath = QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(m_audioPath));
}
bool SoundDocument::save(QString &error)
{
    error.clear();
    if (!isModified()) return true;
    QFile source(m_filePath);
    if (!source.open(QIODevice::ReadOnly)) { error = source.errorString(); return false; }
    if (source.readAll() != m_sourceBytes) { error = tr("The sound file changed outside the editor. Reopen it before saving."); return false; }
    source.close();
    const QDir directory = QFileInfo(m_filePath).absoluteDir();
    QString audioPath = m_audioPath; bool created = false;
    if (m_state.audio.isEmpty()) audioPath.clear();
    else if (m_state.audio != m_savedAudio || QFileInfo(audioPath).suffix().compare(m_state.extension.mid(1), Qt::CaseInsensitive) != 0) {
        if (!directory.mkpath(QStringLiteral("audio"))) { error = tr("Cannot create the audio directory."); return false; }
        QString token = QUuid::createUuid().toString(); token.remove(QLatin1Char('{')).remove(QLatin1Char('}')).remove(QLatin1Char('-'));
        audioPath = directory.absoluteFilePath(QStringLiteral("audio/%1_%2%3").arg(name(), token, m_state.extension));
        QSaveFile output(audioPath);
        if (!output.open(QIODevice::WriteOnly) || output.write(m_state.audio) != m_state.audio.size() || !output.commit()) { error = output.errorString(); return false; }
        created = true;
    }
    QDomDocument document = m_xml.cloneNode(true).toDocument();
    writeSoundState(document, m_state, m_configurationIndex, audioPath.isEmpty() ? QString()
        : QDir::toNativeSeparators(QDir(directory.filePath(QStringLiteral("audio"))).relativeFilePath(audioPath)));
    const QByteArray bytes = document.toByteArray(2); QSaveFile output(m_filePath);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        error = output.errorString(); if (created) QFile::remove(audioPath); return false;
    }
    m_audioPath = audioPath; m_savedAudio = m_state.audio; m_sourceBytes = bytes; m_xml = document;
    m_undoStack.setClean(); emit saved(); return true;
}
