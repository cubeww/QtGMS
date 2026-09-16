#ifndef QTGMS_PROJECTFILETRANSACTION_H
#define QTGMS_PROJECTFILETRANSACTION_H
#include <QByteArray>
#include <QDir>
#include <QList>
#include <QMap>
#include <QTemporaryDir>

// A project edit may update XML and move/copy assets together. Keep recoverable
// preimages until ProjectLoader has accepted the resulting manifest.
class ProjectFileTransaction
{
public:
    explicit ProjectFileTransaction(const QString &directory);
    ~ProjectFileTransaction();
    bool write(const QString &path, const QByteArray &bytes, QString &error);
    bool remove(const QString &path, QString &error);
    bool copy(const QString &source, const QString &destination, QString &error);
    bool move(const QString &source, const QString &destination, QString &error);
    bool finish(QString &error);
    void rollback(QString &error);
    bool containsPath(const QString &path) const;
private:
    bool remember(const QString &path, QString &error);
    QDir m_directory;
    QTemporaryDir m_backup;
    QMap<QString, QString> m_originals;
    QStringList m_createdDirectories;
    bool m_finished = false;
};
#endif
