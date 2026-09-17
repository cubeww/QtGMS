#ifndef QTGMS_LEGACYPROJECTIMPORTER_H
#define QTGMS_LEGACYPROJECTIMPORTER_H

#include "legacyprojectreader.h"
#include <QDir>
#include <QDomDocument>
#include <QImage>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVector>

// Numeric values are the resource kinds used in the GMK resource tree.
enum class LegacyResourceKind { Object = 1, Sprite = 2, Sound = 3, Room = 4,
    Background = 6, Script = 7, Path = 8, Font = 9, Timeline = 12 };

struct LegacyResource
{
    QString name;
    QString path;
    QDomDocument document;
};

struct LegacyReference
{
    QDomElement element;
    QString attribute;
    LegacyResourceKind kind;
    int index;
    bool numeric = false;
};

class LegacyProjectImporter
{
public:
    LegacyProjectImporter(const QString &source, const QString &manifest, const QByteArray &legacyTextEncoding,
                          const std::function<bool()> &cancelled,
                          const std::function<void(const QString &, int)> &progress);
    void importProject();
    QStringList warnings() const { return m_warnings; }

private:
    void readSettings();
    void readConstants();
    void readTriggers();
    void readIncludedFiles();
    void readInformation();
    void readTree(QDomElement parent, int count, int depth = 0);
    void finishResources();
    void readResources(LegacyResourceKind kind, std::initializer_list<int> versions,
                       void (LegacyProjectImporter::*readResource)(LegacyResource &, int));
    void readSound(LegacyResource &resource, int version);
    void readSprite(LegacyResource &resource, int version);
    void readBackground(LegacyResource &resource, int version);
    void readPath(LegacyResource &resource, int version);
    void readScript(LegacyResource &resource, int version);
    void readFont(LegacyResource &resource, int version);
    void readTimeline(LegacyResource &resource, int version);
    void readObject(LegacyResource &resource, int version);
    void readRoom(LegacyResource &resource, int version);
    void readActions(QDomElement container);
    void reference(QDomElement parent, const QString &tag, LegacyResourceKind kind, int index,
                   bool attribute = false, bool numeric = false);
    QString resourceName(LegacyResourceKind kind, int index) const;
    void option(const QString &key, const QString &value);
    void booleanOption(const QString &key);
    void integerFields(QDomElement parent, const QString &fields, bool attributes = false);
    void booleanFields(QDomElement parent, const QString &fields, bool attributes = false);
    QImage readImage(bool bgra, int width = 0, int height = 0);
    void saveImage(const QString &path, QImage image, bool transparent = false, bool smooth = false);
    void write(const QString &path, const QByteArray &data);
    QDomDocument readXml(const QString &path);
    void validateName(const QString &name);

    LegacyProjectReader m_reader;
    QString m_source;
    QString m_manifestPath;
    QByteArray m_legacyTextEncoding;
    QDir m_directory;
    int m_version = 0;
    int m_stage = 0;
    QDomDocument m_manifest;
    QDomDocument m_configuration;
    QMap<LegacyResourceKind, QVector<LegacyResource>> m_resources;
    QVector<LegacyReference> m_references;
    QVector<int> m_roomOrder;
    QMap<LegacyResourceKind, QSet<int>> m_treeResources;
    QStringList m_warnings;
    std::function<void(const QString &, int)> m_progress;
};

#endif
