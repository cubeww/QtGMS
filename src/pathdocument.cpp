#include "pathdocument.h"
#include "actionxml.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <cmath>

class PathChangeCommand : public QUndoCommand
{
public:
    PathChangeCommand(PathDocument *document, const PathState &after, const QString &description, int selected)
        : QUndoCommand(description), m_document(document), m_before(document->state()), m_after(after), m_beforeSelection(document->selectedPoint()), m_afterSelection(selected) {}
    void undo() override { apply(m_before, m_beforeSelection); }
    void redo() override { apply(m_after, m_afterSelection); }
private:
    void apply(const PathState &state, int selection) { m_document->m_state = state; m_document->m_selected = selection; emit m_document->changed(); emit m_document->selectionChanged(); }
    PathDocument *m_document;
    PathState m_before, m_after;
    int m_beforeSelection, m_afterSelection;
};
PathDocument::PathDocument(QObject *parent) : QObject(parent), m_undo(this) { m_undo.setUndoLimit(100); }
static bool writePath(const QString &path, const QByteArray &bytes, QString &error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { error = file.errorString(); return false; }
    return true;
}
bool PathDocument::createEmpty(const QString &path, QString &error)
{
    if (QFileInfo::exists(path)) { error = tr("The path file already exists."); return false; }
    return writePath(path, QByteArrayLiteral("<path><kind>0</kind><closed>-1</closed><precision>4</precision><backroom>-1</backroom><hsnap>16</hsnap><vsnap>16</vsnap><points/></path>\n"), error);
}
bool PathDocument::validate(const PathState &state, QString &error)
{
    if (state.backgroundRoom < -1 || state.precision < 1 || state.precision > 8 || state.snapX < 1 || state.snapX > 999 || state.snapY < 1 || state.snapY > 999) { error = tr("Precision must be 1 to 8; snap distances must be 1 to 999."); return false; }
    for (const auto &point : state.points) {
        if (!std::isfinite(point.position.x()) || !std::isfinite(point.position.y()) || !std::isfinite(point.speed)
            || std::abs(point.position.x()) > 1e9 || std::abs(point.position.y()) > 1e9 || point.speed < 0 || point.speed > 1e9) {
            error = tr("Path coordinates must be within +/- 1,000,000,000 and speeds between 0 and 1,000,000,000."); return false;
        }
    }
    return true;
}
bool PathDocument::load(const QString &path, QString &error)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    const QByteArray bytes = file.readAll(); if (file.error() != QFile::NoError) { error = file.errorString(); return false; }
    QDomDocument xml; if (!xml.setContent(bytes, false, &error)) return false;
    const auto root = xml.documentElement(); if (root.tagName() != QStringLiteral("path")) { error = tr("Expected a path resource."); return false; }
    PathState state; bool valid = true;
    auto integer = [&root, &valid](const QString &tag) { bool ok; const int value = ActionXml::text(root, tag).toInt(&ok); valid = valid && ok; return value; };
    const int kind = integer(QStringLiteral("kind")); valid = valid && (kind == 0 || kind == 1); state.smooth = kind == 1;
    state.closed = integer(QStringLiteral("closed")) != 0; state.precision = integer(QStringLiteral("precision"));
    state.snapX = integer(QStringLiteral("hsnap")); state.snapY = integer(QStringLiteral("vsnap")); state.backgroundRoom = integer(QStringLiteral("backroom"));
    for (auto node : ActionXml::elements(root.firstChildElement(QStringLiteral("points")), QStringLiteral("point"))) {
        const auto values = node.text().split(QLatin1Char(',')); if (values.size() != 3) { valid = false; break; }
        bool xOk, yOk, speedOk; PathPoint point;
        point.position = QPointF(values.at(0).toDouble(&xOk), values.at(1).toDouble(&yOk)); point.speed = values.at(2).toDouble(&speedOk);
        point.metadata = node; state.points.append(point); valid = valid && xOk && yOk && speedOk;
    }
    if (!valid) { error = tr("Invalid path settings or point coordinates."); return false; }
    if (!validate(state, error)) return false;
    m_state = state; m_xml = xml; m_path = QFileInfo(path).absoluteFilePath(); m_source = bytes; m_selected = state.points.isEmpty() ? -1 : 0; m_undo.clear(); emit changed(); return true;
}
QString PathDocument::name() const { QString result = QFileInfo(m_path).fileName(); result.chop(QStringLiteral(".path.gmx").size()); return result; }
void PathDocument::relocate(const QString &oldDirectory, const QString &newDirectory) { m_path = QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(m_path)); }
void PathDocument::selectPoint(int index)
{
    index = index >= 0 && index < m_state.points.size() ? index : -1;
    if (m_selected == index) return; m_selected = index; emit selectionChanged();
}
void PathDocument::edit(const PathState &state, const QString &description, int selected)
{
    bool same = state.smooth == m_state.smooth && state.closed == m_state.closed && state.precision == m_state.precision
        && state.snapX == m_state.snapX && state.snapY == m_state.snapY && state.backgroundRoom == m_state.backgroundRoom && state.points.size() == m_state.points.size();
    for (int i = 0; same && i < state.points.size(); ++i) same = state.points.at(i).position == m_state.points.at(i).position && state.points.at(i).speed == m_state.points.at(i).speed && state.points.at(i).metadata == m_state.points.at(i).metadata;
    if (same) return;
    m_undo.push(new PathChangeCommand(this, state, description, selected >= 0 && selected < state.points.size() ? selected : -1));
}
bool PathDocument::save(QString &error)
{
    if (!validate(m_state, error)) return false;
    if (!isModified()) return true;
    QFile source(m_path); if (!source.open(QIODevice::ReadOnly)) { error = source.errorString(); return false; }
    if (source.readAll() != m_source || source.error() != QFile::NoError) { error = tr("The path changed outside the editor. Reopen it before saving."); return false; } source.close();
    auto xml = m_xml.cloneNode(true).toDocument(); auto root = xml.documentElement();
    ActionXml::setText(root, QStringLiteral("kind"), m_state.smooth ? QStringLiteral("1") : QStringLiteral("0"));
    ActionXml::setText(root, QStringLiteral("closed"), m_state.closed ? QStringLiteral("-1") : QStringLiteral("0"));
    ActionXml::setText(root, QStringLiteral("precision"), QString::number(m_state.precision));
    ActionXml::setText(root, QStringLiteral("hsnap"), QString::number(m_state.snapX)); ActionXml::setText(root, QStringLiteral("vsnap"), QString::number(m_state.snapY));
    ActionXml::setText(root, QStringLiteral("backroom"), QString::number(m_state.backgroundRoom));
    auto points = ActionXml::child(root, QStringLiteral("points")); auto originals = ActionXml::elements(points, QStringLiteral("point"));
    for (int i = 0; i < m_state.points.size(); ++i) {
        const auto &point = m_state.points.at(i); auto node = point.metadata.isNull() ? xml.createElement(QStringLiteral("point")) : xml.importNode(point.metadata, true).toElement();
        while (!node.firstChild().isNull()) node.removeChild(node.firstChild());
        node.appendChild(xml.createTextNode(QStringLiteral("%1,%2,%3").arg(QString::number(point.position.x(), 'g', 16), QString::number(point.position.y(), 'g', 16), QString::number(point.speed, 'g', 16))));
        if (i < originals.size()) points.replaceChild(node, originals.at(i)); else points.appendChild(node);
    }
    for (int i = m_state.points.size(); i < originals.size(); ++i) points.removeChild(originals.at(i));
    const QByteArray bytes = xml.toByteArray(2); if (!writePath(m_path, bytes, error)) return false;
    m_xml = xml; m_source = bytes; m_undo.setClean(); emit saved(); return true;
}
