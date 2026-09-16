#include "imageresizer.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../thirdparty/stb/stb_image_resize2.h"

QImage ImageResizer::resize(const QImage &image, const QSize &size, Quality quality)
{
    if (image.isNull() || size.width() < 1 || size.height() < 1 || size.width() > 8192 || size.height() > 8192)
        return QImage();
    if (image.size() == size) return image;
    const QImage source = image.convertToFormat(QImage::Format_RGBA8888);
    QImage result(size, QImage::Format_RGBA8888);
    if (source.isNull() || result.isNull()) return QImage();
    static const stbir_filter Filters[] = {STBIR_FILTER_POINT_SAMPLE, STBIR_FILTER_TRIANGLE,
        STBIR_FILTER_CUBICBSPLINE, STBIR_FILTER_CATMULLROM, STBIR_FILTER_MITCHELL};
    if (!stbir_resize(source.constBits(), source.width(), source.height(), source.bytesPerLine(),
        result.bits(), result.width(), result.height(), result.bytesPerLine(), STBIR_RGBA,
        STBIR_TYPE_UINT8, STBIR_EDGE_CLAMP, Filters[static_cast<int>(quality)])) return QImage();
    return result.convertToFormat(QImage::Format_ARGB32);
}
