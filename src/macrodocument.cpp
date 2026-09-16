#include "macrodocument.h"
#include "project.h"
#include "actionxml.h"
#include <QDir>
#include <QFileInfo>
#include <QRegExp>
#include <QSet>
class MacroChangeCommand : public QUndoCommand
{
public:
    MacroChangeCommand(MacroDocument *document, const QDomDocument &after, const QString &description)
        : QUndoCommand(description), m_document(document), m_before(document->xml()), m_after(after.cloneNode(true).toDocument()) {}
    void undo() override { m_document->m_xml = m_before; emit m_document->changed(); }
    void redo() override { m_document->m_xml = m_after; emit m_document->changed(); }
private:
    MacroDocument *m_document;
    QDomDocument m_before, m_after;
};
MacroDocument::MacroDocument(Project *project, QObject *parent) : QObject(parent), m_project(project), m_undo(this) { m_undo.setUndoLimit(100); }
bool MacroDocument::load(const QString &scopePath, QString &error)
{
    QDomDocument xml; if (!m_project->loadMacros(scopePath, xml, error)) return false;
    m_path = scopePath; m_global = scopePath == m_project->filePath(); m_xml = xml; m_saved = xml.cloneNode(true).toDocument(); m_undo.clear(); return true;
}
QString MacroDocument::scopeName() const
{
    if (m_global) return tr("All Configurations");
    for (const auto &config : m_project->configurations()) if (config.filePath == m_path) return config.name;
    return QFileInfo(m_path).completeBaseName();
}
void MacroDocument::relocate(const QString &oldDirectory, const QString &newDirectory)
{ m_path = m_global ? m_project->filePath() : QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(m_path)); }
void MacroDocument::edit(const QDomDocument &xml, const QString &description)
{ if (xml.toByteArray(2) != m_xml.toByteArray(2)) m_undo.push(new MacroChangeCommand(this, xml, description)); }
bool MacroDocument::validate(const QDomDocument &xml, QString &error)
{
    QSet<QString> names; int row = 0;
    for (auto entry : ActionXml::elements(xml.documentElement(), QStringLiteral("constant"))) {
        ++row; const QString name = entry.attribute(QStringLiteral("name"));
        if (!QRegExp(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*")).exactMatch(name)) { error = tr("Row %1: use letters, numbers and underscores; the name cannot start with a number.").arg(row); return false; }
        if (names.contains(name)) { error = tr("Row %1: duplicate macro name '%2'.").arg(row).arg(name); return false; }
        if (entry.text().trimmed().isEmpty()) { error = tr("Row %1: enter a value or expression for '%2'.").arg(row).arg(name); return false; }
        names.insert(name);
    }
    return true;
}
bool MacroDocument::save(QString &error)
{
    if (!validate(m_xml, error)) return false;
    if (!isModified()) return true;
    if (!m_project->saveMacros(m_path, m_saved, m_xml, error)) return false;
    m_saved = m_xml.cloneNode(true).toDocument(); m_undo.setClean(); emit saved(); return true;
}
