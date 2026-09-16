#ifndef QTGMS_SCRIPTSOURCE_H
#define QTGMS_SCRIPTSOURCE_H

#include <QString>
#include <QVector>

struct ScriptSection
{
    QString name;
    QString code;
    int headerOffset = 0;
    int offset = 0;
};

class ScriptSource
{
public:
    static bool hasDefinitions(const QString &source);
    static QVector<ScriptSection> sections(const QString &source, const QString &resourceName);
    static QString combine(const QVector<ScriptSection> &sections, bool definitions);
};

#endif
