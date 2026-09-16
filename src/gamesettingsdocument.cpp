#include "gamesettingsdocument.h"
#include "actionxml.h"
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QRegExp>
#include <QSet>
GameSettingsDocument::GameSettingsDocument(Project *project, QObject *parent) : QObject(parent), m_project(project) {}
bool GameSettingsDocument::load(const QString &path, QString &error)
{
    bool found = false;
    for (const auto &config : m_project->configurations()) if (config.filePath == path) { m_name = config.name; found = true; }
    if (!found) { error = tr("Unknown project configuration."); return false; }
    QFile file(path); QDomDocument xml;
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (!xml.setContent(&file, false, &error)) return false;
    if (xml.documentElement().tagName() != QStringLiteral("Config")) { error = tr("Invalid configuration file."); return false; }
    m_path = path; m_options.clear(); m_assets.clear();
    const auto options = xml.documentElement().firstChildElement(QStringLiteral("Options"));
    for (auto node = options.firstChildElement(); !node.isNull(); node = node.nextSiblingElement()) m_options[node.tagName()] = node.text();
    m_before = m_options; m_audioBefore = m_audioGroups = m_project->audioGroups(); return true;
}
QString GameSettingsDocument::value(const QString &key, const QString &fallback) const { return m_options.value(QStringLiteral("option_") + key, fallback); }
void GameSettingsDocument::setValue(const QString &key, const QString &value)
{
    const QString tag = QStringLiteral("option_") + key;
    if (m_options.contains(tag) && m_options.value(tag) == value) return;
    m_options[tag] = value; emit changed();
}
bool GameSettingsDocument::isModified() const { return m_before != m_options || m_audioBefore != m_audioGroups || !m_assets.isEmpty(); }
bool GameSettingsDocument::save(QString &error)
{
    if (!isModified()) return true;
    if (!m_project->saveGameSettings(m_path, m_before, m_options, m_audioBefore, m_audioGroups, m_assets, error)) return false;
    // Refresh the baseline, including unrelated options saved by other editors.
    for (const auto &config : m_project->configurations()) if (config.filePath == m_path) m_options = config.options;
    m_before = m_options; m_audioBefore = m_audioGroups; m_assets.clear(); emit changed(); return true;
}
QStringList GameSettingsDocument::groups(bool audio) const
{
    if (audio) return m_audioGroups;
    QStringList result;
    const int count = qBound(1, value(QStringLiteral("textureGroup_count"), QStringLiteral("1")).toInt(), 4096);
    for (int i = 0; i < count; ++i) result.append(value(QStringLiteral("textureGroups%1").arg(i), i == 0 ? QStringLiteral("Default") : QString::number(i)));
    return result;
}
static bool validGroupName(const QStringList &groups, int index, const QString &name, QString &error)
{
    if (!QRegExp(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*")).exactMatch(name)) { error = QObject::tr("Use letters, digits and underscores; start with a letter or underscore."); return false; }
    for (int i = 0; i < groups.size(); ++i) if (i != index && groups.at(i).compare(name, Qt::CaseInsensitive) == 0) { error = QObject::tr("A group with that name already exists."); return false; }
    return true;
}
bool GameSettingsDocument::renameGroup(bool audio, int index, const QString &name, QString &error)
{
    const auto names = groups(audio);
    if (index <= 0 || index >= names.size()) { error = tr("The default group cannot be renamed."); return false; }
    if (!validGroupName(names, index, name, error)) return false;
    if (audio) { m_audioGroups[index] = name; emit changed(); }
    else {
        for (int i = 0; i < names.size(); ++i) {
            const QString key = QStringLiteral("textureGroup%1_parent").arg(i);
            if (value(key) == names.at(index)) setValue(key, name);
        }
        setValue(QStringLiteral("textureGroups%1").arg(index), name);
    }
    return true;
}
QStringList GameSettingsDocument::textureParents(int index) const
{
    const auto names = groups(false); QStringList parents = {QStringLiteral("<none>")};
    for (int i = 0; i < names.size(); ++i) {
        int ancestor = i; QSet<int> visited;
        while (ancestor >= 0 && ancestor != index && !visited.contains(ancestor)) {
            visited.insert(ancestor); ancestor = names.indexOf(value(QStringLiteral("textureGroup%1_parent").arg(ancestor)));
        }
        if (ancestor < 0) parents.append(names.at(i));
    }
    return parents;
}
bool GameSettingsDocument::addGroup(bool audio, const QString &name, QString &error)
{
    const auto names = groups(audio);
    if (names.size() >= 4096 || !validGroupName(names, -1, name, error)) { if (error.isEmpty()) error = tr("Too many groups."); return false; }
    if (audio) { m_audioGroups.append(name); setValue(QStringLiteral("audioGroupCount"), QString::number(m_audioGroups.size())); }
    else {
        setValue(QStringLiteral("textureGroups%1").arg(names.size()), name);
        setValue(QStringLiteral("textureGroup_count"), QString::number(names.size() + 1));
        setValue(QStringLiteral("textureGroup%1_border").arg(names.size()), QStringLiteral("2"));
        setValue(QStringLiteral("textureGroup%1_parent").arg(names.size()), QStringLiteral("<none>"));
        setValue(QStringLiteral("textureGroup%1_nocropping").arg(names.size()), QStringLiteral("false"));
        setValue(QStringLiteral("textureGroup%1_scaled").arg(names.size()), QStringLiteral("false"));
    }
    setValue(QStringLiteral("%1Group%2_targets").arg(audio ? QStringLiteral("audio") : QStringLiteral("texture")).arg(names.size()), QStringLiteral("$7fffffffffffffff"));
    emit changed(); return true;
}
bool GameSettingsDocument::importAsset(const QString &key, const QString &source, const QString &fileName, QString &error)
{
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > 32 * 1024 * 1024) { error = tr("Settings assets must be smaller than 32 MB."); return false; }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFile::NoError) { error = file.errorString(); return false; }
    setAssetBytes(key, fileName, bytes); return true;
}
void GameSettingsDocument::setAssetBytes(const QString &key, const QString &fileName, const QByteArray &bytes)
{
    const QString relative = QStringLiteral("Configs/%1/windows/%2").arg(QFileInfo(m_path).fileName().section(QStringLiteral(".config.gmx"), 0, 0), fileName);
    m_assets[relative] = bytes; setValue(key, QString(relative).replace(QLatin1Char('/'), QLatin1Char('\\'))); emit changed();
}
QByteArray GameSettingsDocument::assetBytes(const QString &key) const
{
    QString relative = value(key); relative.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (m_assets.contains(relative)) return m_assets.value(relative);
    QFile file(QFileInfo(m_project->filePath()).absoluteDir().absoluteFilePath(relative));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024) return QByteArray();
    return file.readAll();
}
void GameSettingsDocument::relocate(const QString &oldDirectory, const QString &newDirectory)
{ m_path = QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(m_path)); }

QList<ResourceNode> GameSettingsDocument::groupContents(bool audio, int index) const
{
    int configIndex = 0;
    for (int i = 0; i < m_project->configurations().size(); ++i) if (m_project->configurations().at(i).filePath == m_path) configIndex = i;
    QList<ResourceNode> result;
    const QList<ResourceType> types = audio ? QList<ResourceType>{ResourceType::Sound} : QList<ResourceType>{ResourceType::Sprite, ResourceType::Background, ResourceType::Font};
    for (auto type : types) for (const auto &resource : ActionXml::resourceList(*m_project, type)) {
        QFile file(resource.filePath); QDomDocument xml;
        if (!file.open(QIODevice::ReadOnly) || !xml.setContent(&file)) continue;
        auto root = xml.documentElement(); int group = 0;
        if (audio) group = root.firstChildElement(QStringLiteral("audioGroup")).text().toInt();
        else {
            const auto groups = root.firstChildElement(type == ResourceType::Font ? QStringLiteral("texgroups") : QStringLiteral("TextureGroups"));
            const QString prefix = type == ResourceType::Font ? QStringLiteral("texgroup") : QStringLiteral("TextureGroup");
            auto value = groups.firstChildElement(prefix + QString::number(configIndex));
            if (value.isNull()) value = groups.firstChildElement(prefix + QStringLiteral("0"));
            group = value.text().toInt();
        }
        if (group == index) result.append(resource);
    }
    return result;
}
