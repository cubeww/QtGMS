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
RoomAssets::RoomAssets(const Project *project) : m_project(project), m_colorCache(64 * 1024) { reload(); }
void RoomAssets::reload()
{
    m_objects.clear(); m_sprites.clear(); m_backgrounds.clear(); m_objectCache.clear(); m_spriteCache.clear(); m_backgroundCache.clear(); m_colorCache.clear();
    for (const ResourceNode &node : ActionXml::resourceList(*m_project, ResourceType::Object)) m_objects.insert(node.name, node);
    for (const ResourceNode &node : ActionXml::resourceList(*m_project, ResourceType::Sprite)) m_sprites.insert(node.name, node);
    for (const ResourceNode &node : ActionXml::resourceList(*m_project, ResourceType::Background)) m_backgrounds.insert(node.name, node);
}
RoomVisual RoomAssets::object(const QString &name)
{
    if (m_objectCache.contains(name)) return m_objectCache.value(name);
    const ResourceNode node = m_objects.value(name); const QDomElement root = readRoomAsset(node.filePath);
    const QString spriteName = ActionXml::text(root, QStringLiteral("spriteName"));
    if (!m_spriteCache.contains(spriteName)) {
        const ResourceNode sprite = m_sprites.value(spriteName); const QDomElement xml = readRoomAsset(sprite.filePath);
        RoomVisual visual; visual.image = roomAssetImage(sprite.filePath, xml.firstChildElement(QStringLiteral("frames")).firstChildElement(QStringLiteral("frame")).text());
        visual.origin = QPoint(ActionXml::text(xml, QStringLiteral("xorig")).toInt(), ActionXml::text(xml, QStringLiteral("yorigin")).toInt());
        m_spriteCache.insert(spriteName, visual);
    }
    RoomVisual visual = m_spriteCache.value(spriteName); visual.depth = ActionXml::text(root, QStringLiteral("depth")).toInt();
    visual.visible = ActionXml::text(root, QStringLiteral("visible")).toInt() != 0; m_objectCache.insert(name, visual); return visual;
}
RoomVisual RoomAssets::background(const QString &name)
{
    if (m_backgroundCache.contains(name)) return m_backgroundCache.value(name);
    const ResourceNode node = m_backgrounds.value(name); const QDomElement root = readRoomAsset(node.filePath);
    RoomVisual visual; visual.image = roomAssetImage(node.filePath, ActionXml::text(root, QStringLiteral("data")));
    visual.tileSize = QSize(qMax(1, ActionXml::text(root, QStringLiteral("tilewidth")).toInt()), qMax(1, ActionXml::text(root, QStringLiteral("tileheight")).toInt()));
    visual.tileOffset = QPoint(ActionXml::text(root, QStringLiteral("tilexoff")).toInt(), ActionXml::text(root, QStringLiteral("tileyoff")).toInt());
    visual.tileSeparation = QPoint(ActionXml::text(root, QStringLiteral("tilehsep")).toInt(), ActionXml::text(root, QStringLiteral("tilevsep")).toInt());
    m_backgroundCache.insert(name, visual); return visual;
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
