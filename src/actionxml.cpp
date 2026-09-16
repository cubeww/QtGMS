#include "actionxml.h"
#include "actionlibrary.h"
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

// Session-only identities survive DOM snapshots and undo. ObjectDocument strips
// them on save, and copied actions receive fresh identities when inserted.
static const QString EditorIdAttribute = QStringLiteral("qtgms-editor-id");

QString ActionXml::editorId(QDomElement action) { return action.attribute(EditorIdAttribute); }
void ActionXml::assignEditorIds(QDomDocument &xml)
{
    QSet<QString> ids;
    const auto actions = xml.elementsByTagName(QStringLiteral("action"));
    for (int index = 0; index < actions.size(); ++index) {
        auto action = actions.at(index).toElement();
        QString id = editorId(action);
        if (id.isEmpty() || ids.contains(id)) {
            id = QUuid::createUuid().toString();
            action.setAttribute(EditorIdAttribute, id);
        }
        ids.insert(id);
    }
}
void ActionXml::clearEditorIds(QDomElement root)
{
    root.removeAttribute(EditorIdAttribute);
    const auto actions = root.elementsByTagName(QStringLiteral("action"));
    for (int index = 0; index < actions.size(); ++index)
        actions.at(index).toElement().removeAttribute(EditorIdAttribute);
}

QString ActionXml::text(QDomElement parent, const QString &tag) { return parent.firstChildElement(tag).text(); }
QDomElement ActionXml::child(QDomElement parent, const QString &tag)
{
    QDomElement result = parent.firstChildElement(tag);
    if (result.isNull()) { result = parent.ownerDocument().createElement(tag); parent.appendChild(result); }
    return result;
}
void ActionXml::setText(QDomElement parent, const QString &tag, const QString &value)
{
    QDomElement element = child(parent, tag);
    while (!element.firstChild().isNull()) element.removeChild(element.firstChild());
    element.appendChild(parent.ownerDocument().createTextNode(value));
}
QList<QDomElement> ActionXml::elements(QDomElement parent, const QString &tag)
{
    QList<QDomElement> result;
    for (QDomElement element = parent.firstChildElement(tag); !element.isNull(); element = element.nextSiblingElement(tag)) result.append(element);
    return result;
}
static void collectActionResources(const QList<ResourceNode> &nodes, QList<ResourceNode> &result)
{
    for (const ResourceNode &node : nodes) { if (node.isGroup) collectActionResources(node.children, result); else result.append(node); }
}
QList<ResourceNode> ActionXml::resourceList(const Project &project, ResourceType type)
{ QList<ResourceNode> result; collectActionResources(project.resources(type), result); return result; }
QString ActionXml::argumentTag(int kind)
{
    const QStringList tags = QStringLiteral("sprite|sound|background|path|script|object|room|font").split(QLatin1Char('|'));
    if (kind >= 5 && kind <= 12) return tags.at(kind - 5);
    return kind == 14 ? QStringLiteral("timeline") : QStringLiteral("string");
}
QDomElement ActionXml::createAction(QDomDocument &xml, const LibraryAction &definition)
{
    QDomElement action = xml.createElement(QStringLiteral("action"));
    setText(action, QStringLiteral("libid"), QString::number(definition.libraryId)); setText(action, QStringLiteral("id"), QString::number(definition.id));
    setText(action, QStringLiteral("kind"), QString::number(definition.kind)); setText(action, QStringLiteral("exetype"), QString::number(definition.executionType));
    setText(action, QStringLiteral("userelative"), definition.allowRelative ? QStringLiteral("-1") : QStringLiteral("0"));
    setText(action, QStringLiteral("isquestion"), definition.question ? QStringLiteral("-1") : QStringLiteral("0"));
    setText(action, QStringLiteral("useapplyto"), definition.canApplyTo ? QStringLiteral("-1") : QStringLiteral("0"));
    setText(action, QStringLiteral("functionname"), definition.functionName); setText(action, QStringLiteral("codestring"), definition.code);
    setText(action, QStringLiteral("whoName"), QStringLiteral("self")); setText(action, QStringLiteral("relative"), QStringLiteral("0")); setText(action, QStringLiteral("isnot"), QStringLiteral("0"));
    QDomElement args = child(action, QStringLiteral("arguments"));
    for (const LibraryArgument &arg : definition.arguments) {
        QDomElement item = xml.createElement(QStringLiteral("argument")); args.appendChild(item);
        const QString tag = argumentTag(arg.kind);
        setText(item, QStringLiteral("kind"), QString::number(arg.kind));
        setText(item, tag, tag != QStringLiteral("string") && arg.defaultValue == QStringLiteral("-100") ? QStringLiteral("<undefined>") : arg.defaultValue);
    }
    return action;
}
QString ActionXml::actionText(QDomElement action, const ActionLibraryManager &libraries)
{
    const LibraryAction *definition = libraries.action(text(action, QStringLiteral("libid")).toInt(), text(action, QStringLiteral("id")).toInt());
    QString result = definition ? definition->listText : QObject::tr("Unknown action %1/%2").arg(text(action, QStringLiteral("libid")), text(action, QStringLiteral("id")));
    if (result.isEmpty() && definition) result = definition->name;
    const QList<QDomElement> args = elements(action.firstChildElement(QStringLiteral("arguments")), QStringLiteral("argument"));
    // Expand templates in a single pass: argument text may itself contain @ tokens.
    QString expanded;
    for (int p = 0; p < result.size(); ++p) {
        if (result.at(p) != QLatin1Char('@') || p + 1 == result.size()) { expanded += result.at(p); continue; }
        const QChar token = result.at(++p); const int index = token.digitValue();
        if (index >= 0 && index < args.size()) {
            QString value = text(args.at(index), argumentTag(text(args.at(index), QStringLiteral("kind")).toInt()));
            if (definition && index < definition->arguments.size() && definition->arguments.at(index).kind == 4) {
                const QStringList menu = definition->arguments.at(index).menu.split(QLatin1Char('|')); bool ok = false; const int option = value.toInt(&ok);
                if (ok && option >= 0 && option < menu.size()) value = menu.at(option);
            }
            expanded += value;
        } else if (token == QLatin1Char('r')) { if (text(action, QStringLiteral("relative")).toInt()) expanded += QObject::tr("relative "); }
        else if (token == QLatin1Char('N')) { if (text(action, QStringLiteral("isnot")).toInt()) expanded += QObject::tr("not "); }
        else if (token == QLatin1Char('w')) { const QString who = text(action, QStringLiteral("whoName")); if (who != QStringLiteral("self")) expanded += QObject::tr("for %1 ").arg(who); }
        else if (token != QLatin1Char('b') && token != QLatin1Char('i')) expanded += QLatin1Char('@') + QString(token);
    }
    if (text(action, QStringLiteral("kind")).toInt() == 7 && !args.isEmpty()) {
        const QString code = text(args.first(), QStringLiteral("string")).trimmed();
        if (code.startsWith(QStringLiteral("///"))) expanded = code.section(QLatin1Char('\n'), 0, 0).mid(3).trimmed();
    }
    return expanded.replace(QRegularExpression(QStringLiteral("[\\r\\n]+")), QStringLiteral(" "));
}
