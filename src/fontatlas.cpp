#include "fontatlas.h"
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QSet>
#include <QtMath>
#include <algorithm>

QFont FontAtlas::font(const FontState &state)
{
    QFont result(state.family); result.setPixelSize(qMax(1, qRound(state.size * 96.0 / 72.0)));
    result.setBold(state.bold); result.setItalic(state.italic); result.setKerning(false);
    result.setHintingPreference(state.highQuality ? QFont::PreferNoHinting : QFont::PreferFullHinting);
    result.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::NoFontMerging
        | (state.antiAlias ? QFont::PreferAntialias : QFont::NoAntialias)));
    return result;
}
bool FontAtlas::isFamilyAvailable(const QString &family)
{ return QFontDatabase().families().contains(family, Qt::CaseInsensitive); }
bool FontAtlas::build(const FontState &state, FontAtlasData &atlas, QString &error)
{
    error.clear(); atlas = FontAtlasData();
    if (!isFamilyAvailable(state.family)) { error = QObject::tr("The font '%1' is not installed. Install it or choose another font before rebuilding glyphs.").arg(state.family); return false; }
    const QFont renderFont = font(state); QFontMetrics metrics(renderFont);
    QSet<int> characters;
    for (const FontRange &range : state.ranges) {
        if (range.first < 0 || range.last > 65535 || range.first > range.last) { error = QObject::tr("Invalid font character range."); return false; }
        for (int code = range.first; code <= range.last; ++code)
            if (code < 0xd800 || code > 0xdfff) characters.insert(code);
    }
    QList<int> ordered = characters.values(); std::sort(ordered.begin(), ordered.end());
    qint64 area = 0; int widest = 1;
    for (int code : ordered) {
        const QChar ch(static_cast<ushort>(code)); const QRect bounds = metrics.boundingRect(ch);
        const int width = qMax(1, bounds.width()); const int height = qMax(1, metrics.ascent() + qMax(0, bounds.bottom() + 1));
        FontGlyph glyph; glyph.character = code; glyph.rectangle = QRect(0, 0, width, height);
        glyph.shift = metrics.width(ch); glyph.offset = bounds.left(); atlas.glyphs.append(glyph);
        area += qint64(width + 2) * (height + 2); widest = qMax(widest, width + 2);
    }
    const int MaxAtlasSize = 4096;
    int width = 32;
    while (width < MaxAtlasSize && (width < widest || qint64(width) * width < area)) width *= 2;
    if (widest > MaxAtlasSize || area > qint64(MaxAtlasSize) * MaxAtlasSize) {
        error = QObject::tr("These characters exceed a 4096 x 4096 font page. Reduce the size or character ranges."); return false;
    }
    int usedHeight = 0;
    for (;;) {
        int x = 1, y = 1, rowHeight = 0;
        for (FontGlyph &glyph : atlas.glyphs) {
            if (x + glyph.rectangle.width() + 1 > width) { x = 1; y += rowHeight; rowHeight = 0; }
            glyph.rectangle.moveTopLeft(QPoint(x, y)); x += glyph.rectangle.width() + 2;
            rowHeight = qMax(rowHeight, glyph.rectangle.height() + 2);
        }
        usedHeight = y + rowHeight;
        if (usedHeight <= MaxAtlasSize || width == MaxAtlasSize) break;
        width *= 2;
    }
    if (usedHeight > MaxAtlasSize) { error = QObject::tr("The font page is full. Reduce the size or character ranges."); return false; }
    int height = 32; while (height < usedHeight) height *= 2;
    atlas.image = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
    if (atlas.image.isNull()) { error = QObject::tr("Not enough memory for the font page."); return false; }
    atlas.image.fill(Qt::transparent); QPainter page(&atlas.image);
    const int scale = qMax(1, state.antiAlias);
    for (const FontGlyph &glyph : atlas.glyphs) {
        QImage tile(glyph.rectangle.size() * scale, QImage::Format_ARGB32_Premultiplied);
        if (tile.isNull()) { error = QObject::tr("Not enough memory to render a glyph."); return false; }
        tile.fill(Qt::transparent);
        QPainter painter(&tile); painter.scale(scale, scale); painter.setFont(renderFont);
        painter.setRenderHint(QPainter::TextAntialiasing, state.antiAlias != 0); painter.setPen(Qt::white);
        painter.drawText(QPoint(-glyph.offset, metrics.ascent()), QString(QChar(static_cast<ushort>(glyph.character)))); painter.end();
        if (scale > 1) tile = tile.scaled(glyph.rectangle.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        page.drawImage(glyph.rectangle.topLeft(), tile);
    }
    return true;
}
