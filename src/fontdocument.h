#ifndef QTGMS_FONTDOCUMENT_H
#define QTGMS_FONTDOCUMENT_H

#include <QDomDocument>
#include <QObject>
#include <QUndoStack>
#include <QVector>

struct FontRange {
    int first = 32;
    int last = 127;
    bool operator==(const FontRange &other) const { return first == other.first && last == other.last; }
};
struct FontState {
    QString family = QStringLiteral("Arial");
    int size = 12;
    bool bold = false;
    bool italic = false;
    bool highQuality = true;
    int antiAlias = 3;
    bool includeTTF = false;
    int textureGroup = 0;
    QVector<FontRange> ranges = {FontRange()};
    bool operator==(const FontState &other) const;
};
class FontChangeCommand;
class FontDocument : public QObject
{
    Q_OBJECT
public:
    explicit FontDocument(QObject *parent = nullptr);
    static bool createEmpty(const QString &filePath, QString &error);
    bool load(const QString &filePath, int configurationIndex, QString &error);
    bool loadFromXml(const QString &filePath, const QDomDocument &xml, const QByteArray &bytes,
                     int configurationIndex, QString &error);
    bool save(QString &error);
    void edit(const FontState &state, const QString &description);
    void relocate(const QString &oldDirectory, const QString &newDirectory);
    const FontState &state() const { return m_state; }
    QString filePath() const { return m_filePath; }
    QString name() const;
    bool isModified() const { return !m_undoStack.isClean(); }
    QUndoStack *undoStack() { return &m_undoStack; }
signals:
    void changed();
    void saved();
private:
    friend class FontChangeCommand;
    FontState m_state;
    FontState m_savedState;
    QString m_filePath;
    QByteArray m_sourceBytes;
    QByteArray m_imageBytes;
    bool m_imageExists = false;
    QDomDocument m_xml;
    int m_configurationIndex = 0;
    QUndoStack m_undoStack;
};

#endif
