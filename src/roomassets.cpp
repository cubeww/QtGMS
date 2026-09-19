#include "roomassets.h"
#include "actionxml.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>

static QDomElement readRoomAsset(const QString &path)
{
    QFile file(path); QDomDocument xml;
    if (!file.open(QIODevice::ReadOnly) || !xml.setContent(&file)) return QDomElement();
    return xml.documentElement();
}
static QPixmap roomAssetImage(const QString &resourcePath, QString imagePath)
{
    imagePath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    QImageReader reader(QFileInfo(resourcePath).absoluteDir().absoluteFilePath(imagePath));
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 64 * 1024 * 1024) return QPixmap();
    return QPixmap::fromImage(reader.read());
}
static int roomVisualCost(const RoomVisual &visual)
{
    return int(qMax<qint64>(1, qint64(visual.image.width()) * visual.image.height() * visual.image.depth() / 8 / 1024));
}

RoomAssets::RoomAssets(const Project *project)
    : m_project(project), m_colorCache(64 * 1024) { reload(); }
void RoomAssets::reload()
{
    m_objects.clear(); m_sprites.clear(); m_backgrounds.clear(); m_objectCache.clear(); m_visualCache.clear(); m_colorCache.clear();
    m_visualCost = 0;
    for (const ResourceNode &node : ActionXml::resourceList(*m_project, ResourceType::Object)) m_objects.insert(node.name, node);
    for (const ResourceNode &node : ActionXml::resourceList(*m_project, ResourceType::Sprite)) m_sprites.insert(node.name, node);
    for (const ResourceNode &node : ActionXml::resourceList(*m_project, ResourceType::Background)) m_backgrounds.insert(node.name, node);
}
void RoomAssets::invalidate(ResourceType type, const QString &filePath)
{
    if (type != ResourceType::Object && type != ResourceType::Sprite && type != ResourceType::Background) return;
    for (const ResourceNode &node : ActionXml::resourceList(*m_project, type)) {
        if (node.filePath != filePath) continue;
        if (type == ResourceType::Object) {
            m_objects.insert(node.name, node);
            m_objectCache.remove(node.name);
        } else if (type == ResourceType::Sprite) {
            m_sprites.insert(node.name, node);
            removeVisual(QStringLiteral("sprite:") + node.name);
        } else {
            m_backgrounds.insert(node.name, node);
            removeVisual(QStringLiteral("background:") + node.name);
        }
        break;
    }
}
void RoomAssets::removeVisual(const QString &key)
{
    const auto found = m_visualCache.find(key);
    if (found == m_visualCache.end()) return;
    m_visualCost -= roomVisualCost(found.value());
    m_visualCache.erase(found);
}
void RoomAssets::cacheVisual(const QString &key, const RoomVisual &visual)
{
    removeVisual(key);
    m_visualCache.insert(key, visual);
    m_visualCost += roomVisualCost(visual);
    trimCache();
}
void RoomAssets::trimCache()
{
    static const qint64 MaxUnusedCost = 64 * 1024;
    if (m_visualCost <= MaxUnusedCost) return;
    // A QPixmap with other references is still used by a scene or preview.
    // Evicting its lookup entry would let the next instance decode a duplicate.
    qint64 unusedCost = 0;
    for (const auto &visual : m_visualCache)
        if (visual.image.isNull() || visual.image.isDetached()) unusedCost += roomVisualCost(visual);
    for (auto it = m_visualCache.begin(); it != m_visualCache.end() && unusedCost > MaxUnusedCost;) {
        if (it->image.isNull() || it->image.isDetached()) {
            const int cost = roomVisualCost(it.value());
            unusedCost -= cost;
            m_visualCost -= cost;
            it = m_visualCache.erase(it);
        } else ++it;
    }
}
RoomVisual RoomAssets::object(const QString &name)
{
    if (!m_objectCache.contains(name)) {
        const QDomElement root = readRoomAsset(m_objects.value(name).filePath);
        ObjectMetadata metadata;
        metadata.spriteName = ActionXml::text(root, QStringLiteral("spriteName"));
        metadata.depth = ActionXml::text(root, QStringLiteral("depth")).toInt();
        metadata.visible = ActionXml::text(root, QStringLiteral("visible")).toInt() != 0;
        m_objectCache.insert(name, metadata);
    }
    const ObjectMetadata metadata = m_objectCache.value(name);
    const QString key = QStringLiteral("sprite:") + metadata.spriteName;
    RoomVisual visual;
    const auto cached = m_visualCache.constFind(key);
    if (cached != m_visualCache.constEnd()) visual = cached.value();
    else {
        const ResourceNode sprite = m_sprites.value(metadata.spriteName); const QDomElement xml = readRoomAsset(sprite.filePath);
        visual.image = roomAssetImage(sprite.filePath, xml.firstChildElement(QStringLiteral("frames")).firstChildElement(QStringLiteral("frame")).text());
        visual.origin = QPoint(ActionXml::text(xml, QStringLiteral("xorig")).toInt(), ActionXml::text(xml, QStringLiteral("yorigin")).toInt());
        cacheVisual(key, visual);
    }
    visual.depth = metadata.depth; visual.visible = metadata.visible; return visual;
}
RoomVisual RoomAssets::background(const QString &name)
{
    const QString key = QStringLiteral("background:") + name;
    const auto cached = m_visualCache.constFind(key);
    if (cached != m_visualCache.constEnd()) return cached.value();
    const ResourceNode node = m_backgrounds.value(name); const QDomElement root = readRoomAsset(node.filePath);
    RoomVisual visual; visual.image = roomAssetImage(node.filePath, ActionXml::text(root, QStringLiteral("data")));
    visual.tileset = ActionXml::text(root, QStringLiteral("istileset")).toInt() != 0;
    visual.tileSize = QSize(qMax(1, ActionXml::text(root, QStringLiteral("tilewidth")).toInt()), qMax(1, ActionXml::text(root, QStringLiteral("tileheight")).toInt()));
    visual.tileOffset = QPoint(ActionXml::text(root, QStringLiteral("tilexoff")).toInt(), ActionXml::text(root, QStringLiteral("tileyoff")).toInt());
    visual.tileSeparation = QPoint(ActionXml::text(root, QStringLiteral("tilehsep")).toInt(), ActionXml::text(root, QStringLiteral("tilevsep")).toInt());
    cacheVisual(key, visual); return visual;
}
QPixmap RoomAssets::colored(const QPixmap &source, quint32 color)
{
    if ((color & 0xffffff) == 0xffffff || source.isNull()) return source;
    const QString key = QString::number(source.cacheKey()) + QLatin1Char(':') + QString::number(color & 0xffffff);
    if (auto *cached = m_colorCache.object(key)) return *cached;
    QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);
    const int r = color & 255, g = (color >> 8) & 255, b = (color >> 16) & 255;
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) line[x] = qRgba(qRed(line[x]) * r / 255, qGreen(line[x]) * g / 255, qBlue(line[x]) * b / 255, qAlpha(line[x]));
    }
    QPixmap result = QPixmap::fromImage(image); m_colorCache.insert(key, new QPixmap(result), qMax(1, image.byteCount() / 1024)); return result;
}
