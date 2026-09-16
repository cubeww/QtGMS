#ifndef QTGMS_RESOURCEREFERENCES_H
#define QTGMS_RESOURCEREFERENCES_H
#include "project.h"
#include <QDomDocument>
// Rewrite typed GMX references only. Code and untyped string arguments are opaque.
class ResourceReferences
{
public:
    static QString tag(ResourceType type);
    static QString suffix(ResourceType type);
    static void remove(QDomDocument &xml, ResourceType ownerType, ResourceType removedType, const QString &name, int roomIndex);
    static void rename(QDomDocument &xml, ResourceType ownerType, ResourceType renamedType, const QString &before, const QString &after);
};
#endif
