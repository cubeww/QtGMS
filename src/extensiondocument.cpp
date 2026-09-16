#include "extensiondocument.h"
#include "actionxml.h"
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QRegExp>

class ExtensionChangeCommand : public QUndoCommand
{
public:
    ExtensionChangeCommand(ExtensionDocument *document, const ExtensionState &after, const QString &description)
        : QUndoCommand(description), m_document(document), m_before(document->state()), m_after(after)
    { m_after.xml = after.xml.cloneNode(true).toDocument(); }
    void undo() override { m_document->m_state = m_before; emit m_document->changed(); }
    void redo() override { m_document->m_state = m_after; emit m_document->changed(); }
private:
    ExtensionDocument *m_document;
    ExtensionState m_before, m_after;
};
static bool writeExtensionFile(const QString &path, const QByteArray &bytes, QString &error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = QObject::tr("Cannot write %1:\n%2").arg(path, file.errorString()); return false;
    }
    return true;
}
static bool readExtensionFile(const QString &path, QByteArray &bytes, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = QObject::tr("Cannot read %1:\n%2").arg(path, file.errorString()); return false; }
    bytes = file.readAll();
    if (file.error() != QFile::NoError) { error = file.errorString(); return false; }
    return true;
}
ExtensionDocument::ExtensionDocument(QObject *parent) : QObject(parent), m_undo(this) { m_undo.setUndoLimit(100); }
bool ExtensionDocument::createEmpty(const QString &path, QString &error)
{
    if (QFileInfo::exists(path)) { error = tr("The extension already exists."); return false; }
    QDomDocument xml; auto root = xml.createElement(QStringLiteral("extension")); xml.appendChild(root);
    QString name = QFileInfo(path).fileName(); name.chop(14);
    ActionXml::setText(root, QStringLiteral("name"), name);
    ActionXml::setText(root, QStringLiteral("version"), QStringLiteral("1.0.0"));
    ActionXml::setText(root, QStringLiteral("date"), QDate::currentDate().toString(QStringLiteral("dd/MM/yy")));
    for (const QString &key : QStringLiteral("packageID|ProductID|license|description|helpfile|installdir|classname|androidclassname|sourcedir|androidsourcedir|macsourcedir|maclinkerflags|maccompilerflags|androidinject|androidmanifestinject|iosplistinject|androidactivityinject|gradleinject").split(QLatin1Char('|')))
        ActionXml::setText(root, key, QString());
    for (const QString &key : QStringLiteral("iosSystemFrameworks|iosThirdPartyFrameworks|androidPermissions|IncludedResources|files").split(QLatin1Char('|'))) ActionXml::child(root, key);
    auto config = xml.createElement(QStringLiteral("Config")); config.setAttribute(QStringLiteral("name"), QStringLiteral("Default"));
    ActionXml::setText(config, QStringLiteral("CopyToMask"), QStringLiteral("9223372036854775807")); ActionXml::child(root, QStringLiteral("ConfigOptions")).appendChild(config);
    return writeExtensionFile(path, xml.toByteArray(2), error);
}
bool ExtensionDocument::load(const QString &path, QString &error)
{
    QByteArray bytes; if (!readExtensionFile(path, bytes, error)) return false;
    QDomDocument xml; if (!xml.setContent(bytes, false, &error)) return false;
    if (xml.documentElement().tagName() != QStringLiteral("extension")) { error = tr("Expected an extension resource."); return false; }
    m_path = QFileInfo(path).absoluteFilePath(); m_source = bytes; m_state = ExtensionState(); m_state.xml = xml;
    m_originalFiles.clear(); m_initialFiles.clear(); m_missingFiles.clear(); m_undo.clear(); return true;
}
QString ExtensionDocument::name() const { QString result = QFileInfo(m_path).fileName(); result.chop(14); return result; }
QString ExtensionDocument::contentDirectory() const { return QFileInfo(m_path).absoluteDir().filePath(name()); }
void ExtensionDocument::relocate(const QString &oldDirectory, const QString &newDirectory)
{ m_path = QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(m_path)); }
ExtensionState ExtensionDocument::state() const
{ ExtensionState copy = m_state; copy.xml = m_state.xml.cloneNode(true).toDocument(); return copy; }
void ExtensionDocument::edit(const ExtensionState &state, const QString &description)
{
    if (state.xml.toByteArray() == m_state.xml.toByteArray() && state.files == m_state.files) return;
    m_undo.push(new ExtensionChangeCommand(this, state, description));
}
bool ExtensionDocument::validFileName(const QString &name)
{
    const QString normalized = QString(name).replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (normalized.isEmpty() || QDir::isAbsolutePath(normalized) || normalized.contains(QLatin1Char(':'))) return false;
    for (const QString &part : normalized.split(QLatin1Char('/'))) if (part.isEmpty() || part == QStringLiteral("..") || part == QStringLiteral(".")) return false;
    return !normalized.contains(QRegExp(QStringLiteral("[<>\"|?*]")));
}
int ExtensionDocument::fileKind(const QString &name)
{
    const QString suffix = QFileInfo(name).suffix().toLower();
    if (suffix == QStringLiteral("gml")) return 2;
    if (suffix == QStringLiteral("js")) return 5;
    if (suffix == QStringLiteral("lib")) return 3;
    if (QStringLiteral("dll|so|dylib|prx|suprx").split(QLatin1Char('|')).contains(suffix)) return 1;
    return 4;
}
bool ExtensionDocument::fileData(const QString &name, QByteArray &bytes, QString &error)
{
    if (!validFileName(name)) { error = tr("Invalid extension file path: %1").arg(name); return false; }
    if (m_state.files.contains(name)) { bytes = m_state.files.value(name); return true; }
    if (m_initialFiles.contains(name)) { bytes = m_initialFiles.value(name); return true; }
    if (!readExtensionFile(QDir(contentDirectory()).filePath(QString(name).replace(QLatin1Char('\\'), QLatin1Char('/'))), bytes, error)) return false;
    if (!m_originalFiles.contains(name) && !m_missingFiles.contains(name)) m_originalFiles.insert(name, bytes);
    m_initialFiles.insert(name, bytes);
    return true;
}
bool ExtensionDocument::stageFile(ExtensionState &state, const QString &name, const QByteArray &bytes, QString &error)
{
    if (!validFileName(name)) { error = tr("Invalid extension file path: %1").arg(name); return false; }
    const QString normalized = QString(name).replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded();
    for (auto it = state.files.constBegin(); it != state.files.constEnd(); ++it) {
        if (it.key() != name && QString(it.key()).replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded() == normalized) {
            error = tr("Another extension file uses the same filename: %1").arg(it.key()); return false;
        }
    }
    if (!m_originalFiles.contains(name) && !m_missingFiles.contains(name)) {
        const QString path = QDir(contentDirectory()).filePath(QString(name).replace(QLatin1Char('\\'), QLatin1Char('/')));
        if (QFileInfo::exists(path)) { QByteArray original; if (!readExtensionFile(path, original, error)) return false; m_originalFiles.insert(name, original); m_initialFiles.insert(name, original); }
        else m_missingFiles.insert(name);
    }
    state.files.insert(name, bytes); return true;
}
bool ExtensionDocument::save(QString &error)
{
    if (!isModified()) return true;
    QByteArray current;
    if (!readExtensionFile(m_path, current, error)) return false;
    if (current != m_source) { error = tr("The extension changed outside the editor. Reopen it before saving."); return false; }
    QMap<QString, QByteArray> desired = m_initialFiles;
    for (auto it = m_state.files.constBegin(); it != m_state.files.constEnd(); ++it) desired.insert(it.key(), it.value());
    QSet<QString> referenced;
    for (auto file : ActionXml::elements(m_state.xml.documentElement().firstChildElement(QStringLiteral("files")), QStringLiteral("file"))) {
        referenced.insert(ActionXml::text(file, QStringLiteral("filename")));
        for (auto proxy : ActionXml::elements(file.firstChildElement(QStringLiteral("ProxyFiles")), QStringLiteral("ProxyFile")))
            referenced.insert(ActionXml::text(proxy, QStringLiteral("Name")));
    }
    for (auto it = desired.begin(); it != desired.end();) {
        if (!referenced.contains(it.key())) it = desired.erase(it); else ++it;
    }
    QMap<QString, QByteArray> previous; QSet<QString> missing;
    for (auto it = desired.constBegin(); it != desired.constEnd(); ++it) {
        const QString path = QDir(contentDirectory()).filePath(QString(it.key()).replace(QLatin1Char('\\'), QLatin1Char('/')));
        if (QFileInfo::exists(path)) {
            QByteArray bytes; if (!readExtensionFile(path, bytes, error)) return false;
            if (m_missingFiles.contains(it.key()) || bytes != m_originalFiles.value(it.key())) { error = tr("The extension file changed outside the editor: %1").arg(it.key()); return false; }
            previous.insert(path, bytes);
        } else {
            if (!m_missingFiles.contains(it.key())) { error = tr("The extension file was removed outside the editor: %1").arg(it.key()); return false; }
            missing.insert(path);
        }
    }
    QStringList written;
    auto rollback = [&] {
        for (const QString &path : written) {
            QString failure;
            if (missing.contains(path)) { if (!QFile::remove(path)) error += tr("\nCannot remove partially saved file: %1").arg(path); }
            else if (!writeExtensionFile(path, previous.value(path), failure)) error += QLatin1Char('\n') + failure;
        }
    };
    for (auto it = desired.constBegin(); it != desired.constEnd(); ++it) {
        const QString path = QDir(contentDirectory()).filePath(QString(it.key()).replace(QLatin1Char('\\'), QLatin1Char('/')));
        if (previous.contains(path) && previous.value(path) == it.value()) continue;
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) { error = tr("Cannot create the extension file directory."); rollback(); return false; }
        if (!writeExtensionFile(path, it.value(), error)) { rollback(); return false; }
        written.append(path);
    }
    const QByteArray bytes = m_state.xml.toByteArray(2);
    if (!writeExtensionFile(m_path, bytes, error)) { rollback(); return false; }
    m_source = bytes;
    for (auto it = desired.constBegin(); it != desired.constEnd(); ++it) { m_originalFiles.insert(it.key(), it.value()); m_missingFiles.remove(it.key()); }
    m_undo.setClean(); emit saved(); return true;
}
