#ifndef QTGMS_GAMESETTINGSDOCUMENT_H
#define QTGMS_GAMESETTINGSDOCUMENT_H
#include "project.h"
#include <QObject>
class GameSettingsDocument : public QObject
{
    Q_OBJECT
public:
    explicit GameSettingsDocument(Project *project, QObject *parent = nullptr);
    bool load(const QString &path, QString &error);
    bool save(QString &error);
    QString filePath() const { return m_path; }
    QString name() const { return m_name; }
    QString value(const QString &key, const QString &fallback = QString()) const;
    void setValue(const QString &key, const QString &value);
    bool isModified() const;
    QStringList groups(bool audio) const;
    bool renameGroup(bool audio, int index, const QString &name, QString &error);
    QStringList textureParents(int index) const;
    bool addGroup(bool audio, const QString &name, QString &error);
    bool importAsset(const QString &key, const QString &source, const QString &fileName, QString &error);
    QList<ResourceNode> groupContents(bool audio, int index) const;
    void setAssetBytes(const QString &key, const QString &fileName, const QByteArray &bytes);
    QByteArray assetBytes(const QString &key) const;
    void relocate(const QString &oldDirectory, const QString &newDirectory);
signals:
    void changed();
private:
    Project *m_project;
    QString m_path, m_name;
    QMap<QString, QString> m_before, m_options;
    QStringList m_audioBefore, m_audioGroups;
    QMap<QString, QByteArray> m_assets;
};
#endif
