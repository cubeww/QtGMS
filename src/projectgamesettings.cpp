#include "project.h"
#include "actionxml.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

static bool readSettingsXml(const QString &path, QDomDocument &xml, QByteArray &bytes, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    bytes = file.readAll();
    if (file.error() != QFile::NoError) { error = file.errorString(); return false; }
    return xml.setContent(bytes, false, &error);
}
static bool writeSettingsFile(const QString &path, const QByteArray &bytes, QString &error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = QObject::tr("Cannot write %1:\n%2").arg(path, file.errorString()); return false;
    }
    return true;
}
bool Project::saveGameSettings(const QString &path, const QMap<QString, QString> &before,
    const QMap<QString, QString> &after, const QStringList &audioBefore, const QStringList &audioAfter,
    const QMap<QString, QByteArray> &assets, QString &error)
{
    int index = -1;
    for (int i = 0; i < m_configurations.size(); ++i) if (m_configurations.at(i).filePath == path) index = i;
    if (index < 0) { error = QObject::tr("This configuration no longer belongs to the project."); return false; }
    QDomDocument config; QByteArray bytes;
    if (!readSettingsXml(path, config, bytes, error)) return false;
    if (config.documentElement().tagName() != QStringLiteral("Config")) { error = QObject::tr("Invalid configuration file."); return false; }
    auto options = ActionXml::child(config.documentElement(), QStringLiteral("Options"));
    QMap<QString, QString> current;
    for (auto node = options.firstChildElement(); !node.isNull(); node = node.nextSiblingElement()) current[node.tagName()] = node.text();
    QStringList keys = before.keys();
    for (const auto &key : after.keys()) if (!keys.contains(key)) keys.append(key);
    for (const auto &key : keys) {
        if (before.contains(key) == after.contains(key) && before.value(key) == after.value(key)) continue;
        if (current.contains(key) != before.contains(key) || current.value(key) != before.value(key)) {
            error = QObject::tr("The option %1 changed outside this editor. Reopen Global Game Settings before saving.").arg(key); return false;
        }
        if (after.contains(key)) { ActionXml::setText(options, key, after.value(key)); current[key] = after.value(key); }
        else { options.removeChild(options.firstChildElement(key)); current.remove(key); }
    }
    QMap<QString, QByteArray> writes;
    const QDir directory = QFileInfo(m_filePath).absoluteDir();
    for (auto it = assets.cbegin(); it != assets.cend(); ++it) {
        const QString absolute = QDir::cleanPath(directory.absoluteFilePath(it.key()));
        if (!absolute.startsWith(directory.absolutePath() + QLatin1Char('/'), Qt::CaseInsensitive)
            || !it.key().startsWith(QStringLiteral("Configs/")) || absolute == path || absolute == m_filePath) {
            error = QObject::tr("Invalid settings asset path: %1").arg(it.key()); return false;
        }
        writes[absolute] = it.value();
    }
    writes[path] = config.toByteArray(2);
    QByteArray manifestBytes;
    if (audioBefore != audioAfter) {
        if (audioBefore != m_audioGroups) { error = QObject::tr("Audio groups changed in another settings window. Reopen this window."); return false; }
        QDomDocument manifest; QByteArray original;
        if (!readSettingsXml(m_filePath, manifest, original, error)) return false;
        if (original != m_sourceBytes) { error = QObject::tr("The project file changed outside the editor. Reopen the project before saving."); return false; }
        auto groups = ActionXml::child(manifest.documentElement(), QStringLiteral("audiogroups"));
        // Preserve attributes and any unedited group metadata.
        auto entries = ActionXml::elements(groups, QStringLiteral("audiogroup"));
        for (int i = 0; i < audioAfter.size(); ++i) {
            auto entry = i < entries.size() ? entries.at(i) : manifest.createElement(QStringLiteral("audiogroup"));
            entry.setAttribute(QStringLiteral("name"), audioAfter.at(i));
            if (i >= entries.size()) groups.appendChild(entry);
        }
        for (int i = audioAfter.size(); i < entries.size(); ++i) groups.removeChild(entries.at(i));
        manifestBytes = manifest.toByteArray(2); writes[m_filePath] = manifestBytes;
    }
    QMap<QString, QByteArray> originals; QStringList existing, committed;
    // Read all originals before any writes so a failure can restore completed files.
    for (auto it = writes.cbegin(); it != writes.cend(); ++it) {
        if (!QFileInfo::exists(it.key())) continue;
        QFile file(it.key());
        if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
        originals[it.key()] = file.readAll();
        if (file.error() != QFile::NoError) { error = file.errorString(); return false; }
        existing.append(it.key());
    }
    for (auto it = writes.cbegin(); it != writes.cend(); ++it) {
        if (!QDir().mkpath(QFileInfo(it.key()).absolutePath())) error = QObject::tr("Cannot create settings asset directory.");
        else if (writeSettingsFile(it.key(), it.value(), error)) { committed.append(it.key()); continue; }
        for (const auto &written : committed) {
            QString restoreError;
            if (existing.contains(written)) {
                if (!writeSettingsFile(written, originals.value(written), restoreError)) error += QLatin1Char('\n') + restoreError;
            } else if (!QFile::remove(written)) error += QObject::tr("\nCannot remove incomplete asset: %1").arg(written);
        }
        return false;
    }
    m_configurations[index].options = current;
    if (audioBefore != audioAfter) { m_audioGroups = audioAfter; m_sourceBytes = manifestBytes; }
    return true;
}
