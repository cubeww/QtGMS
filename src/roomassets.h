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
    QSize tileSize = QSize(16, 16);
    QPoint tileOffset, tileSeparation;
};
class RoomAssets
{
public:
    explicit RoomAssets(const Project *project);
    void reload();
    RoomVisual object(const QString &name);
    RoomVisual background(const QString &name);
    QPixmap colored(const QPixmap &source, quint32 color);
private:
    const Project *m_project;
    QMap<QString, ResourceNode> m_objects, m_sprites, m_backgrounds;
    QHash<QString, RoomVisual> m_objectCache, m_spriteCache, m_backgroundCache;
    QCache<QString, QPixmap> m_colorCache;
};
#endif
