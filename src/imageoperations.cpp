#include "imageoperations.h"
#include <QPainter>
#include <QtMath>
#include <deque>

static int channel(int value) { return qBound(0, value, 255); }

static double gradientPosition(int kind, double x, double y)
{
    switch (kind) {
    case 0: return x;
    case 1: return 1 - qAbs(2 * x - 1);
    case 2: return y;
    case 3: return 1 - qAbs(2 * y - 1);
    case 4: return qAbs(x - y);
    case 5: return qAbs(x + y - 1);
    case 6: return qMax(0.0, 1 - qSqrt(qPow(2 * x - 1, 2) + qPow(2 * y - 1, 2)));
    case 7: return 1 - qMax(qAbs(2 * x - 1), qAbs(2 * y - 1));
    case 8: return qMax(0.0, 1 - qSqrt(x * x + y * y));
    case 9: return qMax(0.0, 1 - qSqrt((1 - x) * (1 - x) + y * y));
    case 10: return qMax(0.0, 1 - qSqrt(x * x + (1 - y) * (1 - y)));
    case 11: return qMax(0.0, 1 - qSqrt((1 - x) * (1 - x) + (1 - y) * (1 - y)));
    }
    return x;
}
static QImage emptyImage(const QSize &size)
{
    if (size.width() <= 0 || size.height() <= 0 || size.width() > 8192 || size.height() > 8192)
        return QImage();
    QImage image(size, QImage::Format_ARGB32);
    if (!image.isNull()) image.fill(Qt::transparent);
    return image;
}

bool ImageOperations::matches(QRgb a, QRgb b, int tolerance, bool colorOnly)
{
    return qAbs(qRed(a) - qRed(b)) <= tolerance && qAbs(qGreen(a) - qGreen(b)) <= tolerance
        && qAbs(qBlue(a) - qBlue(b)) <= tolerance && (colorOnly || qAbs(qAlpha(a) - qAlpha(b)) <= tolerance);
}

QRect ImageOperations::opaqueBounds(const QImage &image)
{
    QRect bounds;
    const QImage pixels = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < pixels.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(pixels.constScanLine(y));
        int left = pixels.width(), right = -1;
        for (int x = 0; x < pixels.width(); ++x) if (qAlpha(row[x])) { left = qMin(left, x); right = x; }
        if (right >= left) bounds |= QRect(left, y, right - left + 1, 1);
    }
    return bounds;
}

// Sliding windows keep blur and outline costs linear in the number of pixels.
static QImage blurImage(const QImage &source, int radius)
{
    QImage result = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    radius = qBound(1, radius, 128);
    for (bool vertical : {false, true}) {
        QImage output(result.size(), result.format());
        if (output.isNull()) return QImage();
        const int length = vertical ? result.height() : result.width();
        const int lines = vertical ? result.width() : result.height();
        for (int line = 0; line < lines; ++line) {
            int sums[4] = {0, 0, 0, 0};
            auto add = [&](int index, int sign) {
                const QRgb pixel = vertical ? result.pixel(line, qBound(0, index, length - 1))
                                           : result.pixel(qBound(0, index, length - 1), line);
                sums[0] += sign * qRed(pixel); sums[1] += sign * qGreen(pixel);
                sums[2] += sign * qBlue(pixel); sums[3] += sign * qAlpha(pixel);
            };
            for (int i = -radius; i <= radius; ++i) add(i, 1);
            for (int i = 0; i < length; ++i) {
                const int divisor = radius * 2 + 1;
                const QRgb value = qRgba(sums[0] / divisor, sums[1] / divisor, sums[2] / divisor, sums[3] / divisor);
                output.setPixel(vertical ? line : i, vertical ? i : line, value);
                add(i - radius, -1); add(i + radius + 1, 1);
            }
        }
        result = output;
    }
    return result.convertToFormat(QImage::Format_ARGB32);
}

static QImage spreadAlpha(const QImage &source, int radius, bool shrink)
{
    QImage result = source;
    for (bool vertical : {false, true}) {
        QImage output = emptyImage(result.size());
        if (output.isNull()) return QImage();
        const int length = vertical ? result.height() : result.width();
        const int lines = vertical ? result.width() : result.height();
        for (int line = 0; line < lines; ++line) {
            std::deque<QPair<int, int>> pending;
            for (int i = -radius; i < length + radius; ++i) {
                const int alpha = i < 0 || i >= length ? 0
                    : qAlpha(vertical ? result.pixel(line, i) : result.pixel(i, line));
                while (!pending.empty() && (shrink ? pending.back().second >= alpha : pending.back().second <= alpha))
                    pending.pop_back();
                pending.push_back(qMakePair(i, alpha));
                while (pending.front().first < i - 2 * radius) pending.pop_front();
                const int position = i - radius;
                if (position >= 0) output.setPixel(vertical ? line : position, vertical ? position : line,
                    qRgba(255, 255, 255, pending.front().second));
            }
        }
        result = output;
    }
    return result;
}

QImage ImageOperations::apply(const QImage &source, Operation operation, const ImageOperationSettings &s)
{
    if (source.isNull()) return QImage();
    const QImage original = source.convertToFormat(QImage::Format_ARGB32);
    QImage result = original;
    const int width = original.width(), height = original.height();
    if (operation <= Operation::Trim) {
        if (operation == Operation::Mirror) return original.mirrored(s.wrapHorizontal, s.wrapVertical);
        if (operation == Operation::Stretch)
            return ImageResizer::resize(original, s.size, s.quality);
        if (operation == Operation::Trim) {
            QRect bounds = opaqueBounds(original);
            if (bounds.isEmpty()) bounds = QRect(0, 0, 1, 1);
            bounds.adjust(-s.radius, -s.radius, s.radius, s.radius);
            result = emptyImage(bounds.size());
            if (!result.isNull()) { QPainter painter(&result); painter.drawImage(-bounds.topLeft(), original); }
            return result;
        }
        result = emptyImage(operation == Operation::Resize ? s.size : original.size());
        if (result.isNull()) return result;
        if (operation == Operation::Shift) {
            for (int y = 0; y < height; ++y) {
                QRgb *row = reinterpret_cast<QRgb *>(result.scanLine(y));
                int sy = y - s.offsetY;
                if (s.wrapVertical) sy = (sy % height + height) % height;
                if (sy < 0 || sy >= height) continue;
                const QRgb *input = reinterpret_cast<const QRgb *>(original.constScanLine(sy));
                for (int x = 0; x < width; ++x) {
                    int sx = x - s.offsetX;
                    if (s.wrapHorizontal) sx = (sx % width + width) % width;
                    if (sx >= 0 && sx < width) row[x] = input[sx];
                }
            }
            return result;
        }
        QPainter painter(&result);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, s.smooth);
        if (operation == Operation::Resize) {
            painter.drawImage(QPoint((s.size.width() - width) * (s.anchor % 3) / 2,
                (s.size.height() - height) * (s.anchor / 3) / 2), original);
        } else {
            painter.translate(width / 2.0, height / 2.0);
            if (operation == Operation::Rotate) painter.rotate(s.clockwise ? s.amount : -s.amount);
            if (operation == Operation::Scale) painter.scale(s.amount / 100.0, s.secondary / 100.0);
            if (operation == Operation::Skew) painter.shear(s.amount / 100.0, s.secondary / 100.0);
            painter.drawImage(QPointF(-width / 2.0, -height / 2.0), original);
        }
        return result;
    }
    QImage blurred;
    if (operation == Operation::Blur || operation == Operation::Sharpen || operation == Operation::SmoothEdges) {
        if (operation != Operation::SmoothEdges && s.mode == 3) return original;
        blurred = blurImage(original, s.radius);
        if (blurred.isNull()) return QImage();
    }
    QImage alpha;
    if (operation == Operation::Alpha) {
        if (s.alphaImage.isNull()) return original;
        alpha = s.alphaImage.scaled(original.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_ARGB32);
    }
    if (operation == Operation::Outline || operation == Operation::Glow || operation == Operation::Shadow) {
        QImage mask = operation == Operation::Shadow ? original : spreadAlpha(original, s.radius, s.inside);
        if (mask.isNull()) return QImage();
        if (s.smooth || operation == Operation::Glow) mask = blurImage(mask, qMax(1, s.radius / 2));
        if (mask.isNull()) return QImage();
        result = emptyImage(original.size());
        if (result.isNull()) return result;
        for (int y = 0; y < height; ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(result.scanLine(y));
            for (int x = 0; x < width; ++x) {
                const int sx = x - (operation == Operation::Shadow ? s.offsetX : 0);
                const int sy = y - (operation == Operation::Shadow ? s.offsetY : 0);
                int a = mask.rect().contains(sx, sy) ? qAlpha(mask.pixel(sx, sy)) : 0;
                if (s.inside) a = qMax(0, qAlpha(original.pixel(x, y)) - a);
                else if (operation != Operation::Shadow) a = a * (255 - qAlpha(original.pixel(x, y))) / 255;
                row[x] = qRgba(s.color.red(), s.color.green(), s.color.blue(), channel(qRound(a * s.amount / 100) * s.color.alpha() / 255));
            }
        }
        if (!s.onlyEffect) {
            if (s.inside) {
                QImage combined = original; QPainter painter(&combined); painter.drawImage(0, 0, result); painter.end(); result = combined;
            } else { QPainter painter(&result); painter.drawImage(0, 0, original); }
        }
        return result;
    }
    if (operation == Operation::Button) {
        if (s.radius <= 0 || s.amount <= 0 || s.color.alpha() == 0) return original;
        QImage bevel = emptyImage(original.size());
        if (bevel.isNull()) return QImage();
        const QColor highlight = s.color.lighter(160), shadow = s.color.darker(200);
        for (int y = 0; y < height; ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(bevel.scanLine(y));
            for (int x = 0; x < width; ++x) {
                const int edge = qMin(qMin(x, width - x - 1), qMin(y, height - y - 1));
                if (edge >= s.radius) continue;
                const bool light = qMin(x, y) <= qMin(width - x - 1, height - y - 1);
                const QColor &tint = light ? highlight : shadow;
                const double coverage = s.amount / 100.0 * (s.smooth ? 1.0 - double(edge) / s.radius : 1.0);
                row[x] = qRgba(tint.red(), tint.green(), tint.blue(), channel(qRound(tint.alpha() * coverage)));
            }
        }
        // The bevel is a new layer, including where the source pixels are transparent.
        QPainter painter(&result);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.drawImage(0, 0, bevel);
        painter.end();
        return result;
    }
    for (int y = 0; y < height; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const QRgb pixel = original.pixel(x, y);
            int r = qRed(pixel), g = qGreen(pixel), b = qBlue(pixel), a = qAlpha(pixel);
            const QColor color = QColor::fromRgba(pixel);
            switch (operation) {
            case Operation::Grayscale: r = g = b = qGray(pixel); break;
            case Operation::Invert: r = 255 - r; g = 255 - g; b = 255 - b; break;
            case Operation::Opaque: a = 255; break;
            case Operation::Colorize:
            case Operation::ColorizePartial: {
                int hue = qMax(0, color.hsvHue());
                int distance = qAbs(hue - qRound(s.secondary)); distance = qMin(distance, 360 - distance);
                if (operation == Operation::ColorizePartial && distance > s.tolerance) break;
                hue = ((s.relative ? hue : 0) + qRound(s.amount)) % 360; if (hue < 0) hue += 360;
                const QColor changed = QColor::fromHsv(hue, color.hsvSaturation(), color.value());
                r = changed.red(); g = changed.green(); b = changed.blue(); break;
            }
            case Operation::Intensity: {
                const QColor changed = QColor::fromHsv(qMax(0, color.hsvHue()),
                    channel(qRound(color.hsvSaturation() * s.secondary / 100)), channel(qRound(color.value() * s.amount / 100)));
                r = changed.red(); g = changed.green(); b = changed.blue(); break;
            }
            case Operation::EraseColor: if (matches(pixel, s.color.rgba(), qRound(s.tolerance), true)) a = 0; break;
            case Operation::Opacity: if (a) a = s.relative ? channel(a + qRound(s.amount)) : channel(qRound(s.amount)); break;
            case Operation::Alpha: a = qGray(alpha.pixel(x, y)); break;
            case Operation::Fade:
                r = qRound(r + (s.color.red() - r) * s.amount / 100);
                g = qRound(g + (s.color.green() - g) * s.amount / 100);
                b = qRound(b + (s.color.blue() - b) * s.amount / 100); break;
            case Operation::SmoothEdges: a = qMin(a, qAlpha(blurred.pixel(x, y))); break;
            case Operation::Blur:
            case Operation::Sharpen: {
                const QRgb blur = blurred.pixel(x, y);
                auto adjusted = [&](int value, int average) { return operation == Operation::Blur ? average
                    : channel(qRound(value + (value - average) * s.amount / 100)); };
                if (s.mode != 2) { r = adjusted(r, qRed(blur)); g = adjusted(g, qGreen(blur)); b = adjusted(b, qBlue(blur)); }
                if (s.mode != 1) a = adjusted(a, qAlpha(blur));
                break;
            }
            case Operation::Gradient: {
                const double nx = double(x) / qMax(1, width - 1), ny = double(y) / qMax(1, height - 1);
                const double position = gradientPosition(s.mode, nx, ny);
                const double weight = s.replace ? 1.0 : s.amount / 255;
                r = qRound(r + (s.color.red() * (1 - position) + s.otherColor.red() * position - r) * weight);
                g = qRound(g + (s.color.green() * (1 - position) + s.otherColor.green() * position - g) * weight);
                b = qRound(b + (s.color.blue() * (1 - position) + s.otherColor.blue() * position - b) * weight);
                if (s.changeAlpha) a = qRound((s.color.alpha() * (1 - position) + s.otherColor.alpha() * position) * s.amount / 255);
                else if (s.replace) a = qRound(a * s.amount / 255);
                break;
            }
            default: break;
            }
            row[x] = qRgba(channel(r), channel(g), channel(b), channel(a));
        }
    }
    return result;
}
