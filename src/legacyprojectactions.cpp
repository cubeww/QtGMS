#include "legacyprojectimporter.h"
#include "actionxml.h"

#include <QObject>

static LegacyResourceKind legacyArgumentKind(int kind)
{
    switch (kind) {
    case 5: return LegacyResourceKind::Sprite;
    case 6: return LegacyResourceKind::Sound;
    case 7: return LegacyResourceKind::Background;
    case 8: return LegacyResourceKind::Path;
    case 9: return LegacyResourceKind::Script;
    case 10: return LegacyResourceKind::Object;
    case 11: return LegacyResourceKind::Room;
    case 12: return LegacyResourceKind::Font;
    case 14: return LegacyResourceKind::Timeline;
    default: return LegacyResourceKind(0);
    }
}

void LegacyProjectImporter::readActions(QDomElement container)
{
    m_reader.version({400});
    const int count = m_reader.count(100000);
    QDomDocument document = container.ownerDocument();
    for (int i = 0; i < count; ++i) {
        m_reader.version({440});
        auto action = document.createElement(QStringLiteral("action"));
        container.appendChild(action);
        integerFields(action, QStringLiteral("libid|id|kind"));
        booleanFields(action, QStringLiteral("userelative|isquestion|useapplyto"));
        integerFields(action, QStringLiteral("exetype"));
        ActionXml::setText(action, QStringLiteral("functionname"), m_reader.string());
        ActionXml::setText(action, QStringLiteral("codestring"), m_reader.string());
        const int argumentCount = m_reader.count(1024);
        const int typeCount = m_reader.count(1024);
        QVector<int> types;
        for (int j = 0; j < typeCount; ++j) types.append(m_reader.integer());
        const int owner = m_reader.integer();
        if (owner == -1 || owner == -2) ActionXml::setText(action, QStringLiteral("whoName"), owner == -1 ? QStringLiteral("self") : QStringLiteral("other"));
        else reference(action, QStringLiteral("whoName"), LegacyResourceKind::Object, owner);
        booleanFields(action, QStringLiteral("relative"));
        const int valueCount = m_reader.count(1024);
        if (argumentCount > valueCount || argumentCount > typeCount)
            m_reader.fail(QObject::tr("Incomplete action arguments."));
        auto arguments = ActionXml::child(action, QStringLiteral("arguments"));
        for (int j = 0; j < valueCount; ++j) {
            const QString value = m_reader.string();
            if (j >= argumentCount) continue; // GM reserves unused argument slots.
            auto argument = document.createElement(QStringLiteral("argument"));
            arguments.appendChild(argument);
            const int type = types.at(j);
            ActionXml::setText(argument, QStringLiteral("kind"), QString::number(type));
            const QString tag = ActionXml::argumentTag(type);
            const auto kind = legacyArgumentKind(type);
            bool numeric;
            const int index = value.toInt(&numeric);
            if (int(kind) && numeric) reference(argument, tag, kind, index);
            else ActionXml::setText(argument, tag, value);
        }
        booleanFields(action, QStringLiteral("isnot"));
    }
}

void LegacyProjectImporter::readTimeline(LegacyResource &resource, int version)
{
    if (version != 500) m_reader.fail(QObject::tr("Unsupported timeline version: %1.").arg(version));
    auto root = resource.document.documentElement();
    const int count = m_reader.count(100000);
    for (int i = 0; i < count; ++i) {
        auto entry = resource.document.createElement(QStringLiteral("entry"));
        integerFields(entry, QStringLiteral("step"));
        auto event = ActionXml::child(entry, QStringLiteral("event"));
        readActions(event);
        if (!event.firstChildElement(QStringLiteral("action")).isNull()) root.appendChild(entry);
    }
}

void LegacyProjectImporter::readObject(LegacyResource &resource, int version)
{
    if (version != 430 && version != 820) m_reader.fail(QObject::tr("Unsupported object version: %1.").arg(version));
    auto root = resource.document.documentElement();
    reference(root, QStringLiteral("spriteName"), LegacyResourceKind::Sprite, m_reader.integer());
    booleanFields(root, QStringLiteral("solid|visible"));
    integerFields(root, QStringLiteral("depth"));
    booleanFields(root, QStringLiteral("persistent"));
    reference(root, QStringLiteral("parentName"), LegacyResourceKind::Object, m_reader.integer());
    reference(root, QStringLiteral("maskName"), LegacyResourceKind::Sprite, m_reader.integer());
    const int eventTypes = m_reader.count(11) + 1;
    auto events = ActionXml::child(root, QStringLiteral("events"));
    for (int type = 0; type < eventTypes; ++type) {
        int count = 0;
        while (true) {
            const int number = m_reader.integer();
            if (number == -1) break;
            if (++count > 100000) m_reader.fail(QObject::tr("Too many object events."));
            auto event = resource.document.createElement(QStringLiteral("event"));
            event.setAttribute(QStringLiteral("eventtype"), type);
            if (type == 4) reference(event, QStringLiteral("ename"), LegacyResourceKind::Object, number, true);
            else event.setAttribute(QStringLiteral("enumb"), number);
            readActions(event);
            if (!event.firstChildElement(QStringLiteral("action")).isNull()) events.appendChild(event);
        }
    }
    const QStringList keys = QStringLiteral("PhysicsObject|PhysicsObjectSensor|PhysicsObjectShape|PhysicsObjectDensity|PhysicsObjectRestitution|PhysicsObjectGroup|PhysicsObjectLinearDamping|PhysicsObjectAngularDamping|PhysicsObjectFriction|PhysicsObjectAwake|PhysicsObjectKinematic").split(QLatin1Char('|'));
    const QStringList values = QStringLiteral("0|0|0|0.5|0.1|0|0.1|0.1|0.2|-1|0").split(QLatin1Char('|'));
    for (int i = 0; i < keys.size(); ++i) ActionXml::setText(root, keys.at(i), values.at(i));
    auto points = ActionXml::child(root, QStringLiteral("PhysicsShapePoints"));
    if (version == 820) {
        booleanFields(root, QStringLiteral("PhysicsObject|PhysicsObjectSensor"));
        integerFields(root, QStringLiteral("PhysicsObjectShape"));
        for (const QString &key : {QStringLiteral("PhysicsObjectDensity"), QStringLiteral("PhysicsObjectRestitution")})
            ActionXml::setText(root, key, QString::number(m_reader.real(), 'g', 17));
        integerFields(root, QStringLiteral("PhysicsObjectGroup"));
        for (const QString &key : {QStringLiteral("PhysicsObjectLinearDamping"), QStringLiteral("PhysicsObjectAngularDamping")})
            ActionXml::setText(root, key, QString::number(m_reader.real(), 'g', 17));
        const int count = m_reader.count(100000);
        for (int i = 0; i < count; ++i) {
            const double x = m_reader.real(), y = m_reader.real();
            auto point = resource.document.createElement(QStringLiteral("point"));
            point.appendChild(resource.document.createTextNode(QString::number(x, 'g', 17) + QLatin1Char(',') + QString::number(y, 'g', 17)));
            points.appendChild(point);
        }
    }
}
