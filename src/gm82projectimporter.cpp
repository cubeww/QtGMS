#include "gm82projectimporter.h"
#include "actionxml.h"
#include "sevenziparchive.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextCodec>

QString Gm82Properties::text(const QString &key, const QString &defaultValue) const
{
    return values.value(key, defaultValue);
}

qint64 Gm82Properties::integer(const QString &key, qint64 defaultValue) const
{
    if (!values.contains(key)) return defaultValue;
    bool ok;
    const qint64 value = values.value(key).toLongLong(&ok);
    if (!ok || value < -2147483648LL || value > 4294967295LL)
        throw QObject::tr("%1: Invalid integer for %2.").arg(path, key);
    return value;
}

bool Gm82Properties::boolean(const QString &key, bool defaultValue) const
{
    return integer(key, defaultValue ? 1 : 0) != 0;
}

Gm82Properties Gm82Properties::parse(const QString &text, const QString &path)
{
    Gm82Properties result;
    result.path = path;
    int number = 0;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        ++number;
        if (line.isEmpty()) continue;
        const int separator = line.indexOf(QLatin1Char('='));
        if (separator <= 0) throw QObject::tr("%1:%2: Expected key=value.").arg(path).arg(number);
        result.values.insert(line.left(separator), line.mid(separator + 1));
    }
    return result;
}

Gm82ProjectImporter::Gm82ProjectImporter(const QString &source, const QString &manifest,
    const std::function<bool()> &cancelled, const std::function<void(const QString &, int)> &progress)
    : m_source(QFileInfo(source).fileName()), m_manifestPath(QFileInfo(manifest).fileName()),
      m_sourceDirectory(QFileInfo(source).canonicalPath()), m_destination(QFileInfo(manifest).absoluteDir()),
      m_cancelled(cancelled), m_progress(progress)
{
}

void Gm82ProjectImporter::checkCancelled() const
{
    if (m_cancelled()) throw QString();
}

QString Gm82ProjectImporter::sourcePath(const QString &path) const
{
    checkCancelled();
    if (!SevenZipArchive::validPath(path)) throw QObject::tr("Invalid GM82 project path: %1").arg(path);
    const QFileInfo file(m_sourceDirectory.filePath(path));
    const QString resolved = file.canonicalFilePath();
    if (!file.isFile()) throw QObject::tr("Cannot read GM82 project file: %1").arg(file.absoluteFilePath());
    if (!SevenZipArchive::validPath(m_sourceDirectory.relativeFilePath(resolved)))
        throw QObject::tr("GM82 project file points outside the project directory: %1").arg(path);
    return resolved;
}

QString Gm82ProjectImporter::readText(const QString &path) const
{
    QFile file(sourcePath(path));
    if (!file.open(QIODevice::ReadOnly)) throw QObject::tr("%1: %2").arg(path, file.errorString());
    if (file.size() > 64 * 1024 * 1024) throw QObject::tr("GM82 text file exceeds 64 MiB: %1").arg(path);
    const QByteArray bytes = file.readAll();
    if (file.error() != QFile::NoError) throw QObject::tr("%1: %2").arg(path, file.errorString());
    QTextCodec::ConverterState state;
    QString text = QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &state);
    if (state.invalidChars || state.remainingChars) throw QObject::tr("GM82 text is not valid UTF-8: %1").arg(path);
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    return text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
}

Gm82Properties Gm82ProjectImporter::readProperties(const QString &path) const
{
    return Gm82Properties::parse(readText(path), path);
}

void Gm82ProjectImporter::write(const QString &path, const QByteArray &data) const
{
    checkCancelled();
    if (!SevenZipArchive::validPath(path)) throw QObject::tr("Invalid GM82 project path: %1").arg(path);
    const QString target = m_destination.filePath(path);
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) throw QObject::tr("Cannot create directory for %1.").arg(path);
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        throw QObject::tr("Cannot write %1: %2").arg(path, file.errorString());
}

void Gm82ProjectImporter::copy(const QString &source, const QString &destination) const
{
    if (!SevenZipArchive::validPath(destination)) throw QObject::tr("Invalid GM82 project path: %1").arg(destination);
    QFile input(sourcePath(source));
    if (!input.open(QIODevice::ReadOnly)) throw QObject::tr("%1: %2").arg(source, input.errorString());
    const QString target = m_destination.filePath(destination);
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) throw QObject::tr("Cannot create directory for %1.").arg(destination);
    QSaveFile output(target);
    if (!output.open(QIODevice::WriteOnly)) throw output.errorString();
    while (!input.atEnd()) {
        checkCancelled();
        const QByteArray bytes = input.read(1024 * 1024);
        if (input.error() != QFile::NoError) throw QObject::tr("%1: %2").arg(source, input.errorString());
        if (output.write(bytes) != bytes.size()) throw output.errorString();
    }
    if (!output.commit()) throw output.errorString();
}

void Gm82ProjectImporter::validateName(const QString &name) const
{
    if (!SevenZipArchive::validPath(name) || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        throw QObject::tr("Invalid resource filename: %1").arg(name);
}

QString Gm82ProjectImporter::reference(const QString &group, const QString &name) const
{
    if (name.isEmpty()) return QStringLiteral("<undefined>");
    const auto table = m_indexes.constFind(group);
    const int index = table == m_indexes.cend() ? -1 : table->value(name.toCaseFolded(), -1);
    if (index < 0) throw QObject::tr("Unknown GM82 resource: %1/%2").arg(group, name);
    return m_names.value(group).at(index);
}

QDomDocument Gm82ProjectImporter::document(const QString &tag)
{
    QDomDocument xml;
    xml.appendChild(xml.createElement(tag));
    return xml;
}

QString Gm82ProjectImporter::undelimit(QString value)
{
    return value.replace(QStringLiteral("*\\/"), QStringLiteral("*/"))
        .replace(QStringLiteral("\\n"), QStringLiteral("\n"))
        .replace(QStringLiteral("\\r"), QStringLiteral("\r"))
        .replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
}

void Gm82ProjectImporter::fields(QDomElement parent, const Gm82Properties &properties,
    const QString &mapping, bool boolean, bool attributes)
{
    // source:destination pairs; absent properties retain the GMX default.
    for (const QString &pair : mapping.split(QLatin1Char('|'))) {
        const QString source = pair.section(QLatin1Char(':'), 0, 0);
        const QString target = pair.contains(QLatin1Char(':')) ? pair.section(QLatin1Char(':'), 1) : source;
        if (!properties.values.contains(source)) continue;
        const QString value = boolean ? (properties.boolean(source) ? QStringLiteral("-1") : QStringLiteral("0"))
            : QString::number(qint32(properties.integer(source)));
        if (attributes) parent.setAttribute(target, value);
        else ActionXml::setText(parent, target, value);
    }
}

void Gm82ProjectImporter::readIndexes(const Gm82Properties &header)
{
    for (const QString &group : QStringLiteral("sprites|sounds|backgrounds|paths|scripts|fonts|timelines|objects|rooms|triggers|datafiles").split(QLatin1Char('|'))) {
        if (group != QStringLiteral("rooms") && !header.boolean(QStringLiteral("has_") + group, true)) continue;
        QStringList names = readText(group + QStringLiteral("/index.yyd")).split(QLatin1Char('\n'));
        if (!names.isEmpty() && names.last().isEmpty()) names.removeLast();
        if (names.size() > 100000) throw QObject::tr("Too many GM82 resources in %1.").arg(group);
        auto &indexes = m_indexes[group];
        for (int i = 0; i < names.size(); ++i) {
            const QString &name = names.at(i);
            if (name.isEmpty()) continue;
            validateName(name);
            if (indexes.contains(name.toCaseFolded())) throw QObject::tr("Duplicate GM82 resource: %1/%2").arg(group, name);
            indexes.insert(name.toCaseFolded(), i);
        }
        m_names.insert(group, names);
    }
}

void Gm82ProjectImporter::readTree(const QString &group, const QString &tag, const QString &directory)
{
    auto root = ActionXml::child(m_manifest.documentElement(), group);
    while (!root.firstChild().isNull()) root.removeChild(root.firstChild());
    root.setAttribute(QStringLiteral("name"), directory);
    if (m_indexes.value(group).isEmpty()) return;
    QSet<QString> added;
    QVector<QDomElement> stack;
    stack.append(root);
    const auto addResource = [&](QDomElement parent, const QString &name) {
        if (added.contains(name)) throw QObject::tr("Duplicate GM82 tree entry: %1/%2").arg(group, name);
        auto node = m_manifest.createElement(tag);
        node.appendChild(m_manifest.createTextNode(directory + QLatin1Char('\\') + name
            + (tag == QStringLiteral("script") ? QStringLiteral(".gml") : QString())));
        parent.appendChild(node);
        added.insert(name);
        if (group == QStringLiteral("rooms")) m_roomOrder.append(name);
    };
    for (const QString &line : readText(group + QStringLiteral("/tree.yyd")).split(QLatin1Char('\n'))) {
        checkCancelled();
        if (line.isEmpty()) continue;
        int depth = 0;
        while (depth < line.size() && line.at(depth).isSpace()) ++depth;
        if (depth >= line.size() || depth >= stack.size() || depth > 128)
            throw QObject::tr("Invalid GM82 resource tree: %1").arg(group);
        stack.resize(depth + 1);
        const QChar type = line.at(depth);
        const QString name = line.mid(depth + 1);
        if (type == QLatin1Char('+')) {
            auto node = m_manifest.createElement(group);
            node.setAttribute(QStringLiteral("name"), name);
            stack.last().appendChild(node);
            stack.append(node);
        } else if (type == QLatin1Char('|')) addResource(stack.last(), reference(group, name));
        else throw QObject::tr("Invalid GM82 resource tree: %1").arg(group);
    }
    for (const QString &name : m_names.value(group))
        if (!name.isEmpty() && !added.contains(name)) addResource(root, name);
}

void Gm82ProjectImporter::importProject()
{
    const auto header = readProperties(m_source);
    const int version = int(header.integer(QStringLiteral("gm82_version")));
    if (version < 1 || version > 6) throw QObject::tr("Unsupported GM82 format version: %1.").arg(version);
    for (const QString &path : {m_manifestPath, QStringLiteral("Configs/Default.config.gmx")}) {
        QFile file(m_destination.filePath(path));
        if (!file.open(QIODevice::ReadOnly)) throw file.errorString();
        QString error;
        QDomDocument &xml = path == m_manifestPath ? m_manifest : m_configuration;
        if (!xml.setContent(&file, false, &error)) throw error;
    }
    readIndexes(header);
    m_progress(QObject::tr("Game settings"), 2);
    readSettings(header);
    readTriggers();
    readLibraries();
    const QStringList groups = QStringLiteral("sprites|sounds|backgrounds|paths|scripts|fonts|timelines|objects|rooms").split(QLatin1Char('|'));
    for (const QString &group : groups) {
        const QString tag = group.left(group.size() - 1);
        readTree(group, tag, group == QStringLiteral("sounds") || group == QStringLiteral("backgrounds") ? tag : group);
    }
    int total = 0, completed = 0;
    for (const QString &group : groups) total += m_indexes.value(group).size();
    for (const QString &group : groups) {
        const QString tag = group.left(group.size() - 1);
        const QString directory = group == QStringLiteral("sounds") || group == QStringLiteral("backgrounds") ? tag : group;
        for (const QString &name : m_names.value(group)) {
            if (name.isEmpty()) continue;
            checkCancelled();
            m_progress(name, 5 + completed++ * 85 / qMax(1, total));
            QDomDocument xml;
            try {
                if (tag == QStringLiteral("script")) {
                    write(group + QLatin1Char('/') + name + QStringLiteral(".gml"), readText(group + QLatin1Char('/') + name + QStringLiteral(".gml")).toUtf8());
                    continue;
                }
                if (tag == QStringLiteral("sprite")) xml = readSprite(name);
                else if (tag == QStringLiteral("background")) xml = readBackground(name);
                else if (tag == QStringLiteral("sound")) xml = readSound(name);
                else if (tag == QStringLiteral("font")) xml = readFont(name);
                else if (tag == QStringLiteral("path")) xml = readPath(name);
                else if (tag == QStringLiteral("object")) xml = readObject(name);
                else if (tag == QStringLiteral("timeline")) xml = readTimeline(name);
                else if (tag == QStringLiteral("room")) xml = readRoom(name);
                write(directory + QLatin1Char('/') + name + QLatin1Char('.') + tag + QStringLiteral(".gmx"), xml.toByteArray(2));
            } catch (const QString &error) {
                throw group + QLatin1Char('/') + name + QStringLiteral(": ") + error;
            }
        }
    }
    m_progress(QObject::tr("Included files"), 91);
    readIncludedFiles();
    write(QStringLiteral("Configs/Default.config.gmx"), m_configuration.toByteArray(2));
    write(m_manifestPath, m_manifest.toByteArray(2));
    m_progress(QObject::tr("Reading project..."), 95);
}
