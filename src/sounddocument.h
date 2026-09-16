#ifndef QTGMS_SOUNDDOCUMENT_H
#define QTGMS_SOUNDDOCUMENT_H
#include <QDomDocument>
#include <QObject>
#include <QUndoStack>

struct SoundState {
    QByteArray audio;
    QString extension;
    QString originalName;
    int attributes = 0;
    double volume = 1.0;
    int bitRate = 192;
    int sampleRate = 44100;
    int channelType = 0;
    int bitDepth = 16;
    int audioGroup = 0;
};

class SoundChangeCommand;
struct SoundLoadTimings
{
    qint64 metadataNanoseconds = 0;
    qint64 readNanoseconds = 0;
    qint64 decoderProbeNanoseconds = 0;
};

class SoundDocument : public QObject
{
    Q_OBJECT
public:
    explicit SoundDocument(QObject *parent = nullptr);
    static bool createEmpty(const QString &filePath, QString &error);
    bool load(const QString &filePath, int configurationIndex, QString &error, SoundLoadTimings *timings = nullptr);
    bool loadFromXml(const QString &filePath, const QDomDocument &document, const QByteArray &bytes,
                     int configurationIndex, QString &error, SoundLoadTimings *timings = nullptr);
    bool importAudio(const QString &filePath, QString &error);
    bool save(QString &error);
    void relocate(const QString &oldDirectory, const QString &newDirectory);
    void edit(const SoundState &state, const QString &description);
    const SoundState &state() const { return m_state; }
    const QString &filePath() const { return m_filePath; }
    QString name() const;
    bool isModified() const { return !m_undoStack.isClean(); }
    QUndoStack *undoStack() { return &m_undoStack; }
signals:
    void changed();
    void saved();
private:
    friend class SoundChangeCommand;
    void applyState(const SoundState &state);
    SoundState m_state;
    QByteArray m_savedAudio;
    QString m_audioPath;
    QString m_filePath;
    QByteArray m_sourceBytes;
    QDomDocument m_xml;
    int m_configurationIndex = 0;
    QUndoStack m_undoStack;
};
#endif
