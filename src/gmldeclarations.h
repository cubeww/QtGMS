#ifndef QTGMS_GMLDECLARATIONS_H
#define QTGMS_GMLDECLARATIONS_H

#include <QSet>
#include <QString>

// Presentation metadata, tolerant of incomplete code while typing.
struct GmlDeclarations
{
    QSet<QString> names;
    QSet<int> positions;
    static GmlDeclarations fromCode(const QString &source);
};

#endif
