#ifndef QTGMS_GM82PROJECTIMPORTER_H
#define QTGMS_GM82PROJECTIMPORTER_H

#include "actionlibrary.h"
#include <QDir>
#include <QDomDocument>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <functional>

struct Gm82Properties
{
    QString path;
    QMap<QString, QString> values;
    QString text(const QString &key, const QString &defaultValue = QString()) const;
    qint64 integer(const QString &key, qint64 defaultValue = 0) const;
    bool boolean(const QString &key, bool defaultValue = false) const;
    static Gm82Properties parse(const QString &text, const QString &path);
};

// GM82 is a directory project. Convert one resource at a time into the private
// temporary GMX project; neither the source files nor installed libraries change.
class Gm82ProjectImporter
{
public:
    Gm82ProjectImporter(const QString &source, const QString &manifest,
                        const std::function<bool()> &cancelled,
                        const std::function<void(const QString &, int)> &progress);
    void importProject();
    QStringList warnings() const { return m_warnings; }

private:
    void checkCancelled() const;
    QString sourcePath(const QString &path) const;
    QString readText(const QString &path) const;
    Gm82Properties readProperties(const QString &path) const;
    void write(const QString &path, const QByteArray &data) const;
    void copy(const QString &source, const QString &destination) const;
    void validateName(const QString &name) const;
    QString reference(const QString &group, const QString &name) const;
    void readIndexes(const Gm82Properties &header);
    void readTree(const QString &group, const QString &tag, const QString &directory);
    void readSettings(const Gm82Properties &header);
    void readIncludedFiles();
    void readTriggers();
    void readLibraries();
    void readActions(QDomElement parent, const QString &code, const QString &context);
    QDomDocument readSprite(const QString &name);
    QDomDocument readBackground(const QString &name);
    QDomDocument readSound(const QString &name);
    QDomDocument readFont(const QString &name);
    QDomDocument readPath(const QString &name);
    QDomDocument readObject(const QString &name);
    QDomDocument readTimeline(const QString &name);
    QDomDocument readRoom(const QString &name);
    void option(const QString &key, const QString &value);
    static QDomDocument document(const QString &tag);
    static QString undelimit(QString value);
    static void fields(QDomElement parent, const Gm82Properties &properties,
                       const QString &mapping, bool boolean = false, bool attributes = false);

    QString m_source;
    QString m_manifestPath;
    QDir m_sourceDirectory;
    QDir m_destination;
    QDomDocument m_manifest;
    QDomDocument m_configuration;
    QMap<QString, QStringList> m_names;
    QMap<QString, QHash<QString, int>> m_indexes;
    QStringList m_roomOrder;
    QMap<QPair<int, int>, LibraryAction> m_actions;
    QMap<int, QString> m_libraryInitializations;
    QStringList m_warnings;
    QSet<QString> m_instanceNames;
    int m_nextInstanceId = 100001;
    int m_nextTileId = 10000001;
    std::function<bool()> m_cancelled;
    std::function<void(const QString &, int)> m_progress;
};

#endif
