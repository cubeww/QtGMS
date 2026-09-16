#ifndef QTGMS_BACKGROUNDDOCUMENT_H
#define QTGMS_BACKGROUNDDOCUMENT_H

#include <QDomDocument>
#include <QImage>
#include <QObject>
#include <QUndoStack>

struct BackgroundState {
    QImage image;
    bool isTileSet = false;
    int tileWidth = 16;
    int tileHeight = 16;
    int horizontalOffset = 0;
    int verticalOffset = 0;
    int horizontalSeparation = 0;
    int verticalSeparation = 0;
    bool tileHorizontal = true;
    bool tileVertical = true;
    bool for3D = false;
    int textureGroup = 0;
};

class BackgroundChangeCommand;

class BackgroundDocument : public QObject
{
    Q_OBJECT
public:
    explicit BackgroundDocument(QObject *parent = nullptr);
    static bool createEmpty(const QString &filePath, QString &error);
    bool load(const QString &filePath, int configurationIndex, QString &error);
    bool loadFromXml(const QString &filePath, const QDomDocument &document, const QByteArray &bytes,
                     int configurationIndex, QString &error);
    bool save(QString &error);
    void relocate(const QString &oldDirectory, const QString &newDirectory);
    void edit(const BackgroundState &state, const QString &description);
    void replaceImage(const QImage &image);
    const BackgroundState &state() const { return m_state; }
    const QString &filePath() const { return m_filePath; }
    const QString &thumbnailPath() const { return m_imagePath; }
    QString name() const;
    bool isModified() const { return !m_undoStack.isClean(); }
    QUndoStack *undoStack() { return &m_undoStack; }
signals:
    void changed();
    void saved();
private:
    friend class BackgroundChangeCommand;
    void applyState(const BackgroundState &state);
    BackgroundState m_state;
    QImage m_savedImage;
    int m_configurationIndex = 0;
    QString m_filePath;
    QString m_imagePath;
    QDomDocument m_xml;
    QByteArray m_sourceBytes;
    QUndoStack m_undoStack;
};

#endif
