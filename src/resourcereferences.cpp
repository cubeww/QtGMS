#include "resourcereferences.h"
#include "actionxml.h"
QString ResourceReferences::tag(ResourceType type)
{
    switch (type) {
    case ResourceType::Sprite: return QStringLiteral("sprite");
    case ResourceType::Sound: return QStringLiteral("sound");
    case ResourceType::Background: return QStringLiteral("background");
    case ResourceType::Path: return QStringLiteral("path");
    case ResourceType::Script: return QStringLiteral("script");
    case ResourceType::Shader: return QStringLiteral("shader");
    case ResourceType::Font: return QStringLiteral("font");
    case ResourceType::Timeline: return QStringLiteral("timeline");
    case ResourceType::Object: return QStringLiteral("object");
    case ResourceType::Room: return QStringLiteral("room");
    case ResourceType::Extension: return QStringLiteral("extension");
    case ResourceType::IncludedFile: return QStringLiteral("datafile");
    default: return QString();
    }
}
QString ResourceReferences::suffix(ResourceType type)
{
    if (type == ResourceType::Script) return QStringLiteral(".gml");
    if (type == ResourceType::Shader) return QStringLiteral(".shader");
    if (type == ResourceType::IncludedFile || tag(type).isEmpty()) return QString();
    return QLatin1Char('.') + tag(type) + QStringLiteral(".gmx");
}
static void renameReferenceText(QDomElement owner, const QString &tag, const QString &before, const QString &after)
{ if (ActionXml::text(owner, tag) == before) ActionXml::setText(owner, tag, after); }
static void renameReferenceAttribute(QDomElement owner, const QString &attribute, const QString &before, const QString &after)
{ if (owner.hasAttribute(attribute) && owner.attribute(attribute) == before) owner.setAttribute(attribute, after); }
void ResourceReferences::rename(QDomDocument &xml, ResourceType ownerType, ResourceType renamedType, const QString &before, const QString &after)
{
    auto root = xml.documentElement();
    if (ownerType == ResourceType::Object) {
        if (renamedType == ResourceType::Sprite) for (const auto &tag : {QStringLiteral("spriteName"), QStringLiteral("maskName")}) renameReferenceText(root, tag, before, after);
        if (renamedType == ResourceType::Object) {
            renameReferenceText(root, QStringLiteral("parentName"), before, after);
            for (auto event : ActionXml::elements(root.firstChildElement(QStringLiteral("events")), QStringLiteral("event")))
                if (event.attribute(QStringLiteral("eventtype")).toInt() == 4) renameReferenceAttribute(event, QStringLiteral("ename"), before, after);
        }
    }
    if (ownerType == ResourceType::Room) {
        if (renamedType == ResourceType::Object) {
            for (auto instance : ActionXml::elements(root.firstChildElement(QStringLiteral("instances")), QStringLiteral("instance"))) renameReferenceAttribute(instance, QStringLiteral("objName"), before, after);
            for (auto view : ActionXml::elements(root.firstChildElement(QStringLiteral("views")), QStringLiteral("view"))) renameReferenceAttribute(view, QStringLiteral("objName"), before, after);
        }
        if (renamedType == ResourceType::Background) {
            for (auto background : ActionXml::elements(root.firstChildElement(QStringLiteral("backgrounds")), QStringLiteral("background"))) renameReferenceAttribute(background, QStringLiteral("name"), before, after);
            for (auto tile : ActionXml::elements(root.firstChildElement(QStringLiteral("tiles")), QStringLiteral("tile"))) renameReferenceAttribute(tile, QStringLiteral("bgName"), before, after);
        }
    }
    if (ownerType == ResourceType::Object || ownerType == ResourceType::Timeline) {
        const auto actions = root.elementsByTagName(QStringLiteral("action"));
        for (int i = 0; i < actions.size(); ++i) {
            auto action = actions.at(i).toElement();
            if (renamedType == ResourceType::Object) renameReferenceText(action, QStringLiteral("whoName"), before, after);
            for (auto argument : ActionXml::elements(action.firstChildElement(QStringLiteral("arguments")), QStringLiteral("argument"))) {
                bool valid = false; const int kind = ActionXml::text(argument, QStringLiteral("kind")).toInt(&valid);
                const QString argumentTag = valid ? ActionXml::argumentTag(kind) : QString();
                if (argumentTag != QStringLiteral("string") && argumentTag == tag(renamedType)) renameReferenceText(argument, argumentTag, before, after);
            }
        }
    }
    if (ownerType == ResourceType::Extension) {
        QString category;
        switch (renamedType) {
        case ResourceType::Sprite: category = QStringLiteral("Sprites"); break;
        case ResourceType::Sound: category = QStringLiteral("Sounds"); break;
        case ResourceType::Background: category = QStringLiteral("Backgrounds"); break;
        case ResourceType::Path: category = QStringLiteral("Paths"); break;
        case ResourceType::Script: category = QStringLiteral("Scripts"); break;
        case ResourceType::Shader: category = QStringLiteral("Shaders"); break;
        case ResourceType::Font: category = QStringLiteral("Fonts"); break;
        case ResourceType::Timeline: category = QStringLiteral("Time Lines"); break;
        case ResourceType::Object: category = QStringLiteral("Objects"); break;
        case ResourceType::Room: category = QStringLiteral("Rooms"); break;
        case ResourceType::IncludedFile: category = QStringLiteral("Included Files"); break;
        case ResourceType::Extension: category = QStringLiteral("Extensions"); break;
        default: break;
        }
        for (auto resource : ActionXml::elements(root.firstChildElement(QStringLiteral("IncludedResources")), QStringLiteral("Resource"))) {
            QString value = resource.text(); const QChar separator = value.contains(QLatin1Char('\\')) ? QLatin1Char('\\') : QLatin1Char('/');
            QStringList parts = value.split(separator);
            if (parts.size() >= 2 && parts.first() == category && parts.last() == before) {
                if (after.isEmpty()) { resource.parentNode().removeChild(resource); continue; }
                parts.last() = after; while (!resource.firstChild().isNull()) resource.removeChild(resource.firstChild()); resource.appendChild(xml.createTextNode(parts.join(separator)));
            }
        }
    }
    // Paths store their background room as an index; renaming preserves resource order.
}

void ResourceReferences::remove(QDomDocument &xml, ResourceType ownerType, ResourceType removedType, const QString &name, int roomIndex)
{
    auto root = xml.documentElement();
    if (ownerType == ResourceType::Room) {
        const QString container = removedType == ResourceType::Object ? QStringLiteral("instances") : QStringLiteral("tiles");
        const QString item = removedType == ResourceType::Object ? QStringLiteral("instance") : QStringLiteral("tile");
        const QString reference = removedType == ResourceType::Object ? QStringLiteral("objName") : QStringLiteral("bgName");
        if (removedType == ResourceType::Object || removedType == ResourceType::Background)
            for (auto entity : ActionXml::elements(root.firstChildElement(container), item))
                if (entity.attribute(reference) == name) entity.parentNode().removeChild(entity);
        if (removedType == ResourceType::Background)
            for (auto background : ActionXml::elements(root.firstChildElement(QStringLiteral("backgrounds")), QStringLiteral("background")))
                if (background.attribute(QStringLiteral("name")) == name) {
                    background.setAttribute(QStringLiteral("name"), QString());
                    background.setAttribute(QStringLiteral("visible"), 0);
                }
    }
    // Keep collision event bodies and all code/string arguments; only clear their typed target.
    rename(xml, ownerType, removedType, name, ownerType == ResourceType::Extension ? QString() : QStringLiteral("<undefined>"));
    if (ownerType == ResourceType::Path && removedType == ResourceType::Room && roomIndex >= 0) {
        bool valid = false;
        const int index = ActionXml::text(root, QStringLiteral("backroom")).toInt(&valid);
        if (valid && index >= roomIndex)
            ActionXml::setText(root, QStringLiteral("backroom"), QString::number(index == roomIndex ? -1 : index - 1));
    }
}
