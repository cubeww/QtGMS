#include "gm82projectimporter.h"
#include "actionxml.h"

#include <cmath>

static QString gm82Coordinate(const QString &value, const QString &context, bool real = false)
{
    bool ok;
    if (real) {
        const double number = value.toDouble(&ok);
        if (ok && std::isfinite(number)) return QString::number(number, 'g', 17);
    } else {
        const int number = value.toInt(&ok);
        if (ok) return QString::number(number);
    }
    throw QObject::tr("%1: Invalid room coordinate: %2").arg(context, value);
}

static QString gm82Blend(const QString &value, bool instance, const QString &context)
{
    bool ok;
    quint32 color = value.toUInt(&ok);
    if (!ok) throw QObject::tr("%1: Invalid room blend color: %2").arg(context, value);
    // GM82 stores both in GML ABGR. GMX instances use ARGB; tiles use ABGR.
    if (instance) color = (color & 0xff00ff00u) | ((color & 255) << 16) | ((color >> 16) & 255);
    return QString::number(color);
}

QDomDocument Gm82ProjectImporter::readRoom(const QString &name)
{
    const QString directory = QStringLiteral("rooms/") + name + QLatin1Char('/');
    const auto properties = readProperties(directory + QStringLiteral("room.txt"));
    auto xml = document(QStringLiteral("room"));
    auto root = xml.documentElement();
    ActionXml::setText(root, QStringLiteral("caption"), properties.text(QStringLiteral("caption")));
    fields(root, properties, QStringLiteral("width|height|snap_x:hsnap|snap_y:vsnap|roomspeed:speed|bg_color:colour"));
    fields(root, properties, QStringLiteral("isometric|roompersistent:persistent|clear_screen:showcolour|clear_screen:clearDisplayBuffer|views_enabled:enableViews"), true);
    // gm82save writes clear_view to the old GMK clear-flags bit 1, whose
    // meaning is inverted relative to GMX's clearViewBackground checkbox.
    ActionXml::setText(root, QStringLiteral("clearViewBackground"), properties.boolean(QStringLiteral("clear_view")) ? QStringLiteral("0") : QStringLiteral("-1"));
    ActionXml::setText(root, QStringLiteral("code"), readText(directory + QStringLiteral("code.gml")));
    auto backgrounds = ActionXml::child(root, QStringLiteral("backgrounds"));
    auto views = ActionXml::child(root, QStringLiteral("views"));
    for (int i = 0; i < 8; ++i) {
        // Indexed background/view properties share the same record layout.
        Gm82Properties slot;
        slot.path = properties.path;
        const QString suffix = QString::number(i);
        for (auto it = properties.values.cbegin(); it != properties.values.cend(); ++it)
            if (it.key().endsWith(suffix)) slot.values.insert(it.key().left(it.key().size() - 1), it.value());
        auto background = xml.createElement(QStringLiteral("background"));
        fields(background, slot, QStringLiteral("bg_visible:visible|bg_is_foreground:foreground|bg_tile_h:htiled|bg_tile_v:vtiled|bg_stretch:stretch"), true, true);
        fields(background, slot, QStringLiteral("bg_xoffset:x|bg_yoffset:y|bg_hspeed:hspeed|bg_vspeed:vspeed"), false, true);
        background.setAttribute(QStringLiteral("name"), reference(QStringLiteral("backgrounds"), slot.text(QStringLiteral("bg_source"))));
        backgrounds.appendChild(background);
        auto view = xml.createElement(QStringLiteral("view"));
        fields(view, slot, QStringLiteral("view_visible:visible"), true, true);
        fields(view, slot, QStringLiteral("view_xview:xview|view_yview:yview|view_wview:wview|view_hview:hview|view_xport:xport|view_yport:yport|view_wport:wport|view_hport:hport|view_fol_hbord:hborder|view_fol_vbord:vborder|view_fol_hspeed:hspeed|view_fol_vspeed:vspeed"), false, true);
        view.setAttribute(QStringLiteral("objName"), reference(QStringLiteral("objects"), slot.text(QStringLiteral("view_fol_target"))));
        views.appendChild(view);
    }
    auto instances = ActionXml::child(root, QStringLiteral("instances"));
    int lineNumber = 0;
    for (const QString &line : readText(directory + QStringLiteral("instances.txt")).split(QLatin1Char('\n'))) {
        ++lineNumber;
        if (line.isEmpty()) continue;
        checkCancelled();
        const QString context = directory + QStringLiteral("instances.txt:%1").arg(lineNumber);
        const QStringList values = line.split(QLatin1Char(','));
        if (values.size() < 5 || values.size() > 10) throw QObject::tr("%1: Invalid GM82 instance record.").arg(context);
        auto instance = xml.createElement(QStringLiteral("instance"));
        instance.setAttribute(QStringLiteral("objName"), reference(QStringLiteral("objects"), values.at(0)));
        instance.setAttribute(QStringLiteral("x"), gm82Coordinate(values.at(1), context));
        instance.setAttribute(QStringLiteral("y"), gm82Coordinate(values.at(2), context));
        const QString hash = values.at(3);
        if (!hash.isEmpty()) {
            bool ok;
            hash.toUInt(&ok, 16);
            if (!ok || hash.size() > 8) throw QObject::tr("%1: Invalid GM82 instance name.").arg(context);
        }
        const int id = m_nextInstanceId++;
        QString instanceName = QStringLiteral("inst_") + (hash.isEmpty() ? QString::number(quint32(id), 16).rightJustified(8, QLatin1Char('0')) : hash).toUpper();
        const QString baseName = instanceName;
        int suffix = 1;
        while (m_instanceNames.contains(instanceName)) instanceName = baseName + QStringLiteral("_%1").arg(suffix++);
        m_instanceNames.insert(instanceName);
        instance.setAttribute(QStringLiteral("id"), id);
        instance.setAttribute(QStringLiteral("name"), instanceName);
        instance.setAttribute(QStringLiteral("locked"), gm82Coordinate(values.at(4), context).toInt() ? -1 : 0);
        instance.setAttribute(QStringLiteral("scaleX"), values.size() > 5 ? gm82Coordinate(values.at(5), context, true) : QStringLiteral("1"));
        instance.setAttribute(QStringLiteral("scaleY"), values.size() > 6 ? gm82Coordinate(values.at(6), context, true) : QStringLiteral("1"));
        instance.setAttribute(QStringLiteral("colour"), values.size() > 7 ? gm82Blend(values.at(7), true, context) : QStringLiteral("4294967295"));
        instance.setAttribute(QStringLiteral("rotation"), values.size() > 8 ? gm82Coordinate(values.at(8), context, true) : QStringLiteral("0"));
        const bool hasCode = values.size() > 9 ? gm82Coordinate(values.at(9), context).toInt() != 0 : !hash.isEmpty();
        if (hasCode && hash.isEmpty()) throw QObject::tr("%1: Instance code has no filename.").arg(context);
        instance.setAttribute(QStringLiteral("code"), hasCode ? readText(directory + hash + QStringLiteral(".gml")) : QString());
        instances.appendChild(instance);
    }
    auto tiles = ActionXml::child(root, QStringLiteral("tiles"));
    QSet<int> depths;
    for (const QString &line : readText(directory + QStringLiteral("layers.txt")).split(QLatin1Char('\n'))) {
        if (line.isEmpty()) continue;
        const QString depth = gm82Coordinate(line, directory + QStringLiteral("layers.txt"));
        if (depths.contains(depth.toInt())) throw QObject::tr("Duplicate GM82 tile layer: %1").arg(depth);
        depths.insert(depth.toInt());
        const QString path = directory + line + QStringLiteral(".txt");
        lineNumber = 0;
        for (const QString &tileLine : readText(path).split(QLatin1Char('\n'))) {
            ++lineNumber;
            if (tileLine.isEmpty()) continue;
            checkCancelled();
            const QString context = path + QStringLiteral(":%1").arg(lineNumber);
            const QStringList values = tileLine.split(QLatin1Char(','));
            if (values.size() < 8 || values.size() > 11) throw QObject::tr("%1: Invalid GM82 tile record.").arg(context);
            auto tile = xml.createElement(QStringLiteral("tile"));
            tile.setAttribute(QStringLiteral("bgName"), reference(QStringLiteral("backgrounds"), values.at(0)));
            const QStringList coordinates = QStringLiteral("x|y|xo|yo|w|h").split(QLatin1Char('|'));
            for (int i = 0; i < coordinates.size(); ++i) tile.setAttribute(coordinates.at(i), gm82Coordinate(values.at(i + 1), context));
            tile.setAttribute(QStringLiteral("depth"), depth);
            const int id = m_nextTileId++;
            tile.setAttribute(QStringLiteral("id"), id);
            tile.setAttribute(QStringLiteral("name"), QStringLiteral("inst_%1").arg(quint32(id), 8, 16, QLatin1Char('0')));
            tile.setAttribute(QStringLiteral("locked"), gm82Coordinate(values.at(7), context).toInt() ? -1 : 0);
            tile.setAttribute(QStringLiteral("scaleX"), values.size() > 8 ? gm82Coordinate(values.at(8), context, true) : QStringLiteral("1"));
            tile.setAttribute(QStringLiteral("scaleY"), values.size() > 9 ? gm82Coordinate(values.at(9), context, true) : QStringLiteral("1"));
            tile.setAttribute(QStringLiteral("colour"), values.size() > 10 ? gm82Blend(values.at(10), false, context) : QStringLiteral("4294967295"));
            tiles.appendChild(tile);
        }
    }
    auto maker = ActionXml::child(root, QStringLiteral("makerSettings"));
    ActionXml::setText(maker, QStringLiteral("isSet"), QStringLiteral("-1"));
    fields(maker, properties, QStringLiteral("remember:rememberWindowSize|show_grid:showGrid|show_objects:showObjects|show_tiles:showTiles|show_backgrounds:showBackgrounds|show_foregrounds:showForegrounds|show_views:showViews|delete_underlying_objects:deleteUnderlyingObjects|delete_underlying_tiles:deleteUnderlyingTiles"), true);
    fields(maker, properties, QStringLiteral("editor_width:wEditor|editor_height:hEditor|tab:page|editor_x:xoffset|editor_y:yoffset"));
    for (const QString &pair : QStringLiteral("PhysicsWorld:0|PhysicsWorldTop:0|PhysicsWorldLeft:0|PhysicsWorldGravityX:0|PhysicsWorldGravityY:10|PhysicsWorldPixToMeters:0.1").split(QLatin1Char('|')))
        ActionXml::setText(root, pair.section(QLatin1Char(':'), 0, 0), pair.section(QLatin1Char(':'), 1));
    ActionXml::setText(root, QStringLiteral("PhysicsWorldRight"), ActionXml::text(root, QStringLiteral("width")));
    ActionXml::setText(root, QStringLiteral("PhysicsWorldBottom"), ActionXml::text(root, QStringLiteral("height")));
    return xml;
}
