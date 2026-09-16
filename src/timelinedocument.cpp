#include "timelinedocument.h"
#include "actionxml.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

class TimelineChangeCommand : public QUndoCommand
{
public:
    TimelineChangeCommand(TimelineDocument *document, const QDomDocument &xml, const QString &description, int selectedStep)
        : QUndoCommand(description), m_document(document), m_before(document->xml()), m_after(xml.cloneNode(true).toDocument()),
          m_beforeStep(document->selectedStep()), m_afterStep(selectedStep) {}
    void undo() override { m_document->m_xml = m_before; m_document->m_selectedStep = m_beforeStep; emit m_document->changed(); }
    void redo() override { m_document->m_xml = m_after; m_document->m_selectedStep = m_afterStep; emit m_document->changed(); }
private:
    TimelineDocument *m_document;
    QDomDocument m_before, m_after;
    int m_beforeStep, m_afterStep;
};
TimelineDocument::TimelineDocument(QObject *parent) : QObject(parent), m_undo(this) { m_undo.setUndoLimit(100); }
static bool writeTimeline(const QString &path, const QByteArray &bytes, QString &error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { error = file.errorString(); return false; }
    return true;
}
bool TimelineDocument::createEmpty(const QString &path, QString &error)
{
    if (QFileInfo::exists(path)) { error = tr("The timeline file already exists."); return false; }
    return writeTimeline(path, QByteArrayLiteral("<timeline/>\n"), error);
}
QList<QDomElement> TimelineDocument::moments(QDomDocument &xml)
{
    auto result = ActionXml::elements(xml.documentElement(), QStringLiteral("entry"));
    std::stable_sort(result.begin(), result.end(), [](QDomElement a, QDomElement b) { return ActionXml::text(a, QStringLiteral("step")).toInt() < ActionXml::text(b, QStringLiteral("step")).toInt(); });
    return result;
}
QDomElement TimelineDocument::moment(QDomDocument &xml, int step)
{
    for (QDomElement entry : ActionXml::elements(xml.documentElement(), QStringLiteral("entry"))) if (ActionXml::text(entry, QStringLiteral("step")).toInt() == step) return entry;
    return QDomElement();
}
bool TimelineDocument::load(const QString &path, QString &error)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    const QByteArray bytes = file.readAll(); if (file.error() != QFile::NoError) { error = file.errorString(); return false; }
    QDomDocument xml; if (!xml.setContent(bytes, false, &error)) return false;
    if (xml.documentElement().tagName() != QStringLiteral("timeline")) { error = tr("Expected a timeline resource."); return false; }
    QDomElement root = xml.documentElement();
    for (const QDomElement &entry : ActionXml::elements(root, QStringLiteral("entry"))) {
        if (entry.firstChildElement(QStringLiteral("event")).firstChildElement(QStringLiteral("action")).isNull())
            root.removeChild(entry);
    }
    QSet<int> steps;
    for (QDomElement entry : ActionXml::elements(xml.documentElement(), QStringLiteral("entry"))) {
        bool ok = false; const int step = ActionXml::text(entry, QStringLiteral("step")).toInt(&ok);
        if (!ok || step < 0 || steps.contains(step)) { error = tr("Timeline moments must have unique non-negative integer steps."); return false; }
        steps.insert(step);
    }
    m_xml = xml; m_source = bytes; m_path = QFileInfo(path).absoluteFilePath();
    const auto entries = moments(m_xml); m_selectedStep = entries.isEmpty() ? -1 : ActionXml::text(entries.first(), QStringLiteral("step")).toInt();
    m_undo.clear(); emit changed(); return true;
}
QString TimelineDocument::name() const { QString result = QFileInfo(m_path).fileName(); result.chop(QStringLiteral(".timeline.gmx").size()); return result; }
void TimelineDocument::relocate(const QString &oldDirectory, const QString &newDirectory)
{ m_path = QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(m_path)); }
bool TimelineDocument::save(QString &error)
{
    if (!isModified()) return true;
    QFile file(m_path); if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.readAll() != m_source || file.error() != QFile::NoError) { error = tr("The timeline changed outside the editor. Reopen it before saving."); return false; }
    file.close(); const QByteArray bytes = m_xml.toByteArray(2);
    if (!writeTimeline(m_path, bytes, error)) return false;
    m_source = bytes; m_undo.setClean(); emit saved(); return true;
}
void TimelineDocument::edit(const QDomDocument &xml, const QString &description, int selectedStep)
{
    if (xml.toByteArray() == m_xml.toByteArray()) return;
    QDomDocument sorted = xml.cloneNode(true).toDocument();
    // Replace only entry slots, leaving unrelated root metadata in place.
    const auto original = ActionXml::elements(sorted.documentElement(), QStringLiteral("entry")); const auto ordered = moments(sorted);
    for (int i = 0; i < original.size(); ++i) sorted.documentElement().replaceChild(ordered.at(i).cloneNode(true), original.at(i));
    if (sorted.toByteArray() == m_xml.toByteArray()) return;
    m_undo.push(new TimelineChangeCommand(this, sorted, description, selectedStep));
}
bool TimelineDocument::addMoment(int step, QString &error)
{
    QDomDocument xml = this->xml();
    if (step < 0 || !moment(xml, step).isNull()) { error = tr("Choose an unused non-negative step."); return false; }
    auto entry = xml.createElement(QStringLiteral("entry")); ActionXml::setText(entry, QStringLiteral("step"), QString::number(step)); ActionXml::child(entry, QStringLiteral("event"));
    xml.documentElement().appendChild(entry); edit(xml, tr("Add moment"), step); return true;
}
bool TimelineDocument::changeMoment(int oldStep, int newStep, QString &error)
{
    QDomDocument xml = this->xml(); const QDomElement entry = moment(xml, oldStep);
    if (entry.isNull() || newStep < 0 || (oldStep != newStep && !moment(xml, newStep).isNull())) { error = tr("Choose an unused non-negative step."); return false; }
    ActionXml::setText(entry, QStringLiteral("step"), QString::number(newStep)); edit(xml, tr("Change moment"), newStep); return true;
}
static void mergeTimelineMoment(QDomElement target, QDomElement incoming)
{
    const auto attributes = incoming.attributes();
    for (int i = 0; i < attributes.count(); ++i) { const auto attr = attributes.item(i).toAttr(); if (!target.hasAttribute(attr.name())) target.setAttribute(attr.name(), attr.value()); }
    QDomElement event = ActionXml::child(target, QStringLiteral("event"));
    for (QDomNode node = incoming.firstChild(); !node.isNull(); node = node.nextSibling()) {
        if (node.isElement() && node.nodeName() == QStringLiteral("step")) continue;
        if (node.isElement() && node.nodeName() == QStringLiteral("event")) {
            const auto attrs = node.attributes();
            for (int i = 0; i < attrs.count(); ++i) { const auto attr = attrs.item(i).toAttr(); if (!event.hasAttribute(attr.name())) event.setAttribute(attr.name(), attr.value()); }
            for (QDomNode action = node.firstChild(); !action.isNull(); action = action.nextSibling()) event.appendChild(action.cloneNode(true));
        } else target.appendChild(node.cloneNode(true));
    }
}
bool TimelineDocument::transform(MomentOperation operation, int first, int last, double value, QString &error)
{
    if (first < 0 || last < first || !std::isfinite(value)) { error = tr("Invalid moment range."); return false; }
    if (operation == MomentOperation::Spread && value < 0) { error = tr("Spread percentage must be non-negative."); return false; }
    QDomDocument xml = this->xml(); QList<QPair<QDomElement, int>> changed;
    for (QDomElement entry : moments(xml)) {
        const int step = ActionXml::text(entry, QStringLiteral("step")).toInt(); if (step < first || step > last) continue;
        double target = step;
        if (operation == MomentOperation::Shift || operation == MomentOperation::Duplicate) target = step - static_cast<double>(first) + value;
        else if (operation == MomentOperation::Spread) target = first + (step - static_cast<double>(first)) * value / 100.0;
        else if (operation == MomentOperation::Merge) target = first;
        const double rounded = std::floor(target + 0.5);
        if (rounded < 0 || rounded > std::numeric_limits<int>::max()) { error = tr("The operation moves a moment outside the supported step range."); return false; }
        changed.append(qMakePair(entry, static_cast<int>(rounded)));
    }
    if (changed.isEmpty()) { error = tr("There are no moments in this range."); return false; }
    if ((operation == MomentOperation::Shift && value == first) || (operation == MomentOperation::Spread && value == 100)) return true;
    QList<QPair<QDomElement, int>> copies;
    for (const auto &item : changed) copies.append(qMakePair(item.first.cloneNode(true).toElement(), item.second));
    // Remove all sources before inserting destinations, so overlapping ranges
    // cannot change which entries are selected halfway through the operation.
    if (operation != MomentOperation::Duplicate) for (const auto &item : changed) xml.documentElement().removeChild(item.first);
    if (operation != MomentOperation::Delete) {
        for (const auto &item : copies) {
            QDomElement incoming = item.first; ActionXml::setText(incoming, QStringLiteral("step"), QString::number(item.second));
            QDomElement target = moment(xml, item.second);
            if (target.isNull()) xml.documentElement().appendChild(incoming); else mergeTimelineMoment(target, incoming);
        }
    }
    const QStringList descriptions = {tr("Delete moments"), tr("Shift moments"), tr("Duplicate moments"), tr("Spread moments"), tr("Merge moments")};
    int selected = changed.first().second;
    if (operation == MomentOperation::Delete) {
        selected = -1; for (QDomElement entry : moments(xml)) { const int step = ActionXml::text(entry, QStringLiteral("step")).toInt(); selected = step; if (step >= first) break; }
    }
    edit(xml, descriptions.at(static_cast<int>(operation)), selected); return true;
}
void TimelineDocument::clear()
{
    QDomDocument xml = this->xml(); for (QDomElement entry : moments(xml)) xml.documentElement().removeChild(entry);
    edit(xml, tr("Clear timeline"), -1);
}
