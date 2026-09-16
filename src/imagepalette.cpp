#include "imagepalette.h"
#include "editorstandarddialogs.h"
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <QMouseEvent>
#include <QPainter>

static const int PaletteColumns = 6;
static const int PaletteRows = 12;
static const int PaletteCellSize = 18;
static const int PaletteWidth = PaletteColumns * PaletteCellSize;
static const int PaletteHeight = PaletteRows * PaletteCellSize;
static const int SpectrumTop = PaletteHeight + 8;

ImagePalette::ImagePalette(QWidget *parent) : QWidget(parent)
{
    setFixedSize(PaletteWidth, SpectrumTop + PaletteWidth);
    for (int y = 0; y < 12; ++y) for (int x = 0; x < 6; ++x) {
        if (y == 10) m_colors.append(QColor::fromHsv(0, 0, x * 51));
        else if (y == 11) m_colors.append(QColor::fromHsv(x * 60, 255, 255));
        else m_colors.append(QColor::fromHsv(x * 60, y < 5 ? 255 : 255 - (y - 4) * 42, y < 5 ? 255 - y * 40 : 255));
    }
    setToolTip(tr("Left / right click: select color. Middle click: edit palette color."));
}
void ImagePalette::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    for (int i = 0; i < m_colors.size(); ++i) {
        const QRect cell(i % PaletteColumns * PaletteCellSize, i / PaletteColumns * PaletteCellSize, PaletteCellSize, PaletteCellSize);
        painter.fillRect(cell, m_colors.at(i)); painter.setPen(Qt::black); painter.drawRect(cell.adjusted(0, 0, -1, -1));
    }
    const QRect spectrum(0, SpectrumTop, PaletteWidth, PaletteWidth);
    QImage image(spectrum.size(), QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const double horizontal = double(x) / qMax(1, image.width() - 1);
            row[x] = QColor::fromHsv(y * 359 / qMax(1, image.height() - 1), horizontal < 0.5 ? 255 : qRound((1 - horizontal) * 510),
                horizontal < 0.5 ? qRound(horizontal * 510) : 255).rgb();
        }
    }
    painter.drawImage(spectrum.topLeft(), image);
}
void ImagePalette::pick(const QPoint &position, Qt::MouseButton button)
{
    if (!rect().contains(position)) return;
    QColor color;
    if (position.y() < PaletteHeight) {
        const int index = position.y() / PaletteCellSize * PaletteColumns + position.x() / PaletteCellSize;
        color = m_colors.at(index);
        if (button == Qt::MiddleButton) {
            color = EditorColorDialog::getColor(color, this, tr("Palette Color"));
            if (color.isValid()) { m_colors[index] = color; update(); }
            return;
        }
    } else if (position.y() >= SpectrumTop) {
        const double horizontal = double(position.x()) / (PaletteWidth - 1);
        color = QColor::fromHsv((position.y() - SpectrumTop) * 359 / (PaletteWidth - 1),
            horizontal < 0.5 ? 255 : qRound((1 - horizontal) * 510), horizontal < 0.5 ? qRound(horizontal * 510) : 255);
    }
    if (color.isValid() && button != Qt::MiddleButton) emit colorSelected(color, button == Qt::RightButton);
}
void ImagePalette::mousePressEvent(QMouseEvent *event) { pick(event->pos(), event->button()); }
void ImagePalette::mouseMoveEvent(QMouseEvent *event)
{ if (event->buttons() & (Qt::LeftButton | Qt::RightButton)) pick(event->pos(), event->buttons() & Qt::RightButton ? Qt::RightButton : Qt::LeftButton); }
bool ImagePalette::save(const QString &path, QString &error) const
{
    QByteArray bytes("JASC-PAL\n0100\n72\n");
    for (const QColor &color : m_colors) bytes += QStringLiteral("%1 %2 %3\n").arg(color.red()).arg(color.green()).arg(color.blue()).toLatin1();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { error = file.errorString(); return false; }
    return true;
}
bool ImagePalette::load(const QString &path, QString &error)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    QTextStream stream(&file);
    if (stream.readLine().trimmed() != QStringLiteral("JASC-PAL") || stream.readLine().trimmed() != QStringLiteral("0100")) { error = tr("Expected a JASC-PAL palette."); return false; }
    bool valid; const int count = stream.readLine().toInt(&valid);
    if (!valid || count < 1 || count > 256) { error = tr("Invalid palette size."); return false; }
    QVector<QColor> colors;
    for (int i = 0; i < count; ++i) {
        int r = -1, g = -1, b = -1; stream >> r >> g >> b;
        if (stream.status() != QTextStream::Ok || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) { error = tr("Invalid palette color."); return false; }
        if (i < 72) colors.append(QColor(r, g, b));
    }
    while (colors.size() < 72) colors.append(Qt::white);
    m_colors = colors; update(); return true;
}
