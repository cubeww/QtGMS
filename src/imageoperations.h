#ifndef QTGMS_IMAGEOPERATIONS_H
#define QTGMS_IMAGEOPERATIONS_H
#include <QImage>
#include <QColor>
#include "imageresizer.h"

struct ImageOperationSettings {
    double amount = 100;
    double secondary = 100;
    double tolerance = 0;
    int radius = 1;
    int offsetX = 0;
    int offsetY = 0;
    int anchor = 4;
    int mode = 0;
    ImageResizer::Quality quality = ImageResizer::Quality::Normal;
    QSize size;
    QColor color = Qt::black;
    QColor otherColor = Qt::white;
    bool smooth = false;
    bool clockwise = true;
    bool inside = false;
    bool onlyEffect = false;
    bool relative = false;
    bool wrapHorizontal = false;
    bool wrapVertical = false;
    bool changeAlpha = false;
    bool replace = true;
    bool adjustOrigin = true;
    QImage alphaImage;
};

class ImageOperations
{
public:
    enum class Operation { Shift, Mirror, Rotate, Scale, Skew, Resize, Stretch, Trim,
        Grayscale, Colorize, ColorizePartial, Intensity, Invert, Opaque, EraseColor,
        SmoothEdges, Opacity, Alpha, Fade, Blur, Sharpen, Outline, Shadow, Glow, Button, Gradient };
    static QImage apply(const QImage &source, Operation operation, const ImageOperationSettings &settings);
    static QRect opaqueBounds(const QImage &image);
    static bool matches(QRgb a, QRgb b, int tolerance, bool colorOnly);
};
#endif
