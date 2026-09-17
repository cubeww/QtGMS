#include "gm82projectimporter.h"
#include "actionxml.h"

#include <QImageReader>
#include <cmath>

static void gm82TextureGroup(QDomElement root)
{
    ActionXml::setText(ActionXml::child(root, QStringLiteral("TextureGroups")), QStringLiteral("TextureGroup0"), QStringLiteral("0"));
    for (const QString &key : {QStringLiteral("HTile"), QStringLiteral("VTile"), QStringLiteral("For3D")})
        ActionXml::setText(root, key, QStringLiteral("0"));
}

static QSize gm82ImageSize(const QString &path)
{
    QImageReader reader(path);
    const QSize size = reader.size();
    if (size.width() < 1 || size.height() < 1 || qint64(size.width()) * size.height() > 64 * 1024 * 1024)
        throw QObject::tr("Invalid GM82 image dimensions: %1").arg(path);
    return size;
}

QDomDocument Gm82ProjectImporter::readSprite(const QString &name)
{
    const QString directory = QStringLiteral("sprites/") + name + QLatin1Char('/');
    const auto properties = readProperties(directory + QStringLiteral("sprite.txt"));
    auto xml = document(QStringLiteral("sprite"));
    auto root = xml.documentElement();
    ActionXml::setText(root, QStringLiteral("type"), QStringLiteral("0"));
    gm82TextureGroup(root);
    fields(root, properties, QStringLiteral("origin_x:xorig|origin_y:yorigin|collision_shape:colkind|alpha_tolerance:coltolerance|bbox_type:bboxmode|bbox_left|bbox_right|bbox_top|bbox_bottom"));
    fields(root, properties, QStringLiteral("per_frame_colliders:sepmasks"), true);
    const qint64 count = properties.integer(QStringLiteral("frames"));
    if (count < 0 || count > 100000) throw QObject::tr("Invalid GM82 sprite frame count.");
    auto frames = ActionXml::child(root, QStringLiteral("frames"));
    QSize size(0, 0);
    for (int i = 0; i < count; ++i) {
        const QString source = directory + QString::number(i) + QStringLiteral(".png");
        const QSize frameSize = gm82ImageSize(sourcePath(source));
        if (i && size != frameSize) throw QObject::tr("Sprite frames have different sizes.");
        size = frameSize;
        const QString destination = QStringLiteral("images/%1_%2.png").arg(name).arg(i);
        copy(source, QStringLiteral("sprites/") + destination);
        auto frame = xml.createElement(QStringLiteral("frame"));
        frame.setAttribute(QStringLiteral("index"), i);
        frame.appendChild(xml.createTextNode(QString(destination).replace(QLatin1Char('/'), QLatin1Char('\\'))));
        frames.appendChild(frame);
    }
    ActionXml::setText(root, QStringLiteral("width"), QString::number(size.width()));
    ActionXml::setText(root, QStringLiteral("height"), QString::number(size.height()));
    return xml;
}

QDomDocument Gm82ProjectImporter::readBackground(const QString &name)
{
    const QString base = QStringLiteral("backgrounds/") + name;
    const auto properties = readProperties(base + QStringLiteral(".txt"));
    auto xml = document(QStringLiteral("background"));
    auto root = xml.documentElement();
    gm82TextureGroup(root);
    fields(root, properties, QStringLiteral("tileset:istileset"), true);
    fields(root, properties, QStringLiteral("tile_width:tilewidth|tile_height:tileheight|tile_hoffset:tilexoff|tile_voffset:tileyoff|tile_hsep:tilehsep|tile_vsep:tilevsep"));
    QSize size(0, 0);
    QString image;
    if (properties.boolean(QStringLiteral("exists"))) {
        size = gm82ImageSize(sourcePath(base + QStringLiteral(".png")));
        image = QStringLiteral("images/") + name + QStringLiteral(".png");
        copy(base + QStringLiteral(".png"), QStringLiteral("background/") + image);
    }
    ActionXml::setText(root, QStringLiteral("width"), QString::number(size.width()));
    ActionXml::setText(root, QStringLiteral("height"), QString::number(size.height()));
    ActionXml::setText(root, QStringLiteral("data"), image.replace(QLatin1Char('/'), QLatin1Char('\\')));
    return xml;
}

QDomDocument Gm82ProjectImporter::readSound(const QString &name)
{
    const auto properties = readProperties(QStringLiteral("sounds/") + name + QStringLiteral(".txt"));
    auto xml = document(QStringLiteral("sound"));
    auto root = xml.documentElement();
    QString extension = properties.text(QStringLiteral("extension"));
    while (extension.startsWith(QLatin1Char('.'))) extension.remove(0, 1);
    if (!extension.isEmpty()) validateName(extension);
    const QString fileName = name + (extension.isEmpty() ? QString() : QLatin1Char('.') + extension);
    const bool exists = properties.boolean(QStringLiteral("exists"));
    if (exists) copy(QStringLiteral("sounds/") + fileName, QStringLiteral("sound/audio/") + fileName);
    ActionXml::setText(root, QStringLiteral("extension"), extension.isEmpty() ? QString() : QLatin1Char('.') + extension);
    ActionXml::setText(root, QStringLiteral("data"), exists ? fileName : QString());
    ActionXml::setText(root, QStringLiteral("origname"), properties.text(QStringLiteral("source")));
    fields(root, properties, QStringLiteral("kind|effects"));
    fields(root, properties, QStringLiteral("preload"), true);
    for (const QString &key : {QStringLiteral("volume"), QStringLiteral("pan")}) {
        const QString value = properties.text(key, key == QStringLiteral("volume") ? QStringLiteral("1") : QStringLiteral("0"));
        bool ok;
        const double number = value.toDouble(&ok);
        if (!ok || !std::isfinite(number)) throw QObject::tr("%1: Invalid sound %2.").arg(properties.path, key);
        ActionXml::setText(key == QStringLiteral("volume") ? ActionXml::child(root, key) : root, key, value);
    }
    for (const QString &pair : QStringLiteral("bitRate:192|sampleRate:44100|type:0|bitDepth:16").split(QLatin1Char('|'))) {
        const QString key = pair.section(QLatin1Char(':'), 0, 0);
        ActionXml::setText(ActionXml::child(root, key + QLatin1Char('s')), key, pair.section(QLatin1Char(':'), 1));
    }
    for (const QString &key : QStringLiteral("compressed|streamed|uncompressOnLoad|audioGroup").split(QLatin1Char('|')))
        ActionXml::setText(root, key, QStringLiteral("0"));
    return xml;
}

QDomDocument Gm82ProjectImporter::readFont(const QString &name)
{
    const auto properties = readProperties(QStringLiteral("fonts/") + name + QStringLiteral(".txt"));
    auto xml = document(QStringLiteral("font"));
    auto root = xml.documentElement();
    ActionXml::setText(root, QStringLiteral("name"), properties.text(QStringLiteral("name")));
    fields(root, properties, QStringLiteral("size|charset|aa_level:aa"));
    fields(root, properties, QStringLiteral("bold|italic"), true);
    const int first = int(properties.integer(QStringLiteral("range_start"))), last = int(properties.integer(QStringLiteral("range_end")));
    if (first < 0 || last < first || last > 65535) throw QObject::tr("Invalid font character range.");
    ActionXml::setText(ActionXml::child(root, QStringLiteral("ranges")), QStringLiteral("range0"), QStringLiteral("%1,%2").arg(first).arg(last));
    ActionXml::setText(ActionXml::child(root, QStringLiteral("texgroups")), QStringLiteral("texgroup0"), QStringLiteral("0"));
    ActionXml::setText(root, QStringLiteral("renderhq"), QStringLiteral("0"));
    ActionXml::setText(root, QStringLiteral("includeTTF"), QStringLiteral("0"));
    ActionXml::setText(root, QStringLiteral("TTFName"), QString());
    return xml;
}

QDomDocument Gm82ProjectImporter::readPath(const QString &name)
{
    const QString directory = QStringLiteral("paths/") + name + QLatin1Char('/');
    const auto properties = readProperties(directory + QStringLiteral("path.txt"));
    auto xml = document(QStringLiteral("path"));
    auto root = xml.documentElement();
    fields(root, properties, QStringLiteral("connection:kind|precision|snap_x:hsnap|snap_y:vsnap"));
    fields(root, properties, QStringLiteral("closed"), true);
    const QString room = reference(QStringLiteral("rooms"), properties.text(QStringLiteral("background")));
    ActionXml::setText(root, QStringLiteral("backroom"), QString::number(m_roomOrder.indexOf(room)));
    auto points = ActionXml::child(root, QStringLiteral("points"));
    for (const QString &line : readText(directory + QStringLiteral("points.txt")).split(QLatin1Char('\n'))) {
        if (line.isEmpty()) continue;
        checkCancelled();
        const QStringList values = line.split(QLatin1Char(','));
        if (values.size() != 3) throw QObject::tr("Invalid GM82 path point: %1").arg(line);
        for (const QString &value : values) {
            bool ok;
            const double number = value.toDouble(&ok);
            if (!ok || !std::isfinite(number)) throw QObject::tr("Invalid GM82 path point: %1").arg(line);
        }
        auto point = xml.createElement(QStringLiteral("point"));
        point.appendChild(xml.createTextNode(line));
        points.appendChild(point);
    }
    return xml;
}
