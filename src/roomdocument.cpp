#include "roomdocument.h"
#include "actionxml.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>

void RoomEntity::setXml(const QDomElement &element)
{
    // Detached clones still belong to their source document. Keep an independent
    // owner alive when room settings or undo snapshots replace that document.
    QDomDocument document;
    if (!element.isNull()) document.appendChild(document.importNode(element, true));
    xml = document.documentElement();
    xmlDocument = document;
}
RoomEntity RoomEntity::copy() const { RoomEntity result = *this; result.setXml(xml); return result; }
class RoomEntityCommand : public QUndoCommand
{
public:
    RoomEntityCommand(RoomDocument *document, const QVector<RoomEntity> &after, const QString &description)
        : QUndoCommand(description), m_document(document), m_after(after)
    { for (const RoomEntity &record : after) m_before.append(document->entity(record.id)); }
    void undo() override { m_document->applyEntities(m_before); }
    void redo() override { m_document->applyEntities(m_after); }
private:
    RoomDocument *m_document;
    QVector<RoomEntity> m_before, m_after;
};
class RoomSettingsCommand : public QUndoCommand
{
public:
    RoomSettingsCommand(RoomDocument *document, const QDomDocument &after, const QString &description)
        : QUndoCommand(description), m_document(document), m_before(document->settings()), m_after(after.cloneNode(true).toDocument()) {}
    void undo() override { m_document->m_settings = m_before; emit m_document->settingsChanged(); }
    void redo() override { m_document->m_settings = m_after; emit m_document->settingsChanged(); }
private:
    RoomDocument *m_document;
    QDomDocument m_before, m_after;
};
RoomDocument::RoomDocument(QObject *parent) : QObject(parent), m_undo(this) { m_undo.setUndoLimit(100); }
static bool writeRoom(const QString &path, const QByteArray &bytes, QString &error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { error = file.errorString(); return false; }
    return true;
}
bool RoomDocument::createEmpty(const QString &path, QString &error)
{
    if (QFileInfo::exists(path)) { error = tr("The room already exists."); return false; }
    QDomDocument xml; QDomElement root = xml.createElement(QStringLiteral("room")); xml.appendChild(root);
    const QStringList fields = QStringLiteral("caption|width|height|hsnap|vsnap|isometric|speed|persistent|colour|showcolour|code|enableViews|clearViewBackground|clearDisplayBuffer|PhysicsWorld|PhysicsWorldTop|PhysicsWorldLeft|PhysicsWorldRight|PhysicsWorldBottom|PhysicsWorldGravityX|PhysicsWorldGravityY|PhysicsWorldPixToMeters").split(QLatin1Char('|'));
    const QStringList values = QStringLiteral("|640|480|32|32|0|30|0|12632256|-1||0|-1|-1|0|0|0|640|480|0|10|0.1").split(QLatin1Char('|'));
    for (int i = 0; i < fields.size(); ++i) ActionXml::setText(root, fields.at(i), values.at(i));
    QDomElement maker = ActionXml::child(root, QStringLiteral("makerSettings"));
    for (const QString &key : QStringLiteral("isSet|showGrid|showObjects|showTiles|showBackgrounds|showForegrounds").split(QLatin1Char('|'))) ActionXml::setText(maker, key, QStringLiteral("-1"));
    QDomElement backgrounds = ActionXml::child(root, QStringLiteral("backgrounds")), views = ActionXml::child(root, QStringLiteral("views"));
    for (int i = 0; i < 8; ++i) {
        QDomElement bg = xml.createElement(QStringLiteral("background")); backgrounds.appendChild(bg);
        for (const QString &key : QStringLiteral("visible|foreground|x|y|hspeed|vspeed|stretch").split(QLatin1Char('|'))) bg.setAttribute(key, 0);
        bg.setAttribute(QStringLiteral("name"), QString()); bg.setAttribute(QStringLiteral("htiled"), -1); bg.setAttribute(QStringLiteral("vtiled"), -1);
        QDomElement view = xml.createElement(QStringLiteral("view")); views.appendChild(view);
        for (const QString &key : QStringLiteral("visible|xview|yview|xport|yport").split(QLatin1Char('|'))) view.setAttribute(key, 0);
        for (const QString &key : QStringLiteral("wview|wport").split(QLatin1Char('|'))) view.setAttribute(key, 640);
        for (const QString &key : QStringLiteral("hview|hport").split(QLatin1Char('|'))) view.setAttribute(key, 480);
        view.setAttribute(QStringLiteral("objName"), QStringLiteral("<undefined>"));
        view.setAttribute(QStringLiteral("hborder"), 32); view.setAttribute(QStringLiteral("vborder"), 32);
        view.setAttribute(QStringLiteral("hspeed"), -1); view.setAttribute(QStringLiteral("vspeed"), -1);
    }
    ActionXml::child(root, QStringLiteral("instances")); ActionXml::child(root, QStringLiteral("tiles"));
    return writeRoom(path, xml.toByteArray(2), error);
}
bool RoomDocument::load(const QString &path, QString &error)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    const QByteArray bytes = file.readAll(); if (file.error() != QFile::NoError) { error = file.errorString(); return false; }
    QDomDocument xml; if (!xml.setContent(bytes, false, &error)) return false;
    if (xml.documentElement().tagName() != QStringLiteral("room")) { error = tr("Expected a room resource."); return false; }
    for (const QString &key : {QStringLiteral("width"), QStringLiteral("height")}) {
        bool ok; const int dimension = ActionXml::text(xml.documentElement(), key).toInt(&ok);
        if (!ok || dimension < 1 || dimension > 10000000) { error = tr("Room dimensions must be between 1 and 10000000."); return false; }
    }
    QMap<QString, RoomEntity> records; qint64 order = 0;
    for (int tile = 0; tile < 2; ++tile) {
        QDomElement container = ActionXml::child(xml.documentElement(), tile ? QStringLiteral("tiles") : QStringLiteral("instances"));
        for (QDomElement element : ActionXml::elements(container, tile ? QStringLiteral("tile") : QStringLiteral("instance"))) {
            RoomEntity record; record.tile = tile; record.order = order++;
            record.id = (tile ? QStringLiteral("t:") : QStringLiteral("i:")) + element.attribute(tile ? QStringLiteral("id") : QStringLiteral("name"));
            if (records.contains(record.id)) { error = tr("Duplicate room instance or tile identifier: %1").arg(record.id); return false; }
            record.setXml(element); records.insert(record.id, record); container.removeChild(element);
        }
    }
    m_settings = xml; m_entities = records; m_nextOrder = order; m_source = bytes; m_path = QFileInfo(path).absoluteFilePath(); m_undo.clear(); return true;
}
QString RoomDocument::name() const { QString value = QFileInfo(m_path).fileName(); value.chop(9); return value; }
void RoomDocument::relocate(const QString &oldDirectory, const QString &newDirectory)
{ m_path = QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(m_path)); }
RoomEntity RoomDocument::entity(const QString &id) const { RoomEntity result = m_entities.value(id).copy(); result.id = id; return result; }
RoomEntity RoomDocument::createEntity(bool tile)
{
    RoomEntity record; record.tile = tile; record.order = m_nextOrder++;
    QString name;
    do {
        const QString hex = QUuid::createUuid().toString().mid(1, 8).toUpper();
        name = tile ? QString::number(hex.toUInt(nullptr, 16) & 0x7fffffff) : QStringLiteral("inst_") + hex;
        record.id = (tile ? QStringLiteral("t:") : QStringLiteral("i:")) + name;
    } while (m_entities.contains(record.id));
    record.xml = record.xmlDocument.createElement(tile ? QStringLiteral("tile") : QStringLiteral("instance")); record.xmlDocument.appendChild(record.xml);
    record.xml.setAttribute(tile ? QStringLiteral("id") : QStringLiteral("name"), name);
    if (tile) record.xml.setAttribute(QStringLiteral("name"), QStringLiteral("inst_") + QUuid::createUuid().toString().mid(1, 8).toUpper());
    record.xml.setAttribute(QStringLiteral("locked"), 0); record.xml.setAttribute(QStringLiteral("scaleX"), 1); record.xml.setAttribute(QStringLiteral("scaleY"), 1);
    record.xml.setAttribute(QStringLiteral("colour"), QStringLiteral("4294967295"));
    if (!tile) { record.xml.setAttribute(QStringLiteral("rotation"), 0); record.xml.setAttribute(QStringLiteral("code"), QString()); }
    return record;
}
void RoomDocument::applyEntities(const QVector<RoomEntity> &records)
{
    QStringList ids;
    for (const RoomEntity &record : records) { if (record.xml.isNull()) m_entities.remove(record.id); else m_entities.insert(record.id, record); ids.append(record.id); }
    emit entitiesChanged(ids);
}
void RoomDocument::editEntities(const QVector<RoomEntity> &records, const QString &description)
{
    QVector<RoomEntity> changed;
    for (const RoomEntity &record : records) {
        QDomDocument before, after; before.appendChild(before.importNode(m_entities.value(record.id).xml, true)); after.appendChild(after.importNode(record.xml, true));
        if (before.toByteArray() != after.toByteArray() || record.order != m_entities.value(record.id).order) changed.append(record.copy());
    }
    if (!changed.isEmpty()) m_undo.push(new RoomEntityCommand(this, changed, description));
}
void RoomDocument::editSettings(const QDomDocument &settings, const QString &description)
{ if (settings.toByteArray() != m_settings.toByteArray()) m_undo.push(new RoomSettingsCommand(this, settings, description)); }
bool RoomDocument::save(QString &error)
{
    if (!isModified()) return true;
    QFile input(m_path); if (!input.open(QIODevice::ReadOnly)) { error = input.errorString(); return false; }
    if (input.readAll() != m_source || input.error() != QFile::NoError) { error = tr("The room changed outside the editor. Reopen it before saving."); return false; } input.close();
    QDomDocument xml = settings(); QVector<RoomEntity> records; records.reserve(m_entities.size());
    for (const RoomEntity &record : m_entities) records.append(record);
    std::sort(records.begin(), records.end(), [](const RoomEntity &a, const RoomEntity &b) { return a.order < b.order; });
    const QDomElement root = xml.documentElement(); QDomElement instances = ActionXml::child(root, QStringLiteral("instances")), tiles = ActionXml::child(root, QStringLiteral("tiles"));
    for (const RoomEntity &record : records) (record.tile ? tiles : instances).appendChild(xml.importNode(record.xml, true));
    const QByteArray bytes = xml.toByteArray(2); if (!writeRoom(m_path, bytes, error)) return false;
    m_source = bytes; m_undo.setClean(); emit saved(); return true;
}
