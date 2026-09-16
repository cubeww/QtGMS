#ifndef QTGMS_IMAGERESIZER_H
#define QTGMS_IMAGERESIZER_H

#include <QImage>

class ImageResizer
{
public:
    enum class Quality { Poor, Normal, Good, VeryGood, Excellent };
    static QImage resize(const QImage &image, const QSize &size, Quality quality);
};

#endif
