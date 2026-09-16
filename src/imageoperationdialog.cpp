#include "imageoperationdialog.h"
#include "imagecanvas.h"
#include "editorstandarddialogs.h"
#include <QCheckBox>
#include <QComboBox>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QImageReader>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

ImageOperationDialog::ImageOperationDialog(const QImage &image, ImageOperations::Operation operation,
    const QString &title, const QColor &foreground, const QColor &background, QWidget *parent, bool hasOrigin)
    : EditorDialog(parent), m_image(image), m_operation(operation), m_preview(nullptr), m_timer(new QTimer(this))
{
    setWindowTitle(title); m_settings.size = image.size(); m_settings.color = foreground; m_settings.otherColor = background;
    if (operation == ImageOperations::Operation::Resize || operation == ImageOperations::Operation::Stretch) {
        createSizeControls(hasOrigin); return;
    }
    m_preview = new ImageCanvas;
    setWindowTitle(QString(title).remove(QStringLiteral("...")));
    auto *layout = new QVBoxLayout(bodyWidget()); layout->setContentsMargins(16, 10, 16, 10); layout->setSpacing(12);
    auto *previews = new QHBoxLayout; previews->setSpacing(14);
    ImageCanvas *originalPreview = nullptr;
    for (bool after : {false, true}) {
        auto *group = new QWidget; auto *groupLayout = new QVBoxLayout(group); groupLayout->setContentsMargins(0, 0, 0, 0);
        auto *caption = new QLabel(after ? tr("New") : tr("Original")); caption->setAlignment(Qt::AlignCenter); groupLayout->addWidget(caption);
        auto *canvas = after ? m_preview : new ImageCanvas;
        if (!after) originalPreview = canvas;
        canvas->setEditable(false); canvas->setImage(image);
        canvas->setZoom(qMin(1.0, 196.0 / qMax(image.width(), image.height())));
        auto *scroll = new QScrollArea; scroll->setWidget(canvas); scroll->setFixedSize(204, 204); scroll->setAlignment(Qt::AlignCenter);
        scroll->setStyleSheet(QStringLiteral("QScrollArea { background: #c0c0c0; } QScrollArea > QWidget > QWidget { background: #c0c0c0; }"));
        groupLayout->addWidget(scroll); previews->addWidget(group, 0, Qt::AlignTop);
    }
    layout->addLayout(previews);
    auto *form = new QFormLayout;
    auto *parameters = new QGroupBox; parameters->setLayout(form); layout->addWidget(parameters);
    form->setContentsMargins(12, 16, 12, 16); form->setVerticalSpacing(12); form->setHorizontalSpacing(10);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_timer->setSingleShot(true); m_timer->setInterval(150);
    connect(m_timer, &QTimer::timeout, this, &ImageOperationDialog::updatePreview);
    const auto changed = [this] { if (!m_timer->isActive()) m_timer->start(); };
    const auto sliderRow = [form](const QString &label, QAbstractSpinBox *spin, int minimum, int maximum, int value) {
        auto *row = new QWidget; auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0); rowLayout->setSpacing(12);
        auto *slider = new QSlider(Qt::Horizontal); slider->setRange(minimum, maximum); slider->setValue(value);
        slider->setTickPosition(QSlider::TicksBelow); slider->setTickInterval(qMax(1, (maximum - minimum) / 16));
        slider->setPageStep(qMax(1, (maximum - minimum) / 10)); slider->setMinimumWidth(180); slider->setMinimumHeight(28);
        spin->setFixedWidth(52); spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        rowLayout->addWidget(slider, 1); rowLayout->addWidget(spin); form->addRow(label, row);
        return slider;
    };
    const auto numberWithPrecision = [&](const QString &label, double &field, double value, double minimum, double maximum, int decimals) {
        const int precision = decimals == 0 ? 1 : 10;
        field = value; auto *spin = new QDoubleSpinBox; spin->setRange(minimum, maximum); spin->setDecimals(decimals); spin->setValue(value);
        auto *slider = sliderRow(label, spin, qRound(minimum * precision), qRound(maximum * precision), qRound(spin->value() * precision));
        connect(slider, &QSlider::valueChanged, spin, [spin, precision](int value) { spin->setValue(double(value) / precision); });
        double *target = &field; connect(spin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
            [target, slider, precision, changed](double value) {
                const QSignalBlocker blocker(slider); slider->setValue(qRound(value * precision)); *target = value; changed();
            }); return spin;
    };
    const auto number = [&](const QString &label, double &field, double value, double minimum, double maximum) {
        return numberWithPrecision(label, field, value, minimum, maximum, 1);
    };
    auto integer = [&](const QString &label, int &field, int value, int minimum, int maximum) {
        field = value; auto *spin = new QSpinBox; spin->setRange(minimum, maximum); spin->setValue(value);
        auto *slider = sliderRow(label, spin, minimum, maximum, spin->value());
        connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
        int *target = &field; connect(spin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
            [target, slider, changed](int value) {
                const QSignalBlocker blocker(slider); slider->setValue(value); *target = value; changed();
            });
    };
    const auto hueNumber = [&](const QString &label, double &field, double value) {
        auto *spin = numberWithPrecision(label, field, value, 0, 359, 0); spin->setFixedSize(82, 29);
        const auto updateColor = [spin](double value) {
            const QColor color = QColor::fromHsv(qRound(value), 255, 255);
            const QString textColor = qGray(color.rgb()) > 160 ? QStringLiteral("#000000") : QStringLiteral("#ffffff");
            spin->setStyleSheet(QStringLiteral("QDoubleSpinBox, QDoubleSpinBox QLineEdit { background-color: %1; color: %2; }")
                .arg(color.name(), textColor));
        };
        connect(spin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), spin, updateColor);
        updateColor(spin->value()); return spin;
    };
    const auto makeCheck = [&](const QString &label, bool &field, bool value) {
        field = value; auto *box = new QCheckBox(label); box->setChecked(value); bool *target = &field;
        connect(box, &QCheckBox::toggled, this, [target, changed](bool value) { *target = value; changed(); }); return box;
    };
    const auto check = [&](const QString &label, bool &field, bool value) {
        form->addRow(QString(), makeCheck(label, field, value));
    };
    const auto optionRow = [form](const QList<QWidget *> &widgets) {
        auto *row = new QWidget; auto *rowLayout = new QHBoxLayout(row); rowLayout->setContentsMargins(0, 0, 0, 0); rowLayout->setSpacing(12);
        for (auto *widget : widgets) rowLayout->addWidget(widget);
        rowLayout->addStretch(); form->addRow(row);
    };
    const auto byteAmount = [&](const QString &label, double &field, int value) {
        auto *spin = numberWithPrecision(label, field, value, 0, 255, 0);
        field = value * 100.0 / 255; double *target = &field;
        connect(spin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
            [target](double value) { *target = value * 100.0 / 255; });
    };
    auto color = [&](const QString &label, QColor &field) {
        auto *button = new QPushButton; button->setFixedSize(93, 22); QColor *target = &field;
        const auto updateColor = [button, target] { button->setStyleSheet(QStringLiteral("background-color: %1;").arg(target->name())); };
        updateColor();
        connect(button, &QPushButton::clicked, this, [this, target, updateColor, changed] {
            const QColor value = EditorColorDialog::getColor(*target, this, tr("Color"), QColorDialog::ShowAlphaChannel);
            if (!value.isValid()) return;
            *target = value; updateColor(); changed();
        });
        connect(originalPreview, &ImageCanvas::positionPicked, this, [this, target, updateColor, changed](const QPoint &point) {
            *target = QColor::fromRgba(m_image.pixel(point)); updateColor(); changed();
        });
        form->addRow(label, button);
        auto *hint = new QLabel(tr("(you can also click on the original image to change this)")); hint->setWordWrap(true); form->addRow(QString(), hint);
        return button;
    };
    using Operation = ImageOperations::Operation;
    switch (operation) {
    case Operation::Shift:
        integer(tr("Horizontal:"), m_settings.offsetX, 0, -8192, 8192);
        integer(tr("Vertical:"), m_settings.offsetY, 0, -8192, 8192);
        optionRow({makeCheck(tr("Wrap Horizontally"), m_settings.wrapHorizontal, false), makeCheck(tr("Wrap Vertically"), m_settings.wrapVertical, false)}); break;
    case Operation::Mirror:
        optionRow({makeCheck(tr("Mirror Horizontally"), m_settings.wrapHorizontal, true), makeCheck(tr("Flip Vertically"), m_settings.wrapVertical, false)}); break;
    case Operation::Rotate: {
        auto *angle = numberWithPrecision(tr("Angle:"), m_settings.amount, 90, 0, 359, 0);
        QList<QWidget *> options;
        for (int degrees : {90, 180, 270}) {
            auto *button = new QPushButton(tr("%1 degrees").arg(degrees)); options.append(button);
            connect(button, &QPushButton::clicked, angle, [angle, degrees] { angle->setValue(degrees); });
        }
        options.append(makeCheck(tr("Clockwise"), m_settings.clockwise, true)); optionRow(options); break;
    }
    case Operation::Scale: {
        auto *percentage = numberWithPrecision(tr("Percentage:"), m_settings.amount, 100, 1, 400, 0);
        auto *halve = new QPushButton(tr("Halve")); auto *doubleSize = new QPushButton(tr("Double"));
        auto *horizontal = new QCheckBox(tr("Horizontal")); auto *vertical = new QCheckBox(tr("Vertical"));
        horizontal->setChecked(true); vertical->setChecked(true);
        const auto scaleChanged = [this, percentage, horizontal, vertical, changed] {
            m_settings.amount = horizontal->isChecked() ? percentage->value() : 100;
            m_settings.secondary = vertical->isChecked() ? percentage->value() : 100; changed();
        };
        connect(percentage, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, scaleChanged);
        connect(horizontal, &QCheckBox::toggled, this, scaleChanged); connect(vertical, &QCheckBox::toggled, this, scaleChanged);
        connect(halve, &QPushButton::clicked, percentage, [percentage] { percentage->setValue(percentage->value() / 2); });
        connect(doubleSize, &QPushButton::clicked, percentage, [percentage] { percentage->setValue(percentage->value() * 2); });
        optionRow({halve, doubleSize, horizontal, vertical}); break;
    }
    case Operation::Skew:
        numberWithPrecision(tr("Horizontal:"), m_settings.amount, 0, -255, 255, 0);
        numberWithPrecision(tr("Vertical:"), m_settings.secondary, 0, -255, 255, 0); break;
    case Operation::Trim: integer(tr("Transparent border (pixels):"), m_settings.radius, 0, -1024, 1024); break;
    case Operation::Colorize:
    case Operation::ColorizePartial:
        hueNumber(operation == Operation::Colorize ? tr("Hue:") : tr("New Hue:"), m_settings.amount, qMax(0, foreground.hsvHue()));
        if (operation == Operation::ColorizePartial) {
            auto *hue = hueNumber(tr("Old Hue:"), m_settings.secondary, 0);
            connect(originalPreview, &ImageCanvas::positionPicked, this, [this, hue](const QPoint &point) {
                hue->setValue(qMax(0, QColor::fromRgba(m_image.pixel(point)).hsvHue()));
            });
            auto *hint = new QLabel(tr("Click the original image to choose its hue.")); hint->setWordWrap(true); form->addRow(QString(), hint);
            numberWithPrecision(tr("Tolerance:"), m_settings.tolerance, 20, 0, 180, 0);
        }
        check(tr("Shift the hue"), m_settings.relative, false); break;
    case Operation::Intensity:
        number(tr("Value (%):"), m_settings.amount, 100, 0, 400);
        number(tr("Saturation (%):"), m_settings.secondary, 100, 0, 400); break;
    case Operation::EraseColor:
        color(tr("Color:"), m_settings.color); numberWithPrecision(tr("Tolerance:"), m_settings.tolerance, 0, 0, 255, 0); break;
    case Operation::Opacity:
        numberWithPrecision(tr("Opacity (alpha):"), m_settings.amount, 255, 0, 255, 0);
        check(tr("Relative"), m_settings.relative, false); break;
    case Operation::Alpha: {
        auto *button = new QPushButton(tr("Choose alpha image...")); form->addRow(button);
        connect(button, &QPushButton::clicked, this, [this, button, changed] {
            const QString path = QFileDialog::getOpenFileName(this, tr("Alpha from File"), QString(), tr("Images (*.png *.bmp *.jpg *.gif *.tif)"));
            if (path.isEmpty()) return;
            QImageReader reader(path); const QImage image = reader.read();
            if (image.isNull()) { EditorMessageBox::critical(this, tr("Cannot Read Image"), reader.errorString()); return; }
            m_settings.alphaImage = image; button->setText(QFileInfo(path).fileName()); changed();
        }); break;
    }
    case Operation::Fade:
        color(tr("Color:"), m_settings.color); byteAmount(tr("Amount:"), m_settings.amount, 127); break;
    case Operation::Blur:
    case Operation::Sharpen: {
        const bool blur = operation == Operation::Blur;
        auto *options = new QWidget; auto *optionsLayout = new QHBoxLayout(options);
        optionsLayout->setContentsMargins(24, 0, 24, 0); optionsLayout->setSpacing(32);
        auto *presets = new QVBoxLayout; auto *channels = new QVBoxLayout;
        const QStringList names = blur ? QStringList({tr("Small"), tr("Medium"), tr("Large")}) : QStringList({tr("Subtle"), tr("Strong"), tr("Special")});
        m_settings.radius = 1; m_settings.amount = 100; m_settings.mode = blur ? 0 : 1;
        for (int index = 0; index < names.size(); ++index) {
            auto *radio = new QRadioButton(names.at(index)); radio->setChecked(index == 0); presets->addWidget(radio);
            connect(radio, &QRadioButton::toggled, this, [this, blur, index, changed](bool checked) {
                if (!checked) return;
                m_settings.radius = blur ? (1 << index) : (index == 2 ? 2 : 1);
                m_settings.amount = 100 * (1 << index); changed();
            });
        }
        auto *colors = new QCheckBox(blur ? tr("Blur Colors") : tr("Sharpen Colors")); colors->setChecked(true);
        auto *alpha = new QCheckBox(blur ? tr("Blur Transparency") : tr("Sharpen Transparency")); alpha->setChecked(blur);
        channels->addWidget(colors); channels->addWidget(alpha); channels->addStretch();
        const auto channelsChanged = [this, colors, alpha, changed] {
            m_settings.mode = colors->isChecked() ? (alpha->isChecked() ? 0 : 1) : (alpha->isChecked() ? 2 : 3); changed();
        };
        connect(colors, &QCheckBox::toggled, this, channelsChanged); connect(alpha, &QCheckBox::toggled, this, channelsChanged);
        optionsLayout->addLayout(presets); optionsLayout->addLayout(channels); optionsLayout->addStretch(); form->addRow(options); break;
    }
    case Operation::Outline:
        color(tr("Color:"), m_settings.color); integer(tr("Thickness:"), m_settings.radius, 1, 1, 12);
        optionRow({makeCheck(tr("Place inside image"), m_settings.inside, false), makeCheck(tr("Remove the image"), m_settings.onlyEffect, false),
            makeCheck(tr("Smooth"), m_settings.smooth, false)}); break;
    case Operation::Shadow:
        color(tr("Color:"), m_settings.color); byteAmount(tr("Opacity (alpha):"), m_settings.amount, 128);
        integer(tr("Horizontal:"), m_settings.offsetX, 4, -255, 255); integer(tr("Vertical:"), m_settings.offsetY, 4, -255, 255);
        m_settings.radius = 4; check(tr("Soft shadow"), m_settings.smooth, true); break;
    case Operation::Glow:
        color(tr("Color:"), m_settings.color); byteAmount(tr("Opacity (alpha):"), m_settings.amount, 128);
        integer(tr("Thickness:"), m_settings.radius, 4, 1, 32); check(tr("Place inside image"), m_settings.inside, false); break;
    case Operation::Button:
        color(tr("Color:"), m_settings.color); byteAmount(tr("Opacity (alpha):"), m_settings.amount, 128);
        integer(tr("Thickness:"), m_settings.radius, 8, 1, 64); check(tr("Smooth Edges"), m_settings.smooth, false); break;
    case Operation::Gradient: {
        auto *colors = new QWidget; auto *colorsLayout = new QHBoxLayout(colors); colorsLayout->setContentsMargins(0, 0, 0, 0);
        for (int index = 0; index < 2; ++index) {
            if (index) colorsLayout->addStretch();
            colorsLayout->addWidget(new QLabel(index == 0 ? tr("Color1:") : tr("Color2:")));
            auto *button = new QPushButton; button->setFixedSize(92, 22);
            QColor *target = index == 0 ? &m_settings.color : &m_settings.otherColor;
            const auto updateColor = [button, target] { button->setStyleSheet(QStringLiteral("background-color: %1;").arg(target->name())); };
            updateColor(); colorsLayout->addWidget(button);
            connect(button, &QPushButton::clicked, this, [this, target, updateColor, changed] {
                const QColor value = EditorColorDialog::getColor(*target, this, tr("Gradient Color"));
                if (value.isValid()) { *target = value; updateColor(); changed(); }
            });
        }
        form->addRow(colors);
        numberWithPrecision(tr("Opacity (alpha):"), m_settings.amount, 255, 0, 255, 0);
        auto *kinds = new QWidget; auto *kindsLayout = new QGridLayout(kinds);
        kindsLayout->setContentsMargins(0, 0, 0, 0); kindsLayout->setSpacing(8); kindsLayout->setAlignment(Qt::AlignLeft);
        auto *kindGroup = new QButtonGroup(kinds); kindGroup->setExclusive(true);
        const QStringList names = {tr("Horizontal"), tr("Horizontal mirrored"), tr("Vertical"), tr("Vertical mirrored"),
            tr("Diagonal down"), tr("Diagonal up"), tr("Circular"), tr("Square"),
            tr("Top left"), tr("Top right"), tr("Bottom left"), tr("Bottom right")};
        QImage sample(28, 28, QImage::Format_ARGB32); sample.fill(Qt::black);
        ImageOperationSettings thumbnail; thumbnail.color = Qt::black; thumbnail.otherColor = Qt::white; thumbnail.amount = 255;
        for (int index = 0; index < names.size(); ++index) {
            thumbnail.mode = index;
            auto *button = new QToolButton; button->setFixedSize(34, 34); button->setIconSize(QSize(28, 28)); button->setCheckable(true);
            button->setIcon(QIcon(QPixmap::fromImage(ImageOperations::apply(sample, Operation::Gradient, thumbnail))));
            button->setToolTip(names.at(index)); button->setAccessibleName(names.at(index)); button->setChecked(index == m_settings.mode);
            kindGroup->addButton(button, index); kindsLayout->addWidget(button, index / 6, index % 6);
            connect(button, &QToolButton::clicked, this, [this, index, changed] { m_settings.mode = index; changed(); });
        }
        form->addRow(tr("Kind:"), kinds);
        auto *options = new QWidget; auto *optionsLayout = new QHBoxLayout(options); optionsLayout->setContentsMargins(0, 0, 0, 0);
        auto *replace = new QCheckBox(tr("Replace")); replace->setChecked(m_settings.replace);
        auto *transparency = new QCheckBox(tr("Change Transparency")); transparency->setChecked(m_settings.changeAlpha);
        optionsLayout->addWidget(replace); optionsLayout->addStretch(); optionsLayout->addWidget(transparency);
        form->addRow(QString(), options);
        connect(replace, &QCheckBox::toggled, this, [this, changed](bool checked) { m_settings.replace = checked; changed(); });
        connect(transparency, &QCheckBox::toggled, this, [this, changed](bool checked) { m_settings.changeAlpha = checked; changed(); });
        break;
    }
    default: break;
    }
    if (hasOrigin && operation == Operation::Trim)
        check(tr("Adjust origin with image"), m_settings.adjustOrigin, true);
    parameters->setVisible(form->rowCount() > 0);
    auto *buttons = new QHBoxLayout; buttons->setSpacing(30);
    auto *ok = new QPushButton(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("OK")); ok->setDefault(true); ok->setFixedSize(96, 25);
    auto *cancel = new QPushButton(QIcon(QStringLiteral(":/images/close.png")), tr("Cancel")); cancel->setFixedSize(96, 25);
    buttons->addWidget(ok); buttons->addWidget(cancel); buttons->addStretch(); layout->addLayout(buttons);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept); connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    updatePreview();
}

void ImageOperationDialog::createSizeControls(bool hasOrigin)
{
    const bool resizeCanvas = m_operation == ImageOperations::Operation::Resize;
    setWindowTitle(resizeCanvas ? tr("Resize") : tr("Stretch Images"));
    auto *layout = new QVBoxLayout(bodyWidget()); layout->setSpacing(12);
    auto *sizeGroup = new QGroupBox(tr("New Size")); auto *sizeLayout = new QGridLayout(sizeGroup);
    sizeLayout->setHorizontalSpacing(8); sizeLayout->setVerticalSpacing(12);
    auto *width = new QSpinBox; auto *height = new QSpinBox;
    auto *widthPercent = new QDoubleSpinBox; auto *heightPercent = new QDoubleSpinBox;
    for (auto *spin : {width, height}) {
        spin->setRange(1, 8192); spin->setFixedWidth(64); spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setKeyboardTracking(false);
    }
    for (auto *spin : {widthPercent, heightPercent}) {
        spin->setDecimals(2); spin->setRange(0.01, 819200); spin->setValue(100);
        spin->setFixedWidth(64); spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setKeyboardTracking(false);
    }
    width->setValue(m_image.width()); height->setValue(m_image.height());
    auto *widthLabel = new QLabel(tr("&Width:")); widthLabel->setBuddy(widthPercent);
    auto *heightLabel = new QLabel(tr("&Height:")); heightLabel->setBuddy(heightPercent);
    sizeLayout->addWidget(widthLabel, 0, 0); sizeLayout->addWidget(widthPercent, 0, 1);
    sizeLayout->addWidget(new QLabel(QStringLiteral("%")), 0, 2); sizeLayout->addWidget(width, 0, 3); sizeLayout->addWidget(new QLabel(tr("pixels")), 0, 4);
    sizeLayout->addWidget(heightLabel, 1, 0); sizeLayout->addWidget(heightPercent, 1, 1);
    sizeLayout->addWidget(new QLabel(QStringLiteral("%")), 1, 2); sizeLayout->addWidget(height, 1, 3); sizeLayout->addWidget(new QLabel(tr("pixels")), 1, 4);
    auto *aspect = new QCheckBox(tr("Keep &aspect ratio")); aspect->setChecked(true); sizeLayout->addWidget(aspect, 2, 0, 1, 5);
    if (hasOrigin) {
        auto *origin = new QCheckBox(tr("Maintain &origin")); origin->setChecked(m_settings.adjustOrigin);
        sizeLayout->addWidget(origin, 3, 0, 1, 5);
        connect(origin, &QCheckBox::toggled, this, [this](bool checked) { m_settings.adjustOrigin = checked; });
    }
    layout->addWidget(sizeGroup);
    const auto updatePercent = [this, width, height, widthPercent, heightPercent] {
        const QSignalBlocker widthBlocker(widthPercent), heightBlocker(heightPercent);
        widthPercent->setValue(width->value() * 100.0 / m_image.width());
        heightPercent->setValue(height->value() * 100.0 / m_image.height());
        m_settings.size = QSize(width->value(), height->value());
    };
    connect(width, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this, height, aspect, updatePercent](int value) {
        if (aspect->isChecked()) { const QSignalBlocker blocker(height); height->setValue(qMax(1, qRound(double(value) * m_image.height() / m_image.width()))); }
        updatePercent();
    });
    connect(height, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this, width, aspect, updatePercent](int value) {
        if (aspect->isChecked()) { const QSignalBlocker blocker(width); width->setValue(qMax(1, qRound(double(value) * m_image.width() / m_image.height()))); }
        updatePercent();
    });
    connect(widthPercent, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
        [this, width, updatePercent](double value) { width->setValue(qRound(m_image.width() * value / 100)); updatePercent(); });
    connect(heightPercent, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
        [this, height, updatePercent](double value) { height->setValue(qRound(m_image.height() * value / 100)); updatePercent(); });
    auto *options = new QHBoxLayout;
    auto *group = new QGroupBox(resizeCanvas ? tr("Position") : tr("Quality")); group->setMinimumWidth(132);
    if (resizeCanvas) {
        auto *positions = new QGridLayout(group); positions->setSpacing(2); positions->setContentsMargins(20, 20, 20, 16);
        auto *buttons = new QButtonGroup(group); buttons->setExclusive(true);
        const int directions[] = {7, 8, 9, 4, 5, 6, 1, 2, 3};
        const QStringList names = {tr("Top left"), tr("Top"), tr("Top right"), tr("Left"), tr("Center"), tr("Right"), tr("Bottom left"), tr("Bottom"), tr("Bottom right")};
        for (int index = 0; index < 9; ++index) {
            auto *button = new QToolButton; button->setFixedSize(26, 26); button->setCheckable(true); button->setChecked(index == m_settings.anchor);
            button->setIcon(QIcon(QStringLiteral(":/images/editor/anchor%1.png").arg(directions[index])));
            button->setIconSize(QSize(16, 16)); button->setToolTip(names.at(index)); button->setAccessibleName(names.at(index));
            buttons->addButton(button, index); positions->addWidget(button, index / 3, index % 3);
            connect(button, &QToolButton::clicked, this, [this, index] { m_settings.anchor = index; });
        }
        options->addStretch(); options->addWidget(group);
    } else {
        auto *qualityLayout = new QVBoxLayout(group); qualityLayout->setSpacing(4);
        const QStringList names = {tr("Poor"), tr("Normal"), tr("Good"), tr("Very Good"), tr("Excellent")};
        const QStringList filters = {tr("Nearest neighbor"), tr("Bilinear"), tr("Cubic B-spline"), tr("Catmull-Rom"), tr("Mitchell-Netravali")};
        for (int index = 0; index < names.size(); ++index) {
            auto *button = new QRadioButton(names.at(index)); button->setToolTip(filters.at(index));
            button->setChecked(index == static_cast<int>(m_settings.quality)); qualityLayout->addWidget(button);
            connect(button, &QRadioButton::toggled, this, [this, index](bool checked) {
                if (checked) m_settings.quality = static_cast<ImageResizer::Quality>(index);
            });
        }
        options->addWidget(group); options->addStretch();
    }
    layout->addLayout(options); layout->addStretch();
    auto *buttons = new QHBoxLayout;
    auto *ok = new QPushButton(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("OK")); ok->setDefault(true); ok->setMinimumWidth(92);
    auto *cancel = new QPushButton(QIcon(QStringLiteral(":/images/close.png")), tr("Cancel")); cancel->setMinimumWidth(92);
    buttons->addWidget(ok); buttons->addStretch(); buttons->addWidget(cancel); layout->addLayout(buttons);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept); connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    widthPercent->setFocus(); widthPercent->selectAll();
}

void ImageOperationDialog::updatePreview()
{
    const QImage image = ImageOperations::apply(m_image, m_operation, m_settings);
    if (!image.isNull()) { m_preview->setImage(image); m_preview->setZoom(qMin(1.0, 196.0 / qMax(image.width(), image.height()))); }
}
