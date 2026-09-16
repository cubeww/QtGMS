#ifndef QTGMS_SPRITEDOCUMENT_H
#define QTGMS_SPRITEDOCUMENT_H

#include <QDomDocument>
#include <QImage>
#include <QObject>
#include <QUndoStack>

struct SpriteFrame {
    QString id;
    QString filePath;
    QImage image;
    bool modified = false;
};

struct SpriteState {
    QList<SpriteFrame> frames;
    QSize size;
    QPoint origin;
    int collisionKind = 0;
    int alphaTolerance = 0;
    int boundingBoxMode = 0;
    QRect boundingBox;
    bool separateMasks = false;
    bool tileHorizontal = false;
    bool tileVertical = false;
    bool for3D = false;
    int textureGroup = 0;
};

class SpriteChangeCommand;

class SpriteDocument : public QObject
{
    Q_OBJECT
public:
    explicit SpriteDocument(QObject *parent = nullptr);
    static bool createEmpty(const QString &filePath, QString &error);
    bool load(const QString &filePath, int configurationIndex, QString &error);
    bool loadFromXml(const QString &filePath, const QDomDocument &document, const QByteArray &source,
                     int configurationIndex, QString &error);
    bool save(QString &error);
    void relocate(const QString &oldDirectory, const QString &newDirectory);
    const SpriteState &state() const { return m_state; }
    QString name() const;
    const QString &filePath() const { return m_filePath; }
    QString thumbnailPath() const;
    QUndoStack *undoStack() { return &m_undoStack; }
    bool isModified() const { return !m_undoStack.isClean(); }
    void edit(const SpriteState &state, const QString &description);
    void replaceFrame(const QString &id, const QImage &image);
    int frameIndex(const QString &id) const;
    QRect boundingBox(int frame = -1) const;
    QImage collisionMask(int frame) const;
    static SpriteFrame createFrame(const QImage &image);
    static bool importImages(const QStringList &paths, QList<SpriteFrame> &frames, QString &error);
signals:
    void changed();
    void saved();
private:
    friend class SpriteChangeCommand;
    void applyState(const SpriteState &state);
    int m_configurationIndex = 0;
    QString m_filePath;
    QByteArray m_sourceBytes;
    QDomDocument m_xml;
    SpriteState m_state;
    QUndoStack m_undoStack;
};

#endif
