#include "objectdocument.h"
#include "actionxml.h"
#include "actionlibrary.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QRegularExpression>

class ObjectChangeCommand : public QUndoCommand
{
public:
    ObjectChangeCommand(ObjectDocument *document, const QDomDocument &xml, const QString &description)
        : QUndoCommand(description), m_document(document), m_before(document->xml()), m_after(xml.cloneNode(true).toDocument()) {}
    void undo() override { m_document->m_xml = m_before; emit m_document->changed(); }
    void redo() override { m_document->m_xml = m_after; emit m_document->changed(); }
private:
    ObjectDocument *m_document;
    QDomDocument m_before, m_after;
};
ObjectDocument::ObjectDocument(QObject *parent) : QObject(parent), m_undo(this) { m_undo.setUndoLimit(100); }
bool ObjectDocument::createEmpty(const QString &path, QString &error)
{
    if (QFileInfo::exists(path)) { error = tr("The object file already exists."); return false; }
    QDomDocument xml; QDomElement root = xml.createElement(QStringLiteral("object")); xml.appendChild(root);
    for (const QString &tag : {QStringLiteral("spriteName"), QStringLiteral("parentName"), QStringLiteral("maskName")}) ActionXml::setText(root, tag, QStringLiteral("<undefined>"));
    const QStringList keys = QStringLiteral("solid|visible|depth|persistent|PhysicsObject|PhysicsObjectSensor|PhysicsObjectShape|PhysicsObjectDensity|PhysicsObjectRestitution|PhysicsObjectGroup|PhysicsObjectLinearDamping|PhysicsObjectAngularDamping|PhysicsObjectFriction|PhysicsObjectAwake|PhysicsObjectKinematic").split(QLatin1Char('|'));
    const QStringList values = QStringLiteral("0|-1|0|0|0|0|0|0.5|0.1|0|0.1|0.1|0.2|-1|0").split(QLatin1Char('|'));
    for (int i = 0; i < keys.size(); ++i) ActionXml::setText(root, keys.at(i), values.at(i));
    ActionXml::child(root, QStringLiteral("events")); ActionXml::child(root, QStringLiteral("PhysicsShapePoints"));
    QSaveFile file(path); const QByteArray bytes = xml.toByteArray(2);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { error = file.errorString(); return false; }
    return true;
}
bool ObjectDocument::load(const QString &path, QString &error)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    const QByteArray bytes = file.readAll(); QDomDocument xml;
    if (!xml.setContent(bytes, false, &error)) return false;
    if (xml.documentElement().tagName() != QStringLiteral("object")) { error = tr("Expected an object resource."); return false; }
    QDomElement events = xml.documentElement().firstChildElement(QStringLiteral("events"));
    for (const QDomElement &event : ActionXml::elements(events, QStringLiteral("event"))) {
        if (event.firstChildElement(QStringLiteral("action")).isNull()) events.removeChild(event);
    }
    ActionXml::assignEditorIds(xml);
    m_xml = xml; m_source = bytes; m_path = QFileInfo(path).absoluteFilePath(); m_undo.clear(); emit changed(); return true;
}
QString ObjectDocument::name() const { QString result = QFileInfo(m_path).fileName(); result.chop(11); return result; }
void ObjectDocument::relocate(const QString &oldDirectory, const QString &newDirectory)
{ m_path = QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(m_path)); }
void ObjectDocument::edit(const QDomDocument &xml, const QString &description)
{
    QDomDocument edited = xml.cloneNode(true).toDocument();
    ActionXml::assignEditorIds(edited);
    if (edited.toByteArray() != m_xml.toByteArray()) m_undo.push(new ObjectChangeCommand(this, edited, description));
}
bool ObjectDocument::save(const Project &project, QString &error)
{
    if (!isModified()) return true;
    QMap<QString, QString> paths; for (const ResourceNode &node : ActionXml::resourceList(project, ResourceType::Object)) paths.insert(node.name, node.filePath);
    QSet<QString> ancestors; ancestors.insert(name()); QString parent = ActionXml::text(m_xml.documentElement(), QStringLiteral("parentName"));
    while (!parent.isEmpty() && parent != QStringLiteral("<undefined>")) {
        if (ancestors.contains(parent)) { error = tr("Object parent relationships cannot contain a cycle."); return false; }
        ancestors.insert(parent);
        if (!paths.contains(parent)) break; // Preserve references to missing resources.
        QFile file(paths.value(parent)); QDomDocument xml;
        if (!file.open(QIODevice::ReadOnly) || !xml.setContent(&file, false, &error)) { error = tr("Cannot read parent object %1: %2").arg(parent, error); return false; }
        parent = ActionXml::text(xml.documentElement(), QStringLiteral("parentName"));
    }
    QFile source(m_path);
    if (!source.open(QIODevice::ReadOnly)) { error = source.errorString(); return false; }
    if (source.readAll() != m_source || source.error() != QFile::NoError) { error = tr("The object changed outside the editor. Reopen it before saving."); return false; }
    source.close();
    QDomDocument stored = xml();
    ActionXml::clearEditorIds(stored.documentElement());
    const QByteArray bytes = stored.toByteArray(2); QSaveFile output(m_path);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) { error = output.errorString(); return false; }
    m_source = bytes; m_undo.setClean(); emit saved(); return true;
}
