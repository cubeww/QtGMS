#ifndef QTGMS_EXTENSIONPACKAGE_H
#define QTGMS_EXTENSIONPACKAGE_H

#include <QTemporaryDir>

// Unpack into an isolated directory before the project transaction imports it.
class ExtensionPackage
{
public:
    bool load(const QString &path, QString &error);
    QString resourcePath() const { return m_resourcePath; }

private:
    QTemporaryDir m_directory;
    QString m_resourcePath;
};

#endif
