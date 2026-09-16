#ifndef QTGMS_FONTATLAS_H
#define QTGMS_FONTATLAS_H

#include "fontdocument.h"
#include <QFont>
#include <QImage>
#include <QRect>

struct FontGlyph {
    int character;
    QRect rectangle;
    int shift;
    int offset;
};
struct FontAtlasData {
    QImage image;
    QVector<FontGlyph> glyphs;
};
class FontAtlas
{
public:
    static QFont font(const FontState &state);
    static bool isFamilyAvailable(const QString &family);
    static bool build(const FontState &state, FontAtlasData &atlas, QString &error);
};

#endif
