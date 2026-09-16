#include "editortheme.h"
#include "editorstyle.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QPalette>
#include <QImage>

QImage EditorTheme::tintActionImage(const QImage &source)
{
    QImage image = source.convertToFormat(QImage::Format_ARGB32);
    // Use the same warm-color to green conversion as the extracted toolbar
    // assets. Keep transparency, neutral shading and other hues intact.
    for (int y = 0; y < image.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = QColor::fromRgba(line[x]);
            if (!color.alpha() || color.hsvSaturationF() <= 0.3 || color.hsvHue() < 0 || color.hsvHue() >= 70) continue;
            const qreal value = color.value() * 0.82, saturation = color.hsvSaturationF();
            line[x] = qRgba(qRound(value * (1 - saturation * 0.5)), qRound(value), qRound(value * (1 - saturation)), color.alpha());
        }
    }
    return image;
}

void EditorTheme::apply(QApplication &application)
{
    application.setStyle(new EditorStyle);
    // Keep the reference's 11-pixel size, using an outline font for scaling.
    QFont interfaceFont(QStringLiteral("Tahoma"));
    interfaceFont.setPixelSize(11);
    application.setFont(interfaceFont);

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(56, 56, 56));
    palette.setColor(QPalette::WindowText, QColor(221, 221, 221));
    palette.setColor(QPalette::Base, QColor(33, 33, 33));
    palette.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
    palette.setColor(QPalette::Text, QColor(221, 221, 221));
    palette.setColor(QPalette::Button, QColor(80, 80, 80));
    palette.setColor(QPalette::ButtonText, QColor(221, 221, 221));
    palette.setColor(QPalette::Light, QColor(174, 174, 164));
    palette.setColor(QPalette::Midlight, QColor(108, 108, 104));
    palette.setColor(QPalette::Mid, QColor(77, 77, 73));
    palette.setColor(QPalette::Dark, QColor(24, 24, 24));
    palette.setColor(QPalette::Shadow, QColor(16, 16, 16));
    palette.setColor(QPalette::Highlight, QColor(0, 120, 215));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor(255, 255, 225));
    palette.setColor(QPalette::ToolTipText, Qt::black);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(128, 128, 128));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(128, 128, 128));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(128, 128, 128));
    application.setPalette(palette);

    QFile styleFile(QStringLiteral(":/styles/editor.qss"));
    if (!styleFile.open(QIODevice::ReadOnly))
        qFatal("Cannot read the embedded editor stylesheet.");
    application.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
}
