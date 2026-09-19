#ifndef QTGMS_ROOMASSETS_H
#define QTGMS_ROOMASSETS_H
#include "project.h"
#include <QCache>
#include <QHash>
#include <QPixmap>
#include <QPoint>
struct RoomVisual {
    QPixmap image;
    QPoint origin;
    int depth = 0;
    bool visible = true;
    bool tileset = false;
    QSize tileSize = QSize(16, 16);
    QPoint tileOffset, tileSeparation;
};
class RoomAssets
{
public:
    explicit RoomAssets(const Project *project);
    void reload();
    void invalidate(ResourceType type, const QString &filePath);
    void trimCache();
    RoomVisual object(const QString &name);
    RoomVisual background(const QString &name);
    QPixmap colored(const QPixmap &source, quint32 color);
private:
    struct ObjectMetadata {
        QString spriteName;
        int depth = 0;
        bool visible = true;
    };
    void cacheVisual(const QString &key, const RoomVisual &visual);
    void removeVisual(const QString &key);
    const Project *m_project;
    QMap<QString, ResourceNode> m_objects, m_sprites, m_backgrounds;
    QHash<QString, ObjectMetadata> m_objectCache;
    QHash<QString, RoomVisual> m_visualCache;
    qint64 m_visualCost = 0;
    QCache<QString, QPixmap> m_colorCache;
};
#endif
