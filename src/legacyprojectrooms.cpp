#include "legacyprojectimporter.h"
#include "actionxml.h"

#include <QObject>

static QString legacyInstanceColor(quint32 color)
{
    return QString::number((color & 0xff00ff00u) | ((color & 255) << 16) | ((color >> 16) & 255));
}

void LegacyProjectImporter::readRoom(LegacyResource &resource, int version)
{
    if (version != 520 && version != 541 && version != 810 && version != 811 && version != 820)
        m_reader.fail(QObject::tr("Unsupported room version: %1.").arg(version));
    auto root = resource.document.documentElement();
    ActionXml::setText(root, QStringLiteral("caption"), m_reader.string());
    integerFields(root, QStringLiteral("width|height|vsnap|hsnap"));
    booleanFields(root, QStringLiteral("isometric"));
    integerFields(root, QStringLiteral("speed"));
    booleanFields(root, QStringLiteral("persistent"));
    integerFields(root, QStringLiteral("colour"));
    const int clearFlags = m_reader.integer();
    ActionXml::setText(root, QStringLiteral("showcolour"), clearFlags & 1 ? QStringLiteral("-1") : QStringLiteral("0"));
    ActionXml::setText(root, QStringLiteral("clearViewBackground"), clearFlags & 2 ? QStringLiteral("0") : QStringLiteral("-1"));
    ActionXml::setText(root, QStringLiteral("clearDisplayBuffer"), clearFlags & 1 ? QStringLiteral("-1") : QStringLiteral("0"));
    ActionXml::setText(root, QStringLiteral("code"), m_reader.string());
    auto backgrounds = ActionXml::child(root, QStringLiteral("backgrounds"));
    const int backgroundCount = m_reader.count(8);
    for (int i = 0; i < backgroundCount; ++i) {
        auto background = resource.document.createElement(QStringLiteral("background"));
        backgrounds.appendChild(background);
        booleanFields(background, QStringLiteral("visible|foreground"), true);
        const QString name = resourceName(LegacyResourceKind::Background, m_reader.integer());
        background.setAttribute(QStringLiteral("name"), name);
        integerFields(background, QStringLiteral("x|y"), true);
        booleanFields(background, QStringLiteral("htiled|vtiled"), true);
        integerFields(background, QStringLiteral("hspeed|vspeed"), true);
        booleanFields(background, QStringLiteral("stretch"), true);
    }
    booleanFields(root, QStringLiteral("enableViews"));
    auto views = ActionXml::child(root, QStringLiteral("views"));
    const int viewCount = m_reader.count(8);
    for (int i = 0; i < viewCount; ++i) {
        auto view = resource.document.createElement(QStringLiteral("view"));
        views.appendChild(view);
        booleanFields(view, QStringLiteral("visible"), true);
        integerFields(view, QStringLiteral("xview|yview|wview|hview|xport|yport"), true);
        if (version > 520) integerFields(view, QStringLiteral("wport|hport"), true);
        else {
            view.setAttribute(QStringLiteral("wport"), view.attribute(QStringLiteral("wview")));
            view.setAttribute(QStringLiteral("hport"), view.attribute(QStringLiteral("hview")));
        }
        integerFields(view, QStringLiteral("hborder|vborder|hspeed|vspeed"), true);
        const QString name = resourceName(LegacyResourceKind::Object, m_reader.integer());
        view.setAttribute(QStringLiteral("objName"), name.isEmpty() ? QStringLiteral("<undefined>") : name);
    }
    auto instances = ActionXml::child(root, QStringLiteral("instances"));
    const int instanceCount = m_reader.count();
    QSet<int> instanceIds;
    for (int i = 0; i < instanceCount; ++i) {
        auto instance = resource.document.createElement(QStringLiteral("instance"));
        instances.appendChild(instance);
        integerFields(instance, QStringLiteral("x|y"), true);
        const QString name = resourceName(LegacyResourceKind::Object, m_reader.integer());
        instance.setAttribute(QStringLiteral("objName"), name.isEmpty() ? QStringLiteral("<undefined>") : name);
        const int id = m_reader.integer();
        if (instanceIds.contains(id)) m_reader.fail(QObject::tr("Duplicate room instance ID: %1.").arg(id));
        instanceIds.insert(id);
        instance.setAttribute(QStringLiteral("id"), id);
        instance.setAttribute(QStringLiteral("name"), QStringLiteral("inst_%1").arg(quint32(id), 8, 16, QLatin1Char('0')).toUpper().replace(QStringLiteral("INST_"), QStringLiteral("inst_")));
        instance.setAttribute(QStringLiteral("code"), m_reader.string());
        instance.setAttribute(QStringLiteral("scaleX"), QStringLiteral("1"));
        instance.setAttribute(QStringLiteral("scaleY"), QStringLiteral("1"));
        instance.setAttribute(QStringLiteral("colour"), QStringLiteral("4294967295"));
        instance.setAttribute(QStringLiteral("rotation"), QStringLiteral("0"));
        if (version >= 810) {
            instance.setAttribute(QStringLiteral("scaleX"), QString::number(m_reader.real(), 'g', 17));
            instance.setAttribute(QStringLiteral("scaleY"), QString::number(m_reader.real(), 'g', 17));
            instance.setAttribute(QStringLiteral("colour"), legacyInstanceColor(quint32(m_reader.integer())));
        }
        if (version >= 811) instance.setAttribute(QStringLiteral("rotation"), QString::number(m_reader.real(), 'g', 17));
        booleanFields(instance, QStringLiteral("locked"), true);
    }
    auto tiles = ActionXml::child(root, QStringLiteral("tiles"));
    const int tileCount = m_reader.count();
    QSet<int> tileIds;
    for (int i = 0; i < tileCount; ++i) {
        auto tile = resource.document.createElement(QStringLiteral("tile"));
        tiles.appendChild(tile);
        integerFields(tile, QStringLiteral("x|y"), true);
        tile.setAttribute(QStringLiteral("bgName"), resourceName(LegacyResourceKind::Background, m_reader.integer()));
        integerFields(tile, QStringLiteral("xo|yo|w|h|depth"), true);
        const int id = m_reader.integer();
        if (tileIds.contains(id)) m_reader.fail(QObject::tr("Duplicate room tile ID: %1.").arg(id));
        tileIds.insert(id);
        tile.setAttribute(QStringLiteral("id"), id);
        tile.setAttribute(QStringLiteral("name"), QStringLiteral("inst_%1").arg(quint32(id), 8, 16, QLatin1Char('0')).toUpper().replace(QStringLiteral("INST_"), QStringLiteral("inst_")));
        tile.setAttribute(QStringLiteral("scaleX"), QStringLiteral("1"));
        tile.setAttribute(QStringLiteral("scaleY"), QStringLiteral("1"));
        tile.setAttribute(QStringLiteral("colour"), QStringLiteral("4294967295"));
        if (version >= 810) {
            tile.setAttribute(QStringLiteral("scaleX"), QString::number(m_reader.real(), 'g', 17));
            tile.setAttribute(QStringLiteral("scaleY"), QString::number(m_reader.real(), 'g', 17));
            tile.setAttribute(QStringLiteral("colour"), QString::number(quint32(m_reader.integer())));
        }
        booleanFields(tile, QStringLiteral("locked"), true);
    }
    ActionXml::setText(root, QStringLiteral("PhysicsWorld"), QStringLiteral("0"));
    ActionXml::setText(root, QStringLiteral("PhysicsWorldTop"), QStringLiteral("0"));
    ActionXml::setText(root, QStringLiteral("PhysicsWorldLeft"), QStringLiteral("0"));
    ActionXml::setText(root, QStringLiteral("PhysicsWorldRight"), ActionXml::text(root, QStringLiteral("width")));
    ActionXml::setText(root, QStringLiteral("PhysicsWorldBottom"), ActionXml::text(root, QStringLiteral("height")));
    ActionXml::setText(root, QStringLiteral("PhysicsWorldGravityX"), QStringLiteral("0"));
    ActionXml::setText(root, QStringLiteral("PhysicsWorldGravityY"), QStringLiteral("10"));
    ActionXml::setText(root, QStringLiteral("PhysicsWorldPixToMeters"), QStringLiteral("0.1"));
    if (version == 820) {
        booleanFields(root, QStringLiteral("PhysicsWorld"));
        integerFields(root, QStringLiteral("PhysicsWorldTop|PhysicsWorldLeft|PhysicsWorldRight|PhysicsWorldBottom"));
        for (const QString &key : {QStringLiteral("PhysicsWorldGravityX"), QStringLiteral("PhysicsWorldGravityY"), QStringLiteral("PhysicsWorldPixToMeters")})
            ActionXml::setText(root, key, QString::number(m_reader.real(), 'g', 17));
    }
    auto maker = ActionXml::child(root, QStringLiteral("makerSettings"));
    ActionXml::setText(maker, QStringLiteral("isSet"), QStringLiteral("-1"));
    booleanFields(maker, QStringLiteral("rememberWindowSize"));
    integerFields(maker, QStringLiteral("wEditor|hEditor"));
    booleanFields(maker, QStringLiteral("showGrid|showObjects|showTiles|showBackgrounds|showForegrounds|showViews|deleteUnderlyingObjects|deleteUnderlyingTiles"));
    if (version == 520) m_reader.skip(24);
    integerFields(maker, QStringLiteral("page|xoffset|yoffset"));
    // Room references only target resource tables already read. Large instance
    // and tile lists can therefore be released before reading the next room.
    write(resource.path, resource.document.toByteArray(2));
    resource.document.clear();
}
