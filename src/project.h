#ifndef QTGMS_PROJECT_H
#define QTGMS_PROJECT_H

#include <QList>
#include <QMetaType>
#include <QByteArray>
#include <QMap>
#include <QStringList>
#include <QSharedPointer>

enum class ResourceType {
    Sprite, Sound, Background, Path, Script, Shader, Font, Timeline,
    Object, Room, IncludedFile, Extension, Macro, GameInformation, GameSettings
};

Q_DECLARE_METATYPE(ResourceType)

struct ResourceNode {
    ResourceType type = ResourceType::Sprite;
    QString name;
    QString filePath;
    QString thumbnailPath;
    QString spriteName;
    QString value;
    bool isGroup = false;
    bool isMissing = false;
    QList<ResourceNode> children;
};

struct ProjectConfiguration {
    QString name;
    QString filePath;
    QMap<QString, QString> options;
    QList<ResourceNode> macros;
};

struct ConfigurationEdit {
    QString name;
    int sourceIndex = 0;
    bool added = false;
};

class ProjectLoader;
class QTemporaryDir;
class QDomDocument;
class QDomElement;

class Project
{
public:
    bool isOpen() const { return !m_filePath.isEmpty(); }
    bool isTemporary() const { return !m_temporaryDirectory.isNull(); }
    static bool createTemporary(Project &project, QString &error);
    bool saveTemporaryAs(const QString &filePath, QString &error);
    const QString &filePath() const { return m_filePath; }
    const QString &informationFilePath() const { return m_informationFilePath; }
    const QList<ResourceNode> &resources(ResourceType type) const;
    const QList<ProjectConfiguration> &configurations() const { return m_configurations; }
    const QStringList &audioGroups() const { return m_audioGroups; }
    const QStringList &warnings() const { return m_warnings; }
    int resourceCount() const { return m_resourceCount; }
    void updateThumbnail(ResourceType type, const QString &filePath, const QString &thumbnailPath);
    void updateObjectSprite(const QString &filePath, const QString &spriteName, const QString &thumbnailPath);
    bool saveResourceTree(ResourceType type, const QList<ResourceNode> &nodes, QString &error);
    bool copyResource(ResourceType type, const QString &source, const QList<int> &groupPath, ResourceNode &created, QString &error);
    QStringList resourceReferences(ResourceType type, const QString &path, QString &error) const;
    bool createResource(ResourceType type, const QList<int> &groupPath, ResourceNode &resource, QString &error);
    QString shaderType(const QString &filePath) const;
    bool setShaderType(const QString &filePath, const QString &type, QString &error);
    bool validateResourceName(ResourceType type, const QString &path, const QString &name, QString &error) const;
    bool removeResource(ResourceType type, const QString &path, QString &error);
    bool renameResource(ResourceType type, const QString &path, const QString &name, QString &newPath, QString &error);
    bool saveConfigurations(const QList<ConfigurationEdit> &configurations, QString &error);
    bool ensureInformationFile(QString &error);
    bool saveGameSettings(const QString &path, const QMap<QString, QString> &before,
                          const QMap<QString, QString> &after, const QStringList &audioBefore,
                          const QStringList &audioAfter, const QMap<QString, QByteArray> &assets, QString &error);
    bool loadMacros(const QString &scopePath, QDomDocument &macros, QString &error) const;
    bool saveMacros(const QString &scopePath, const QDomDocument &before, const QDomDocument &after, QString &error);

private:
    friend class ProjectLoader;
    bool writeResourceTree(ResourceType type, const QList<ResourceNode> &nodes, const QMap<QString, QDomElement> &newEntries, QString &error);

    QString m_filePath;
    QByteArray m_sourceBytes;
    QSharedPointer<QTemporaryDir> m_temporaryDirectory;
    QString m_informationFilePath;
    QMap<ResourceType, QList<ResourceNode>> m_resources;
    QList<ProjectConfiguration> m_configurations;
    QStringList m_audioGroups = {QStringLiteral("audiogroup_default")};
    QStringList m_warnings;
    int m_resourceCount = 0;
};

#endif
