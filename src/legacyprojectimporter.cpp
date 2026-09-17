#include "legacyprojectimporter.h"
#include "actionxml.h"
#include "sevenziparchive.h"

#include <QFileInfo>
#include <QObject>
#include <QSaveFile>
#include <QUuid>
#include <QtEndian>

struct LegacyResourceFormat
{
    LegacyResourceKind kind;
    const char *group;
    const char *tag;
    const char *directory;
    const char *suffix;
};

static const LegacyResourceFormat LegacyFormats[] = {
    {LegacyResourceKind::Sprite, "sprites", "sprite", "sprites", ".sprite.gmx"},
    {LegacyResourceKind::Sound, "sounds", "sound", "sound", ".sound.gmx"},
    {LegacyResourceKind::Background, "backgrounds", "background", "background", ".background.gmx"},
    {LegacyResourceKind::Path, "paths", "path", "paths", ".path.gmx"},
    {LegacyResourceKind::Script, "scripts", "script", "scripts", ".gml"},
    {LegacyResourceKind::Font, "fonts", "font", "fonts", ".font.gmx"},
    {LegacyResourceKind::Timeline, "timelines", "timeline", "timelines", ".timeline.gmx"},
    {LegacyResourceKind::Object, "objects", "object", "objects", ".object.gmx"},
    {LegacyResourceKind::Room, "rooms", "room", "rooms", ".room.gmx"}
};

static const LegacyResourceFormat *legacyFormat(LegacyResourceKind kind)
{
    for (const auto &format : LegacyFormats) if (format.kind == kind) return &format;
    return nullptr;
}

LegacyProjectImporter::LegacyProjectImporter(const QString &source, const QString &manifest, const QByteArray &legacyTextEncoding,
    const std::function<bool()> &cancelled, const std::function<void(const QString &, int)> &progress)
    : m_reader(source, cancelled), m_source(source), m_manifestPath(manifest), m_legacyTextEncoding(legacyTextEncoding),
      m_directory(QFileInfo(manifest).absoluteDir()), m_progress(progress)
{
}

void LegacyProjectImporter::write(const QString &path, const QByteArray &data)
{
    m_reader.checkCancelled();
    const QString destination = m_directory.filePath(path);
    if (!QDir().mkpath(QFileInfo(destination).absolutePath()))
        m_reader.fail(QObject::tr("Cannot create directory for %1.").arg(path));
    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        m_reader.fail(QObject::tr("Cannot write %1: %2").arg(path, file.errorString()));
}

QDomDocument LegacyProjectImporter::readXml(const QString &path)
{
    QFile file(m_directory.filePath(path));
    if (!file.open(QIODevice::ReadOnly)) m_reader.fail(file.errorString());
    QDomDocument document;
    QString error;
    if (!document.setContent(&file, false, &error)) m_reader.fail(error);
    return document;
}

void LegacyProjectImporter::validateName(const QString &name)
{
    if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || !SevenZipArchive::validPath(name))
        m_reader.fail(QObject::tr("Invalid resource filename: %1").arg(name));
}

void LegacyProjectImporter::option(const QString &key, const QString &value)
{
    ActionXml::setText(ActionXml::child(m_configuration.documentElement(), QStringLiteral("Options")), QStringLiteral("option_") + key, value);
}

void LegacyProjectImporter::booleanOption(const QString &key)
{
    option(key, m_reader.boolean() ? QStringLiteral("true") : QStringLiteral("false"));
}

void LegacyProjectImporter::integerFields(QDomElement parent, const QString &fields, bool attributes)
{
    for (const QString &field : fields.split(QLatin1Char('|'))) {
        const QString value = QString::number(m_reader.integer());
        if (attributes) parent.setAttribute(field, value);
        else ActionXml::setText(parent, field, value);
    }
}

void LegacyProjectImporter::booleanFields(QDomElement parent, const QString &fields, bool attributes)
{
    for (const QString &field : fields.split(QLatin1Char('|'))) {
        const QString value = m_reader.boolean() ? QStringLiteral("-1") : QStringLiteral("0");
        if (attributes) parent.setAttribute(field, value);
        else ActionXml::setText(parent, field, value);
    }
}

QString LegacyProjectImporter::resourceName(LegacyResourceKind kind, int index) const
{
    const auto table = m_resources.constFind(kind);
    if (table != m_resources.cend() && index >= 0 && index < table->size()) return table->at(index).name;
    return QString();
}

void LegacyProjectImporter::reference(QDomElement parent, const QString &tag, LegacyResourceKind kind,
                                     int index, bool attribute, bool numeric)
{
    LegacyReference reference;
    reference.element = attribute ? parent : ActionXml::child(parent, tag);
    reference.attribute = attribute ? tag : QString();
    reference.kind = kind;
    reference.index = index;
    reference.numeric = numeric;
    m_references.append(reference);
}

void LegacyProjectImporter::readResources(LegacyResourceKind kind, std::initializer_list<int> versions,
                                         void (LegacyProjectImporter::*readResource)(LegacyResource &, int))
{
    const auto &format = *legacyFormat(kind);
    m_reader.setContext(QString::fromLatin1(format.group));
    const int tableVersion = m_reader.version(versions);
    const int count = m_reader.count(100000);
    QVector<LegacyResource> &resources = m_resources[kind];
    resources.resize(count);
    QSet<QString> names;
    for (int index = 0; index < count; ++index) {
        const bool compressed = tableVersion >= 800 && !(kind == LegacyResourceKind::Sprite && tableVersion == 810);
        m_reader.setContext(QStringLiteral("%1[%2]").arg(QString::fromLatin1(format.group)).arg(index));
        if (compressed) m_reader.beginBlock();
        if (m_reader.boolean()) {
            LegacyResource &resource = resources[index];
            const QString originalName = m_reader.string();
            // Legacy names are stored inside the project and may have padding
            // that cannot be retained when the resource becomes a GMX file.
            resource.name = originalName.trimmed();
            validateName(resource.name);
            const QString baseName = resource.name;
            int suffix = index;
            while (names.contains(resource.name.toCaseFolded()))
                resource.name = baseName + QStringLiteral("_imported_%1").arg(suffix++);
            names.insert(resource.name.toCaseFolded());
            if (resource.name != originalName) {
                // GMK references use table IDs, so keep every slot and give its
                // files a unique name before writing images, audio or code.
                // Name-based GML is ambiguous and must not be replaced globally.
                m_warnings.append(QObject::tr("Resource '%1' (%2 ID %3) was imported as '%4' to use a valid, unique filename. Resource references were updated; check name-based GML references.")
                    .arg(originalName, QString::fromLatin1(format.group)).arg(index).arg(resource.name));
            }
            m_reader.setContext(resource.name);
            m_progress(resource.name, m_stage + index * 7 / qMax(1, count));
            if (compressed) m_reader.skip(8);
            resource.path = QString::fromLatin1(format.directory) + QLatin1Char('/') + resource.name + QString::fromLatin1(format.suffix);
            resource.document.appendChild(resource.document.createElement(QString::fromLatin1(format.tag)));
            (this->*readResource)(resource, m_reader.integer());
            // Only the small XML descriptions and unresolved references stay in
            // memory. Image and audio payloads are written while reading them.
        }
        m_reader.endBlock();
    }
    m_stage += 8;
}

void LegacyProjectImporter::importProject()
{
    m_reader.setContext(QObject::tr("Project header"));
    m_reader.version({1234321});
    m_version = m_reader.version({701, 800, 810});
    m_reader.setTextEncoding(m_version >= 810 ? QByteArray("UTF-8") : m_legacyTextEncoding);
    m_manifest = readXml(m_manifestPath);
    m_configuration = readXml(QStringLiteral("Configs/Default.config.gmx"));
    quint32 gameId;
    if (m_version == 701) {
        const int before = m_reader.count(), after = m_reader.count();
        m_reader.skip(qint64(before) * 4);
        const int seed = m_reader.integer();
        m_reader.skip(qint64(after) * 4);
        QByteArray id = m_reader.bytes(1);
        m_reader.setEncoding(seed);
        id += m_reader.bytes(3);
        gameId = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(id.constData()));
    } else gameId = quint32(m_reader.integer());
    option(QStringLiteral("gameid"), QString::number(gameId));
    const QByteArray guid = m_reader.bytes(16);
    const uchar *data = reinterpret_cast<const uchar *>(guid.constData());
    const QUuid uuid(qFromLittleEndian<quint32>(data), qFromLittleEndian<quint16>(data + 4),
        qFromLittleEndian<quint16>(data + 6), data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15]);
    option(QStringLiteral("gameguid"), uuid.toString().toUpper());
    readSettings();
    if (m_version >= 800) {
        readTriggers();
        m_reader.version({800});
        readConstants();
        m_reader.skip(8);
    }
    m_stage = 8;
    readResources(LegacyResourceKind::Sound, {400, 800}, &LegacyProjectImporter::readSound);
    readResources(LegacyResourceKind::Sprite, {400, 800, 810}, &LegacyProjectImporter::readSprite);
    readResources(LegacyResourceKind::Background, {400, 800}, &LegacyProjectImporter::readBackground);
    readResources(LegacyResourceKind::Path, {420, 800}, &LegacyProjectImporter::readPath);
    readResources(LegacyResourceKind::Script, {400, 800, 810}, &LegacyProjectImporter::readScript);
    readResources(LegacyResourceKind::Font, {540, 800}, &LegacyProjectImporter::readFont);
    readResources(LegacyResourceKind::Timeline, {500, 800}, &LegacyProjectImporter::readTimeline);
    readResources(LegacyResourceKind::Object, {400, 800}, &LegacyProjectImporter::readObject);
    readResources(LegacyResourceKind::Room, {420, 800}, &LegacyProjectImporter::readRoom);
    m_reader.skip(8); // Last allocated instance and tile IDs.
    readIncludedFiles();
    m_reader.setContext(QObject::tr("Extension packages"));
    m_reader.version({700});
    const int packages = m_reader.count();
    for (int i = 0; i < packages; ++i) {
        const QString name = m_reader.string();
        m_warnings.append(QObject::tr("Extension package '%1' is not embedded in the legacy project. Add its GMS extension separately.").arg(name));
    }
    readInformation();
    m_reader.setContext(QObject::tr("Library initialization"));
    m_reader.version({500});
    const int libraries = m_reader.count();
    for (int i = 0; i < libraries; ++i) {
        const QString code = m_reader.string();
        if (code.trimmed().isEmpty()) continue;
        // Library initialization has no GMX counterpart. Keep its source for
        // the user instead of silently losing external action-library code.
        const QString path = QStringLiteral("legacy/library%1.gml").arg(i);
        write(path, code.toUtf8());
        m_warnings.append(QObject::tr("Legacy library initialization was saved to %1; move it to game initialization code.").arg(path));
    }
    m_reader.setContext(QObject::tr("Resource tree"));
    const int treeVersion = m_reader.version({500, 540, 700});
    const int rooms = m_reader.count(100000);
    for (int i = 0; i < rooms; ++i) m_roomOrder.append(m_reader.integer());
    readTree(m_manifest.documentElement(), treeVersion > 540 ? 12 : 11);
    finishResources();
    write(QStringLiteral("Configs/Default.config.gmx"), m_configuration.toByteArray(2));
    write(m_manifestPath, m_manifest.toByteArray(2));
    m_progress(QObject::tr("Reading project..."), 95);
}

void LegacyProjectImporter::readConstants()
{
    QDomElement constants = ActionXml::child(m_manifest.documentElement(), QStringLiteral("constants"));
    const int count = m_reader.count(100000);
    for (int i = 0; i < count; ++i) {
        const QString name = m_reader.string();
        const QString value = m_reader.string();
        QDomElement constant = m_manifest.createElement(QStringLiteral("constant"));
        constant.setAttribute(QStringLiteral("name"), name);
        constant.appendChild(m_manifest.createTextNode(value));
        constants.appendChild(constant);
    }
}

void LegacyProjectImporter::readTriggers()
{
    m_reader.setContext(QObject::tr("Triggers"));
    m_reader.version({800});
    const int count = m_reader.count(100000);
    QDomElement triggers = ActionXml::child(m_manifest.documentElement(), QStringLiteral("triggers"));
    for (int i = 0; i < count; ++i) {
        m_reader.beginBlock();
        if (m_reader.boolean()) {
            m_reader.version({800});
            QDomElement trigger = m_manifest.createElement(QStringLiteral("trigger"));
            trigger.setAttribute(QStringLiteral("index"), i);
            ActionXml::setText(trigger, QStringLiteral("name"), m_reader.string());
            ActionXml::setText(trigger, QStringLiteral("condition"), m_reader.string());
            ActionXml::setText(trigger, QStringLiteral("checkstep"), QString::number(m_reader.integer()));
            ActionXml::setText(trigger, QStringLiteral("constant"), m_reader.string());
            triggers.appendChild(trigger);
        }
        m_reader.endBlock();
    }
    m_reader.skip(8);
    if (!triggers.firstChild().isNull())
        m_warnings.append(QObject::tr("Legacy trigger definitions and events were preserved, but automatic trigger execution is not supported. Convert them to Step events before running the game."));
}

void LegacyProjectImporter::readTree(QDomElement parent, int count, int depth)
{
    if (depth > 128) m_reader.fail(QObject::tr("Resource groups are nested too deeply."));
    for (int i = 0; i < count; ++i) {
        const int status = m_reader.integer();
        const auto kind = LegacyResourceKind(m_reader.integer());
        const int index = m_reader.integer();
        const QString name = m_reader.string();
        const int children = m_reader.count(100000);
        const auto format = legacyFormat(kind);
        QDomElement node;
        if (format && status == 1) {
            node = ActionXml::child(m_manifest.documentElement(), QString::fromLatin1(format->group));
        } else if (format && status == 2) {
            node = m_manifest.createElement(QString::fromLatin1(format->group));
            node.setAttribute(QStringLiteral("name"), name.isEmpty() ? QStringLiteral("Group") : name);
            parent.appendChild(node);
        } else if (format && status == 3 && !resourceName(kind, index).isEmpty()) {
            if (!m_treeResources[kind].contains(index)) {
                node = m_manifest.createElement(QString::fromLatin1(format->tag));
                const auto &resource = m_resources[kind].at(index);
                QString reference = resource.path;
                if (kind != LegacyResourceKind::Script) reference.chop(int(qstrlen(format->suffix)));
                node.appendChild(m_manifest.createTextNode(reference.replace(QLatin1Char('/'), QLatin1Char('\\'))));
                parent.appendChild(node);
                m_treeResources[kind].insert(index);
            }
        }
        // Information/settings/package nodes have no resource children.
        if (children) readTree(node.isNull() ? parent : node, children, depth + 1);
    }
}

static void collectRoomNames(const QDomElement &parent, QStringList &names)
{
    for (auto node = parent.firstChildElement(); !node.isNull(); node = node.nextSiblingElement()) {
        if (node.tagName() == QStringLiteral("rooms")) collectRoomNames(node, names);
        else if (node.tagName() == QStringLiteral("room")) names.append(QFileInfo(QString(node.text()).replace(QLatin1Char('\\'), QLatin1Char('/'))).fileName());
    }
}

void LegacyProjectImporter::finishResources()
{
    m_progress(QObject::tr("Resolving resource references..."), 90);
    // A resource can exist outside the saved tree (for example after recovery).
    for (const auto &format : LegacyFormats) {
        const auto &resources = m_resources[format.kind];
        auto group = ActionXml::child(m_manifest.documentElement(), QString::fromLatin1(format.group));
        for (int i = 0; i < resources.size(); ++i) {
            if (resources.at(i).name.isEmpty() || m_treeResources[format.kind].contains(i)) continue;
            auto node = m_manifest.createElement(QString::fromLatin1(format.tag));
            QString path = resources.at(i).path;
            if (format.kind != LegacyResourceKind::Script) path.chop(int(qstrlen(format.suffix)));
            node.appendChild(m_manifest.createTextNode(path.replace(QLatin1Char('/'), QLatin1Char('\\'))));
            group.appendChild(node);
        }
    }
    auto rooms = ActionXml::child(m_manifest.documentElement(), QStringLiteral("rooms"));
    QStringList treeOrder, order;
    collectRoomNames(rooms, treeOrder);
    for (int index : m_roomOrder) {
        const QString name = resourceName(LegacyResourceKind::Room, index);
        if (!name.isEmpty() && !order.contains(name)) order.append(name);
    }
    for (const QString &name : treeOrder) if (!order.contains(name)) order.append(name);
    if (treeOrder != order) {
        while (!rooms.firstChild().isNull()) rooms.removeChild(rooms.firstChild());
        for (const QString &name : order) {
            auto node = m_manifest.createElement(QStringLiteral("room"));
            node.appendChild(m_manifest.createTextNode(QStringLiteral("rooms\\") + name));
            rooms.appendChild(node);
        }
        m_warnings.append(QObject::tr("Room groups were flattened to preserve the legacy room execution order."));
    }
    for (LegacyReference &reference : m_references) {
        const QString name = resourceName(reference.kind, reference.index);
        QString value = name.isEmpty() ? QStringLiteral("<undefined>") : name;
        if (reference.numeric) value = QString::number(order.indexOf(name));
        if (!reference.attribute.isEmpty()) reference.element.setAttribute(reference.attribute, value);
        else {
            while (!reference.element.firstChild().isNull()) reference.element.removeChild(reference.element.firstChild());
            reference.element.appendChild(reference.element.ownerDocument().createTextNode(value));
        }
    }
    m_references.clear();
    for (auto table = m_resources.begin(); table != m_resources.end(); ++table) {
        for (auto &resource : table.value()) {
            if (!resource.name.isEmpty() && !resource.document.isNull())
                write(resource.path, resource.document.toByteArray(2));
            resource.document.clear();
        }
    }
}
