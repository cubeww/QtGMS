#include "spriteframeswindow.h"
#include "spritedocument.h"
#include "imageoperationdialog.h"
#include "editorstandarddialogs.h"
#include "imageresizer.h"
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QDataStream>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QListWidget>
#include <QFileDialog>
#include <QPainter>
#include <QComboBox>
#include <algorithm>

static void addSpriteDialogButtons(EditorDialog &dialog, QFormLayout *layout)
{
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setIcon(QIcon(QStringLiteral(":/images/editor/ok.png")));
    buttons->button(QDialogButtonBox::Cancel)->setIcon(QIcon(QStringLiteral(":/images/close.png")));
    layout->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
}

static QPushButton *spriteColorButton(QWidget *parent, QColor &color)
{
    auto *button = new QPushButton(parent);
    button->setFixedSize(92, 24);
    const auto update = [button, &color] { button->setStyleSheet(QStringLiteral("background-color: %1;").arg(color.name())); };
    update();
    QObject::connect(button, &QPushButton::clicked, parent, [parent, &color, update] {
        const QColor chosen = EditorColorDialog::getColor(color, parent);
        if (chosen.isValid()) { color = chosen; update(); }
    });
    return button;
}

void SpriteFramesWindow::copyFrames(bool cut)
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) return;
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_5_6);
    stream << quint32(rows.size());
    for (int row : rows) stream << m_document->state().frames.at(row).image;
    auto *mime = new QMimeData;
    mime->setImageData(m_document->state().frames.at(rows.first()).image);
    mime->setData(QStringLiteral("application/x-qtgms-sprite-frames"), bytes);
    QApplication::clipboard()->setMimeData(mime);
    if (cut) deleteFrame();
}

void SpriteFramesWindow::pasteFrames()
{
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime) return;
    QList<SpriteFrame> frames;
    if (mime->hasFormat(QStringLiteral("application/x-qtgms-sprite-frames"))) {
        const QByteArray bytes = mime->data(QStringLiteral("application/x-qtgms-sprite-frames"));
        QDataStream stream(bytes); stream.setVersion(QDataStream::Qt_5_6);
        quint32 count = 0; stream >> count;
        if (count > 10000) return;
        for (quint32 index = 0; index < count; ++index) {
            QImage image; stream >> image;
            if (stream.status() != QDataStream::Ok || image.isNull()) return;
            frames.append(SpriteDocument::createFrame(image));
        }
    } else if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (!image.isNull()) frames.append(SpriteDocument::createFrame(image));
    }
    if (frames.isEmpty()) return;
    insertFrames(frames, false, false, tr("Paste subimages"));
}

void SpriteFramesWindow::insertFrames(const QList<SpriteFrame> &imported, bool replace, bool append, const QString &title)
{
    if (imported.isEmpty()) return;
    QList<SpriteFrame> frames = imported;
    SpriteState state = m_document->state();
    if (replace || state.frames.isEmpty()) {
        state.frames = frames; state.size = frames.first().image.size();
        m_document->edit(state, title); m_frames->setCurrentRow(0); return;
    }
    bool differentSize = false;
    QSize largest = state.size;
    for (const SpriteFrame &frame : frames) {
        differentSize |= frame.image.size() != state.size;
        largest = largest.expandedTo(frame.image.size());
    }
    int mode = 0, anchor = 4;
    if (differentSize) {
        EditorDialog dialog(this); dialog.setWindowTitle(tr("Inserting Images"));
        auto *layout = new QFormLayout(dialog.bodyWidget());
        auto *method = new QComboBox;
        method->addItems({tr("Keep sprite size"), tr("Stretch new images"), tr("Enlarge sprite canvas")});
        layout->addRow(tr("Size:"), method);
        auto *position = new QComboBox;
        position->addItems({tr("Top left"), tr("Top"), tr("Top right"), tr("Left"), tr("Center"),
            tr("Right"), tr("Bottom left"), tr("Bottom"), tr("Bottom right")});
        position->setCurrentIndex(4); layout->addRow(tr("Position:"), position);
        connect(method, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), position,
            [position](int index) { position->setEnabled(index != 1); });
        addSpriteDialogButtons(dialog, layout);
        if (dialog.exec() != QDialog::Accepted) return;
        mode = method->currentIndex(); anchor = position->currentIndex();
    }
    ImageOperationSettings settings; settings.size = mode == 2 ? largest : state.size; settings.anchor = anchor;
    if (mode == 2 && largest != state.size) {
        for (SpriteFrame &frame : state.frames) {
            frame.image = ImageOperations::apply(frame.image, ImageOperations::Operation::Resize, settings); frame.modified = true;
            if (frame.image.isNull()) { EditorMessageBox::critical(this, title, tr("Not enough memory to resize the images.")); return; }
        }
        state.origin += QPoint((largest.width() - state.size.width()) * (anchor % 3) / 2,
            (largest.height() - state.size.height()) * (anchor / 3) / 2);
        state.size = largest;
    }
    for (SpriteFrame &frame : frames) {
        if (frame.image.size() == state.size) continue;
        frame.image = ImageOperations::apply(frame.image, mode == 1 ? ImageOperations::Operation::Stretch : ImageOperations::Operation::Resize, settings);
        if (frame.image.isNull()) { EditorMessageBox::critical(this, title, tr("Not enough memory to resize the images.")); return; }
    }
    const int row = append ? state.frames.size() : qMax(0, m_frames->currentRow());
    for (int index = 0; index < frames.size(); ++index) state.frames.insert(row + index, frames.at(index));
    m_document->edit(state, title);
    m_frames->clearSelection();
    for (int index = row; index < row + frames.size(); ++index) m_frames->item(index)->setSelected(true);
    m_frames->setCurrentRow(row, QItemSelectionModel::NoUpdate);
}

void SpriteFramesWindow::eraseFrames()
{
    if (selectedRows().isEmpty()) return;
    EditorDialog dialog(this); dialog.setWindowTitle(tr("Erase Images"));
    auto *layout = new QFormLayout(dialog.bodyWidget());
    QColor color = Qt::white;
    layout->addRow(tr("Color:"), spriteColorButton(&dialog, color));
    auto *opacity = new QSpinBox; opacity->setRange(0, 255); opacity->setValue(0);
    layout->addRow(tr("Opacity (alpha):"), opacity);
    auto *all = new QCheckBox(tr("Apply to all images")); layout->addRow(all);
    addSpriteDialogButtons(dialog, layout);
    if (dialog.exec() != QDialog::Accepted) return;
    color.setAlpha(opacity->value());
    SpriteState state = m_document->state();
    const QList<int> rows = selectedRows();
    for (int index = 0; index < state.frames.size(); ++index) {
        if (!all->isChecked() && !rows.contains(index)) continue;
        state.frames[index].image.fill(color); state.frames[index].modified = true;
    }
    m_document->edit(state, tr("Erase images"));
}

void SpriteFramesWindow::setTransparencyBackground()
{
    EditorDialog dialog(this); dialog.setWindowTitle(tr("Transparency Background"));
    auto *layout = new QFormLayout(dialog.bodyWidget());
    QColor first = m_checkerFirst, second = m_checkerSecond;
    layout->addRow(tr("Color 1:"), spriteColorButton(&dialog, first));
    layout->addRow(tr("Color 2:"), spriteColorButton(&dialog, second));
    auto *pattern = new QCheckBox(tr("Use checker pattern")); pattern->setChecked(first != second); layout->addRow(pattern);
    auto *size = new QSpinBox; size->setRange(1, 64); size->setValue(m_checkerSize); layout->addRow(tr("Block size:"), size);
    addSpriteDialogButtons(dialog, layout);
    if (dialog.exec() != QDialog::Accepted) return;
    m_checkerFirst = first; m_checkerSecond = pattern->isChecked() ? second : first; m_checkerSize = size->value();
    refresh();
}

void SpriteFramesWindow::applyOperation(ImageOperations::Operation operation, const QString &title)
{
    SpriteState state = m_document->state();
    if (state.frames.isEmpty()) return;
    using Operation = ImageOperations::Operation;
    const bool sizeChange = operation == Operation::Resize || operation == Operation::Stretch || operation == Operation::Trim;
    ImageOperationDialog dialog(state.frames.at(qMax(0, m_frames->currentRow())).image, operation, title,
        operation == Operation::Button ? Qt::white : Qt::black, Qt::white, this, sizeChange);
    auto *all = new QCheckBox(tr("Apply to all images"), &dialog); all->setChecked(sizeChange || selectedRows().isEmpty());
    if (!sizeChange) {
        auto *layout = qobject_cast<QVBoxLayout *>(dialog.bodyWidget()->layout());
        layout->insertWidget(layout->count() - 1, all);
    } else all->hide();
    if (dialog.exec() != QDialog::Accepted) return;
    const ImageOperationSettings &settings = dialog.settings();
    const QList<int> rows = selectedRows();
    QRect trimBounds;
    if (operation == Operation::Trim) {
        for (const SpriteFrame &frame : state.frames) trimBounds = trimBounds.united(ImageOperations::opaqueBounds(frame.image));
        if (trimBounds.isEmpty()) trimBounds = QRect(0, 0, 1, 1);
        trimBounds.adjust(-settings.radius, -settings.radius, settings.radius, settings.radius);
        if (trimBounds.isEmpty() || trimBounds.width() > 8192 || trimBounds.height() > 8192) {
            EditorMessageBox::warning(this, title, tr("The resulting image size must be between 1 and 8192 pixels.")); return;
        }
    }
    for (int index = 0; index < state.frames.size(); ++index) {
        if (!all->isChecked() && !rows.contains(index)) continue;
        SpriteFrame &frame = state.frames[index];
        const QImage result = operation == Operation::Trim ? frame.image.copy(trimBounds) : ImageOperations::apply(frame.image, operation, settings);
        if (result.isNull()) { EditorMessageBox::critical(this, title, tr("Not enough memory to process the images.")); return; }
        frame.image = result; frame.modified = true;
    }
    if (sizeChange) {
        const QSize oldSize = state.size; state.size = state.frames.first().image.size();
        if (settings.adjustOrigin) {
            if (operation == Operation::Trim) state.origin -= trimBounds.topLeft();
            else if (operation == Operation::Resize) state.origin += QPoint(
                (state.size.width() - oldSize.width()) * (settings.anchor % 3) / 2,
                (state.size.height() - oldSize.height()) * (settings.anchor / 3) / 2);
            else state.origin = QPoint(qRound(double(state.origin.x()) * state.size.width() / oldSize.width()),
                qRound(double(state.origin.y()) * state.size.height() / oldSize.height()));
        }
    }
    m_document->edit(state, title);
}

void SpriteFramesWindow::premultiplyAlpha()
{
    SpriteState state = m_document->state();
    for (int index : selectedRows()) {
        QImage &image = state.frames[index].image;
        image = image.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const int alpha = qAlpha(row[x]);
                row[x] = qRgba((qRed(row[x]) * alpha + 127) / 255, (qGreen(row[x]) * alpha + 127) / 255,
                    (qBlue(row[x]) * alpha + 127) / 255, alpha);
            }
        }
        state.frames[index].modified = true;
    }
    m_document->edit(state, tr("Pre-multiply alpha"));
}

void SpriteFramesWindow::cycleFrames(int direction)
{
    SpriteState state = m_document->state();
    if (state.frames.size() < 2) return;
    if (direction < 0) state.frames.append(state.frames.takeFirst());
    else state.frames.prepend(state.frames.takeLast());
    m_document->edit(state, tr("Cycle images"));
}

void SpriteFramesWindow::reverseAnimation(bool append)
{
    SpriteState state = m_document->state();
    if (state.frames.isEmpty()) return;
    if (append) {
        for (int index = state.frames.size() - 1; index >= 0; --index)
            state.frames.append(SpriteDocument::createFrame(state.frames.at(index).image));
    } else std::reverse(state.frames.begin(), state.frames.end());
    m_document->edit(state, append ? tr("Add reverse animation") : tr("Reverse animation"));
}

void SpriteFramesWindow::changeAnimationLength(bool stretch)
{
    SpriteState state = m_document->state();
    const int previousCount = state.frames.size();
    if (!previousCount) return;
    bool accepted;
    const QString title = stretch ? tr("Stretch Animation") : tr("Set Animation Length");
    const int count = EditorInputDialog::getInt(this, title, tr("Number of frames:"), previousCount, 1, 10000, 1, &accepted);
    if (!accepted || count == previousCount) return;
    const QList<SpriteFrame> previous = state.frames; state.frames.clear();
    for (int index = 0; index < count; ++index)
        state.frames.append(SpriteDocument::createFrame(previous.at(stretch ? int(qint64(index) * previousCount / count) : index % previousCount).image));
    m_document->edit(state, title);
}

void SpriteFramesWindow::createAnimation(SpriteAnimation::Kind kind, int direction)
{
    SpriteState state = m_document->state();
    if (state.frames.isEmpty()) return;
    using Kind = SpriteAnimation::Kind;
    const QStringList titles = {tr("Translation Sequence"), tr("Rotation Sequence"), tr("Colorize Animation"),
        tr("Fade to Color"), tr("Disappear"), tr("Shrink"), tr("Grow"), tr("Flatten"), tr("Raise")};
    const QString title = titles.at(int(kind));
    EditorDialog dialog(this); dialog.setWindowTitle(title);
    auto *layout = new QFormLayout(dialog.bodyWidget());
    const auto number = [layout](const QString &label, int minimum, int maximum, int value) {
        auto *spin = new QSpinBox; spin->setRange(minimum, maximum); spin->setValue(value); layout->addRow(label, spin); return spin;
    };
    auto *count = number(tr("Number of frames:"), 1, 10000, qMax(10, state.frames.size()));
    QSpinBox *horizontal = nullptr, *vertical = nullptr, *angle = nullptr;
    if (kind == Kind::Translation) {
        horizontal = number(tr("Horizontal movement:"), -8192, 8192, state.size.width());
        vertical = number(tr("Vertical movement:"), -8192, 8192, 0);
    }
    if (kind == Kind::Rotation) angle = number(tr("Angle (degrees):"), 0, 3600, 360);
    QColor color = Qt::white;
    if (kind == Kind::Colorize || kind == Kind::Fade) layout->addRow(tr("Color:"), spriteColorButton(&dialog, color));
    addSpriteDialogButtons(dialog, layout);
    if (dialog.exec() != QDialog::Accepted) return;
    if (qint64(state.size.width()) * state.size.height() * 4 * count->value() > 512 * 1024 * 1024) {
        EditorMessageBox::warning(this, title, tr("This sequence would exceed 512 MB. Reduce the frame count or sprite size.")); return;
    }
    SpriteAnimation::Settings settings; settings.kind = kind; settings.direction = direction; settings.count = count->value();
    settings.color = color; if (angle) settings.angle = angle->value();
    if (horizontal) { settings.horizontal = horizontal->value(); settings.vertical = vertical->value(); }
    QList<QImage> source;
    for (const SpriteFrame &frame : state.frames) source.append(frame.image);
    const QList<QImage> generated = SpriteAnimation::generate(source, settings);
    if (generated.size() != settings.count) { EditorMessageBox::critical(this, title, tr("Not enough memory to create the animation.")); return; }
    state.frames.clear();
    for (const QImage &image : generated) state.frames.append(SpriteDocument::createFrame(image));
    m_document->edit(state, title);
}

void SpriteFramesWindow::blendAnimation(bool morph)
{
    SpriteState state = m_document->state();
    if (state.frames.isEmpty()) return;
    const QString title = morph ? tr("Morph Animation") : tr("Overlay Animation");
    const QString path = QFileDialog::getOpenFileName(this, title, QString(), tr("Images (*.png *.bmp *.jpg *.gif *.tif *.tiff)"));
    if (path.isEmpty()) return;
    QList<SpriteFrame> overlay; QString error;
    if (!SpriteDocument::importImages({path}, overlay, error)) { EditorMessageBox::critical(this, title, error); return; }
    for (SpriteFrame &frame : overlay) {
        if (frame.image.size() != state.size) frame.image = ImageResizer::resize(frame.image, state.size, ImageResizer::Quality::Normal);
        if (frame.image.isNull()) { EditorMessageBox::critical(this, title, tr("Not enough memory to resize the image.")); return; }
    }
    if (morph) {
        bool accepted;
        const int count = EditorInputDialog::getInt(this, title, tr("Number of frames:"), qMax(10, state.frames.size()), 1, 10000, 1, &accepted);
        if (!accepted) return;
        if (qint64(state.size.width()) * state.size.height() * 4 * count > 512 * 1024 * 1024) {
            EditorMessageBox::warning(this, title, tr("This sequence would exceed 512 MB. Reduce the frame count or sprite size.")); return;
        }
        const QList<SpriteFrame> previous = state.frames;
        state.frames.clear();
        for (int index = 0; index < count; ++index)
            state.frames.append(SpriteDocument::createFrame(previous.at(index % previous.size()).image));
    }
    for (int index = 0; index < state.frames.size(); ++index) {
        SpriteFrame &frame = state.frames[index];
        const QImage &target = overlay.at(index % overlay.size()).image;
        if (morph) frame.image = SpriteAnimation::interpolate(frame.image, target,
            state.frames.size() == 1 ? 1.0 : double(index) / (state.frames.size() - 1));
        else { QPainter painter(&frame.image); painter.drawImage(QPoint(), target); }
        if (frame.image.isNull()) { EditorMessageBox::critical(this, title, tr("Not enough memory to process the animation.")); return; }
        frame.modified = true;
    }
    m_document->edit(state, title);
}
