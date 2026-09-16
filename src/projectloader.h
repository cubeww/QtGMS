#ifndef QTGMS_PROJECTLOADER_H
#define QTGMS_PROJECTLOADER_H

#include "project.h"

#include <QDir>
#include <QXmlStreamReader>

class ProjectLoader
{
public:
    // Publish the new project only after the complete manifest is valid.
    bool load(const QString &filePath, Project &project, QString &error);

private:
    QList<ResourceNode> readResources(ResourceType type, const QString &groupTag,
                                     const QString &itemTag, const QString &suffix,
                                     const QString &dataDirectory = QString(), int depth = 0);
    void readConfigurations();
    void readConfiguration(ProjectConfiguration &configuration);
    void checkResource(ResourceNode &resource);
    void readThumbnailMetadata(QList<ResourceNode> &resources);
    void readThumbnailMetadata(ResourceNode &resource);
    void resolveObjectThumbnails(QList<ResourceNode> &resources,
                                 const QMap<QString, QString> &spriteThumbnails);
    QString resourcePath(const QString &reference, const QString &suffix) const;

    Project m_project;
    QDir m_directory;
    QXmlStreamReader m_xml;
};

#endif
