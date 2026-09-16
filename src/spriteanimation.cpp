#include "spriteanimation.h"
#include "imageoperations.h"
#include <QPainter>
#include <QPolygonF>
#include <QTransform>
#include <QtMath>

QImage SpriteAnimation::interpolate(const QImage &first, const QImage &second, double amount)
{
    const QImage a = first.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage b = second.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (a.isNull() || b.isNull() || a.size() != b.size()) return QImage();
    QImage result(a.size(), QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) return result;
    const int weight = qBound(0, qRound(amount * 255), 255);
    for (int y = 0; y < a.height(); ++y) {
        const QRgb *left = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const QRgb *right = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        QRgb *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            const auto mix = [weight](int from, int to) { return (from * (255 - weight) + to * weight + 127) / 255; };
            out[x] = qRgba(mix(qRed(left[x]), qRed(right[x])), mix(qGreen(left[x]), qGreen(right[x])),
                mix(qBlue(left[x]), qBlue(right[x])), mix(qAlpha(left[x]), qAlpha(right[x])));
        }
    }
    return result.convertToFormat(QImage::Format_ARGB32);
}

QList<QImage> SpriteAnimation::generate(const QList<QImage> &images, const Settings &settings)
{
    QList<QImage> result;
    if (images.isEmpty() || settings.count < 1) return result;
    for (int index = 0; index < settings.count; ++index) {
        const QImage &source = images.at(index % images.size());
        const double progress = settings.count == 1 ? 1.0 : double(index) / (settings.count - 1);
        ImageOperationSettings operation;
        QImage image;
        switch (settings.kind) {
        case Kind::Translation:
            operation.offsetX = qRound(settings.horizontal * progress);
            operation.offsetY = qRound(settings.vertical * progress);
            operation.wrapHorizontal = operation.wrapVertical = true;
            image = ImageOperations::apply(source, ImageOperations::Operation::Shift, operation);
            break;
        case Kind::Rotation:
            operation.amount = settings.angle * (qFuzzyCompare(qAbs(settings.angle), 360.0)
                ? double(index) / settings.count : progress);
            operation.clockwise = settings.direction == 0;
            image = ImageOperations::apply(source, ImageOperations::Operation::Rotate, operation);
            break;
        case Kind::Colorize:
            operation.amount = qMax(0, settings.color.hue());
            image = interpolate(source, ImageOperations::apply(source, ImageOperations::Operation::Colorize, operation), progress);
            break;
        case Kind::Fade:
            operation.color = settings.color;
            operation.amount = progress * 100;
            image = ImageOperations::apply(source, ImageOperations::Operation::Fade, operation);
            break;
        case Kind::Disappear:
            image = source.convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < image.height(); ++y) {
                QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int x = 0; x < image.width(); ++x)
                    row[x] = qRgba(qRed(row[x]), qGreen(row[x]), qBlue(row[x]), qRound(qAlpha(row[x]) * (1 - progress)));
            }
            break;
        default: {
            const bool perspective = settings.kind == Kind::Flatten || settings.kind == Kind::Raise;
            const bool grow = settings.kind == Kind::Grow || settings.kind == Kind::Raise;
            const double scale = grow ? progress : 1 - progress;
            image = QImage(source.size(), QImage::Format_ARGB32);
            if (image.isNull()) return QList<QImage>();
            image.fill(Qt::transparent);
            if (scale <= 0) break;
            const double width = source.width(), height = source.height();
            const int direction = settings.direction;
            QPainter painter(&image);
            if (!perspective) {
                const double x = direction == 1 ? 0 : direction == 2 ? width * (1 - scale) : width * (1 - scale) / 2;
                const double y = direction == 3 ? 0 : direction == 4 ? height * (1 - scale) : height * (1 - scale) / 2;
                painter.drawImage(QRectF(x, y, width * scale, height * scale), source);
            } else {
                QPolygonF original, target;
                original << QPointF(0, 0) << QPointF(width, 0) << QPointF(width, height) << QPointF(0, height);
                target = original;
                const double insetX = width * (1 - scale) / 2, insetY = height * (1 - scale) / 2;
                if (direction == 1) {
                    target[1] = QPointF(width * scale, insetY); target[2] = QPointF(width * scale, height - insetY);
                } else if (direction == 2) {
                    target[0] = QPointF(width * (1 - scale), insetY); target[3] = QPointF(width * (1 - scale), height - insetY);
                } else if (direction == 3) {
                    target[2] = QPointF(width - insetX, height * scale); target[3] = QPointF(insetX, height * scale);
                } else {
                    target[0] = QPointF(insetX, height * (1 - scale)); target[1] = QPointF(width - insetX, height * (1 - scale));
                }
                QTransform transform;
                if (QTransform::quadToQuad(original, target, transform)) {
                    painter.setTransform(transform); painter.drawImage(QPoint(), source);
                }
            }
            break;
        }
        }
        if (image.isNull()) return QList<QImage>();
        result.append(image);
    }
    return result;
}
