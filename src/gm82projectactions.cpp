#include "gm82projectimporter.h"
#include "actionxml.h"

void Gm82ProjectImporter::readLibraries()
{
    const QDir directory(ActionLibraryManager::directory());
    for (const QString &name : directory.entryList({QStringLiteral("*.lib")}, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        checkCancelled();
        ActionLibrary library;
        QString error;
        // Import runs in a worker. Parse definitions without creating QPixmaps.
        if (!ActionLibraryReader::read(directory.filePath(name), library, error, false)) {
            m_warnings.append(name + QStringLiteral(": ") + error);
            continue;
        }
        for (const auto &action : library.actions) {
            const auto key = qMakePair(library.id, action.id);
            if (action.kind < 8 && !m_actions.contains(key)) m_actions.insert(key, action);
        }
        if (!library.initializationCode.trimmed().isEmpty() && !m_libraryInitializations.contains(library.id))
            m_libraryInitializations.insert(library.id, library.initializationCode);
    }
}

void Gm82ProjectImporter::readActions(QDomElement parent, const QString &code, const QString &context)
{
    static const QString Token = QStringLiteral("/*\"/*'/**//* YYD ACTION");
    auto xml = parent.ownerDocument();
    const QStringList parts = code.split(Token);
    if (!parts.first().trimmed().isEmpty()) throw QObject::tr("%1: Missing GM82 action header.").arg(context);
    for (int i = 1; i < parts.size(); ++i) {
        checkCancelled();
        const QString &part = parts.at(i);
        const int end = part.indexOf(QStringLiteral("*/"));
        if (end < 0) throw QObject::tr("%1: Unterminated GM82 action header.").arg(context);
        const auto properties = Gm82Properties::parse(part.left(end), context);
        const int library = int(properties.integer(QStringLiteral("lib_id"), -1));
        const int id = int(properties.integer(QStringLiteral("action_id"), -1));
        const auto definition = m_actions.constFind(qMakePair(library, id));
        if (definition == m_actions.cend())
            throw QObject::tr("%1: Action library definition %2/%3 is not installed. GM82 stores action IDs only; install the matching .lib file before importing.").arg(context).arg(library).arg(id);
        if (m_libraryInitializations.contains(library)) {
            const QString path = QStringLiteral("legacy/library%1.gml").arg(library);
            write(path, m_libraryInitializations.take(library).toUtf8());
            m_warnings.append(QObject::tr("Legacy library initialization was saved to %1; move it to game initialization code.").arg(path));
        }
        auto action = ActionXml::createAction(xml, definition.value());
        fields(action, properties, QStringLiteral("relative|invert:isnot"), true);
        if (properties.values.contains(QStringLiteral("applies_to"))) {
            QString owner = properties.text(QStringLiteral("applies_to"));
            if (owner != QStringLiteral("self") && owner != QStringLiteral("other")) owner = reference(QStringLiteral("objects"), owner);
            ActionXml::setText(action, QStringLiteral("whoName"), owner);
        }
        const auto arguments = ActionXml::elements(action.firstChildElement(QStringLiteral("arguments")), QStringLiteral("argument"));
        const QStringList metadata = QStringLiteral("lib_id|action_id|relative|applies_to|invert|var_name|var_value|repeats").split(QLatin1Char('|'));
        for (auto it = properties.values.cbegin(); it != properties.values.cend(); ++it) {
            if (metadata.contains(it.key())) continue;
            bool ok = false;
            const int index = it.key().mid(3).toInt(&ok);
            if (!it.key().startsWith(QStringLiteral("arg")) || !ok || index < 0 || index >= arguments.size())
                throw QObject::tr("%1: Unknown GM82 action property: %2").arg(context, it.key());
        }
        for (int index = 0; index < arguments.size(); ++index) {
            QString key = QStringLiteral("arg%1").arg(index);
            if (definition->kind == 5 && index == 0) key = QStringLiteral("repeats");
            if (definition->kind == 6) key = index == 0 ? QStringLiteral("var_name") : QStringLiteral("var_value");
            const int kind = definition->arguments.at(index).kind;
            const QString tag = ActionXml::argumentTag(kind);
            QString value;
            if (definition->kind == 7 && index == 0) {
                value = part.mid(end + 2);
                if (value.startsWith(QLatin1Char('\n'))) value.remove(0, 1);
            } else {
                if (!properties.values.contains(key)) continue;
                value = properties.text(key);
                if (tag != QStringLiteral("string")) value = reference(tag + QLatin1Char('s'), value);
                else if (definition->kind == 0) value = undelimit(value);
            }
            ActionXml::setText(arguments.at(index), tag, value);
        }
        if (definition->kind != 7 && !part.mid(end + 2).trimmed().isEmpty())
            throw QObject::tr("%1: Unexpected code after a non-code GM82 action.").arg(context);
        parent.appendChild(action);
    }
}

// Event headers are lines beginning with #define, exactly as gm82save writes
// them. #define is not allowed inside object/timeline action code by that format.
static QVector<QPair<QString, QString>> gm82Events(const QString &code, const QString &context)
{
    QVector<QPair<QString, QString>> events;
    for (const QString &line : code.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("#define "))) events.append(qMakePair(line.mid(8), QString()));
        else if (!events.isEmpty()) events.last().second += line + QLatin1Char('\n');
        else if (!line.trimmed().isEmpty()) throw QObject::tr("%1: Missing GM82 event header.").arg(context);
    }
    return events;
}

QDomDocument Gm82ProjectImporter::readObject(const QString &name)
{
    const QString path = QStringLiteral("objects/") + name;
    const auto properties = readProperties(path + QStringLiteral(".txt"));
    auto xml = document(QStringLiteral("object"));
    auto root = xml.documentElement();
    ActionXml::setText(root, QStringLiteral("spriteName"), reference(QStringLiteral("sprites"), properties.text(QStringLiteral("sprite"))));
    ActionXml::setText(root, QStringLiteral("maskName"), reference(QStringLiteral("sprites"), properties.text(QStringLiteral("mask"))));
    ActionXml::setText(root, QStringLiteral("parentName"), reference(QStringLiteral("objects"), properties.text(QStringLiteral("parent"))));
    fields(root, properties, QStringLiteral("visible|solid|persistent"), true);
    fields(root, properties, QStringLiteral("depth"));
    const QStringList eventTypes = QStringLiteral("Create|Destroy|Alarm|Step|Collision|Keyboard|Mouse|Other|Draw|KeyPress|KeyRelease|Trigger").split(QLatin1Char('|'));
    auto container = ActionXml::child(root, QStringLiteral("events"));
    QSet<QString> seen;
    for (const auto &source : gm82Events(readText(path + QStringLiteral(".gml")), path)) {
        const int separator = source.first.indexOf(QLatin1Char('_'));
        const int type = eventTypes.indexOf(source.first.left(separator));
        const QString value = source.first.mid(separator + 1);
        if (separator < 0 || type < 0 || seen.contains(source.first)) throw QObject::tr("Invalid GM82 event: %1").arg(source.first);
        seen.insert(source.first);
        auto event = xml.createElement(QStringLiteral("event"));
        event.setAttribute(QStringLiteral("eventtype"), type);
        if (type == 4) event.setAttribute(QStringLiteral("ename"), reference(QStringLiteral("objects"), value));
        else {
            bool ok = true;
            const int number = type == 11 ? m_indexes.value(QStringLiteral("triggers")).value(value.toCaseFolded(), -1) : value.toInt(&ok);
            if (!ok || number < 0) throw QObject::tr("Invalid GM82 event: %1").arg(source.first);
            event.setAttribute(QStringLiteral("enumb"), number);
        }
        readActions(event, source.second, path + QLatin1Char('/') + source.first);
        if (!event.firstChildElement(QStringLiteral("action")).isNull()) container.appendChild(event);
    }
    for (const QString &pair : QStringLiteral("PhysicsObject:0|PhysicsObjectSensor:0|PhysicsObjectShape:0|PhysicsObjectDensity:0.5|PhysicsObjectRestitution:0.1|PhysicsObjectGroup:0|PhysicsObjectLinearDamping:0.1|PhysicsObjectAngularDamping:0.1|PhysicsObjectFriction:0.2|PhysicsObjectAwake:-1|PhysicsObjectKinematic:0").split(QLatin1Char('|')))
        ActionXml::setText(root, pair.section(QLatin1Char(':'), 0, 0), pair.section(QLatin1Char(':'), 1));
    ActionXml::child(root, QStringLiteral("PhysicsShapePoints"));
    return xml;
}

QDomDocument Gm82ProjectImporter::readTimeline(const QString &name)
{
    const QString path = QStringLiteral("timelines/") + name + QStringLiteral(".gml");
    auto xml = document(QStringLiteral("timeline"));
    QSet<int> seen;
    for (const auto &source : gm82Events(readText(path), path)) {
        bool ok;
        const int step = source.first.toInt(&ok);
        if (!ok || step < 0 || seen.contains(step)) throw QObject::tr("Invalid GM82 timeline moment: %1").arg(source.first);
        seen.insert(step);
        auto entry = xml.createElement(QStringLiteral("entry"));
        ActionXml::setText(entry, QStringLiteral("step"), QString::number(step));
        auto event = ActionXml::child(entry, QStringLiteral("event"));
        readActions(event, source.second, path + QLatin1Char('/') + source.first);
        if (!event.firstChildElement(QStringLiteral("action")).isNull()) xml.documentElement().appendChild(entry);
    }
    return xml;
}

void Gm82ProjectImporter::readTriggers()
{
    auto container = ActionXml::child(m_manifest.documentElement(), QStringLiteral("triggers"));
    const auto names = m_names.value(QStringLiteral("triggers"));
    for (int i = 0; i < names.size(); ++i) {
        if (names.at(i).isEmpty()) continue;
        const QString path = QStringLiteral("triggers/") + names.at(i);
        const auto properties = readProperties(path + QStringLiteral(".txt"));
        auto trigger = m_manifest.createElement(QStringLiteral("trigger"));
        trigger.setAttribute(QStringLiteral("index"), i);
        ActionXml::setText(trigger, QStringLiteral("name"), names.at(i));
        ActionXml::setText(trigger, QStringLiteral("constant"), properties.text(QStringLiteral("constant")));
        ActionXml::setText(trigger, QStringLiteral("condition"), readText(path + QStringLiteral(".gml")));
        fields(trigger, properties, QStringLiteral("kind:checkstep"));
        container.appendChild(trigger);
    }
    if (!container.firstChild().isNull())
        m_warnings.append(QObject::tr("Legacy trigger definitions and events were preserved, but automatic trigger execution is not supported. Convert them to Step events before running the game."));
}
