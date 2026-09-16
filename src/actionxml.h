#ifndef QTGMS_ACTIONXML_H
#define QTGMS_ACTIONXML_H
#include "project.h"
#include <QDomDocument>
struct LibraryAction;
class ActionLibraryManager;
class ActionXml
{
public:
    static QString text(QDomElement parent, const QString &tag);
    static void setText(QDomElement parent, const QString &tag, const QString &value);
    static QDomElement child(QDomElement parent, const QString &tag);
    static QList<QDomElement> elements(QDomElement parent, const QString &tag);
    static QList<ResourceNode> resourceList(const Project &project, ResourceType type);
    static QDomElement createAction(QDomDocument &xml, const LibraryAction &definition);
    static QString argumentTag(int kind);
    static QString actionText(QDomElement action, const ActionLibraryManager &libraries);
    static QString editorId(QDomElement action);
    static void assignEditorIds(QDomDocument &xml);
    static void clearEditorIds(QDomElement root);
};
#endif
