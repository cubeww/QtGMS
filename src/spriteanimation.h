#ifndef QTGMS_SPRITEANIMATION_H
#define QTGMS_SPRITEANIMATION_H
#include <QColor>
#include <QImage>
#include <QList>

class SpriteAnimation
{
public:
    enum class Kind { Translation, Rotation, Colorize, Fade, Disappear, Shrink, Grow, Flatten, Raise };
    struct Settings {
        Kind kind = Kind::Translation;
        int count = 10;
        int direction = 0;
        int horizontal = 0, vertical = 0;
        double angle = 360;
        QColor color = Qt::white;
    };
    static QList<QImage> generate(const QList<QImage> &images, const Settings &settings);
    static QImage interpolate(const QImage &first, const QImage &second, double amount);
};
#endif
