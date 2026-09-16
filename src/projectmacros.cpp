#include "project.h"
#include "actionxml.h"
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

static QDomDocument macroSection(const QDomDocument &document, bool global)
{
    const auto root = document.documentElement();
    const auto constants = (global ? root : root.firstChildElement(QStringLiteral("ConfigConstants"))).firstChildElement(QStringLiteral("constants"));
    QDomDocument result;
    result.appendChild(constants.isNull() ? result.createElement(QStringLiteral("constants")) : result.importNode(constants, true));
    // The count belongs to the file format, not the editable macro values.
    // Recompute it on save so repeated edits compare the same document state.
    result.documentElement().removeAttribute(QStringLiteral("number"));
    return result;
}
static bool readMacroScope(const QString &path, QDomDocument &document, QByteArray &bytes, QString &error)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    bytes = file.readAll(); if (file.error() != QFile::NoError) { error = file.errorString(); return false; }
    return document.setContent(bytes, false, &error);
}
bool Project::loadMacros(const QString &scopePath, QDomDocument &macros, QString &error) const
{
    bool known = scopePath == m_filePath;
    for (const auto &config : m_configurations) known = known || config.filePath == scopePath;
    if (!isOpen() || !known) { error = QObject::tr("The macro scope does not belong to the current project."); return false; }
    QDomDocument document; QByteArray bytes; if (!readMacroScope(scopePath, document, bytes, error)) return false;
    const bool global = scopePath == m_filePath;
    if (document.documentElement().tagName() != (global ? QStringLiteral("assets") : QStringLiteral("Config"))) { error = QObject::tr("Invalid project or configuration document."); return false; }
    if (global && bytes != m_sourceBytes) { error = QObject::tr("The project file changed outside the editor. Reopen the project before editing macros."); return false; }
    macros = macroSection(document, global); return true;
}
bool Project::saveMacros(const QString &scopePath, const QDomDocument &before, const QDomDocument &after, QString &error)
{
    int configIndex = -1;
    for (int i = 0; i < m_configurations.size(); ++i) if (m_configurations.at(i).filePath == scopePath) configIndex = i;
    const bool global = scopePath == m_filePath;
    if (!isOpen() || (!global && configIndex < 0)) { error = QObject::tr("The macro scope no longer belongs to this project."); return false; }
    QDomDocument document; QByteArray bytes; if (!readMacroScope(scopePath, document, bytes, error)) return false;
    if (document.documentElement().tagName() != (global ? QStringLiteral("assets") : QStringLiteral("Config"))) { error = QObject::tr("Invalid project or configuration document."); return false; }
    if (global && bytes != m_sourceBytes) { error = QObject::tr("The project file changed outside the editor. Reopen it before saving."); return false; }
    if (macroSection(document, global).toByteArray(2) != before.toByteArray(2)) { error = QObject::tr("These macros changed outside the editor. Reopen the macro editor before saving."); return false; }
    // Read the latest whole document and replace only this scope's constants.
    // Resource creation and unrelated configuration changes stay intact.
    auto parent = global ? document.documentElement() : ActionXml::child(document.documentElement(), QStringLiteral("ConfigConstants"));
    auto constants = parent.firstChildElement(QStringLiteral("constants"));
    const int count = ActionXml::elements(after.documentElement(), QStringLiteral("constant")).size();
    if (count == 0) {
        if (!constants.isNull()) parent.removeChild(constants);
    } else {
        auto updated = document.importNode(after.documentElement(), true).toElement();
        updated.setAttribute(QStringLiteral("number"), count);
        if (constants.isNull()) parent.appendChild(updated); else parent.replaceChild(updated, constants);
    }
    const QByteArray output = document.toByteArray(2); QSaveFile file(scopePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(output) != output.size() || !file.commit()) { error = file.errorString(); return false; }
    QList<ResourceNode> definitions;
    for (auto entry : ActionXml::elements(after.documentElement(), QStringLiteral("constant"))) {
        ResourceNode macro; macro.type = ResourceType::Macro; macro.name = entry.attribute(QStringLiteral("name")); macro.value = entry.text(); definitions.append(macro);
    }
    if (global) { m_resourceCount += definitions.size() - m_resources[ResourceType::Macro].size(); m_resources[ResourceType::Macro] = definitions; m_sourceBytes = output; }
    else { m_resourceCount += definitions.size() - m_configurations.at(configIndex).macros.size(); m_configurations[configIndex].macros = definitions; }
    return true;
}
