#ifndef QTGMS_ROOMDOCUMENT_H
#define QTGMS_ROOMDOCUMENT_H
#include <QObject>
#include <QDomDocument>
#include <QMap>
#include <QUndoStack>
#include <QVector>

struct RoomEntity {
    QString id;
    bool tile = false;
    qint64 order = 0;
    QDomDocument xmlDocument;
    QDomElement xml;
    void setXml(const QDomElement &element);
    RoomEntity copy() const;
};
class RoomEntityCommand;
class RoomSettingsCommand;
class RoomDocument : public QObject
{
    Q_OBJECT
public:
    explicit RoomDocument(QObject *parent = nullptr);
    static bool createEmpty(const QString &path, QString &error);
    bool load(const QString &path, QString &error);
    bool save(QString &error);
    void relocate(const QString &oldDirectory, const QString &newDirectory);
    QString filePath() const { return m_path; }
    QString name() const;
    bool isModified() const { return !m_undo.isClean(); }
    QUndoStack *undoStack() { return &m_undo; }
    const QMap<QString, RoomEntity> &entities() const { return m_entities; }
    RoomEntity entity(const QString &id) const;
    RoomEntity createEntity(bool tile);
    void editEntities(const QVector<RoomEntity> &records, const QString &description);
    QDomDocument settings() const { return m_settings.cloneNode(true).toDocument(); }
    void editSettings(const QDomDocument &settings, const QString &description);
signals:
    void entitiesChanged(const QStringList &ids);
    void settingsChanged();
    void saved();
private:
    friend class RoomEntityCommand;
    friend class RoomSettingsCommand;
    void applyEntities(const QVector<RoomEntity> &records);
    QMap<QString, RoomEntity> m_entities;
    QDomDocument m_settings;
    QString m_path;
    QByteArray m_source;
    QUndoStack m_undo;
    qint64 m_nextOrder = 0;
};
#endif
