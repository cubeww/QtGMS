#include "imageeditorwindow.h"
#include "imageoperationdialog.h"
#include "imagepalette.h"
#include "editorstandarddialogs.h"
#include "editordialog.h"
#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDialog>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QImageReader>
#include <QLabel>
#include <QMenuBar>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

class ImageOriginCommand : public QUndoCommand
{
public:
    ImageOriginCommand(QPoint *origin, const QPoint &value) : m_origin(origin), m_before(*origin), m_after(value) {}
    void undo() override { *m_origin = m_before; }
    void redo() override { *m_origin = m_after; }
private:
    QPoint *m_origin;
    QPoint m_before, m_after;
};

ImageEditorWindow::ImageEditorWindow(const QImage &image, QWidget *parent)
    : EditorWindow(parent), m_preview(new ImageCanvas), m_history(new QUndoStack(this)), m_scroll(new QScrollArea),
      m_palette(new ImagePalette), m_statusLabel(new QLabel), m_toolLabel(new QLabel),
      m_foregroundButton(new QPushButton), m_backgroundButton(new QPushButton),
      m_sizeSpin(new QSpinBox), m_opacitySpin(new QSpinBox), m_hardnessSpin(new QSpinBox), m_toleranceSpin(new QSpinBox),
      m_onionOpacity(new QSpinBox), m_onionForward(new QSpinBox), m_onionBackward(new QSpinBox),
      m_shapeStyle(new QComboBox), m_arrowMode(new QComboBox), m_textAlignment(new QComboBox),
      m_antialias(new QCheckBox(tr("Anti-alias"))), m_colorOnly(new QCheckBox(tr("Color only"))),
      m_rightErase(new QCheckBox(tr("Right button erases")))
{
    setObjectName(QStringLiteral("imageEditor")); setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Image Editor")); resize(1000, 760); setMinimumSize(650, 440); m_history->setUndoLimit(100);
    auto *fileMenu = editorMenuBar()->addMenu(tr("&File")); auto *editMenu = editorMenuBar()->addMenu(tr("&Edit"));
    auto *viewMenu = editorMenuBar()->addMenu(tr("&View")); auto *transformMenu = editorMenuBar()->addMenu(tr("&Transform"));
    auto *imageMenu = editorMenuBar()->addMenu(tr("&Image"));
    auto action = [this](QMenu *menu, const QString &text, const QString &icon, const QKeySequence &key, const std::function<void()> &function) {
        auto *item = menu->addAction(text); item->setShortcut(key);
        if (!icon.isEmpty()) item->setIcon(QIcon(QStringLiteral(":/images/editor/%1.png").arg(icon)));
        item->setIconVisibleInMenu(true);
        connect(item, &QAction::triggered, this, function); return item;
    };
    auto *newAction = action(fileMenu, tr("&New"), QStringLiteral("add"), QKeySequence::New, [this] { newImage(); });
    auto *openAction = action(fileMenu, tr("&Open..."), QStringLiteral("insert"), QKeySequence::Open, [this] { openImage(false); });
    fileMenu->addSeparator();
    auto *saveAction = action(fileMenu, tr("&Save as PNG File..."), QString(), QKeySequence::Save, [this] { exportImage(); });
    saveAction->setIcon(QIcon(QStringLiteral(":/images/save.png"))); fileMenu->addSeparator();
    action(fileMenu, tr("Save Color Palette..."), QStringLiteral("colorize"), QKeySequence(), [this] { paletteFile(true); });
    action(fileMenu, tr("Load Color Palette..."), QStringLiteral("colorize"), QKeySequence(), [this] { paletteFile(false); }); fileMenu->addSeparator();
    m_previousAction = action(fileMenu, tr("&Previous Image"), QStringLiteral("left"), QKeySequence(Qt::SHIFT | Qt::Key_Left), [this] { changeFrame(-1); });
    m_nextAction = action(fileMenu, tr("N&ext Image"), QStringLiteral("right"), QKeySequence(Qt::SHIFT | Qt::Key_Right), [this] { changeFrame(1); }); fileMenu->addSeparator();
    auto *acceptAction = action(fileMenu, tr("&Close Saving Changes"), QStringLiteral("ok"), QKeySequence(Qt::CTRL | Qt::Key_Return), [this] { acceptImage(); });
    auto *undoAction = action(editMenu, tr("&Undo"), QStringLiteral("undo"), QKeySequence::Undo, [this] { m_canvas->undo(); });
    auto *redoAction = action(editMenu, tr("&Redo"), QStringLiteral("redo"), QKeySequence::Redo, [this] { m_canvas->redo(); });
    redoAction->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    undoAction->setEnabled(false); redoAction->setEnabled(false);
    connect(m_history, &QUndoStack::canUndoChanged, undoAction, &QAction::setEnabled);
    connect(m_history, &QUndoStack::canRedoChanged, redoAction, &QAction::setEnabled); editMenu->addSeparator();
    action(editMenu, tr("&Erase to Left Color"), QStringLiteral("erase"), QKeySequence(Qt::SHIFT | Qt::Key_Delete), [this] { m_canvas->fillSelection(); }); editMenu->addSeparator();
    action(editMenu, tr("&Delete"), QStringLiteral("delete"), QKeySequence(Qt::Key_Delete), [this] { m_canvas->eraseSelection(); });
    auto *cutAction = action(editMenu, tr("Cu&t"), QStringLiteral("cut"), QKeySequence::Cut, [this] { m_canvas->cut(); });
    auto *copyAction = action(editMenu, tr("&Copy"), QStringLiteral("copy"), QKeySequence::Copy, [this] { m_canvas->copy(); });
    auto *pasteAction = action(editMenu, tr("&Paste"), QStringLiteral("paste"), QKeySequence::Paste, [this] { m_canvas->paste(); });
    auto *pasteFileAction = action(editMenu, tr("Paste from &File..."), QStringLiteral("insert"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_V), [this] { openImage(true); }); editMenu->addSeparator();
    action(editMenu, tr("Select &All"), QStringLiteral("selectall"), QKeySequence::SelectAll, [this] { m_canvas->selectAll(); });
    action(editMenu, tr("Clear Selection"), QStringLiteral("selection"), QKeySequence(Qt::CTRL | Qt::Key_D), [this] { m_canvas->clearSelection(); });
    auto *zoomOut = action(viewMenu, tr("Zoom &Out"), QStringLiteral("zoomout"), QKeySequence(Qt::Key_Minus), [this] { m_canvas->setZoom(m_canvas->zoom() / 2); });
    auto *zoomReset = action(viewMenu, tr("&No Zoom"), QStringLiteral("actualsize"), QKeySequence(Qt::CTRL | Qt::Key_0), [this] { m_canvas->setZoom(1); });
    auto *zoomIn = action(viewMenu, tr("Zoom &In"), QStringLiteral("zoomin"), QKeySequence(Qt::Key_Plus), [this] { m_canvas->setZoom(m_canvas->zoom() * 2); }); viewMenu->addSeparator();
    m_gridAction = action(viewMenu, tr("Toggle &Grid"), QStringLiteral("grid"), QKeySequence(Qt::CTRL | Qt::Key_G), [this] { updateTools(); }); m_gridAction->setCheckable(true);
    action(viewMenu, tr("Grid Settings..."), QStringLiteral("gridsettings"), QKeySequence(), [this] { configureGrid(); }); viewMenu->addSeparator();
    auto *previewAction = viewMenu->addAction(tr("Show &Preview")); previewAction->setCheckable(true); previewAction->setChecked(true);
    previewAction->setIcon(QIcon(QStringLiteral(":/images/editor/preview.png"))); previewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    previewAction->setIconVisibleInMenu(true);
    action(viewMenu, tr("Set Transparency Background..."), QStringLiteral("transparencybackground"), QKeySequence(Qt::CTRL | Qt::Key_T), [this] { configureBackground(); }); viewMenu->addSeparator();
    m_scratchAction = action(viewMenu, tr("Toggle &Scratch Page"), QStringLiteral("edit"), QKeySequence(Qt::Key_J), [this] { toggleScratch(); }); m_scratchAction->setCheckable(true);
    using Operation = ImageOperations::Operation;
    struct OperationEntry { Operation operation; const char *label; const char *icon; };
    const OperationEntry transforms[] = {{Operation::Shift, QT_TR_NOOP("&Shift..."), "shift"}, {Operation::Mirror, QT_TR_NOOP("&Mirror/Flip..."), "mirror"},
        {Operation::Rotate, QT_TR_NOOP("&Rotate..."), "rotate"}, {Operation::Scale, QT_TR_NOOP("Sc&ale..."), "scale"}, {Operation::Skew, QT_TR_NOOP("S&kew..."), "skew"},
        {Operation::Resize, QT_TR_NOOP("Resize &Canvas..."), "resize"}, {Operation::Stretch, QT_TR_NOOP("Str&etch..."), "stretch"}, {Operation::Trim, QT_TR_NOOP("&Trim..."), "trim"}};
    const OperationEntry effects[] = {{Operation::Grayscale, QT_TR_NOOP("Black and &White..."), "grayscale"}, {Operation::Colorize, QT_TR_NOOP("&Colorize..."), "colorize"},
        {Operation::ColorizePartial, QT_TR_NOOP("Colorize Partial..."), "colorize"}, {Operation::Intensity, QT_TR_NOOP("&Intensity..."), "intensity"}, {Operation::Invert, QT_TR_NOOP("In&vert..."), "invert"},
        {Operation::Opaque, QT_TR_NOOP("Make Opaque..."), "opaque"}, {Operation::EraseColor, QT_TR_NOOP("Erase a Color..."), "erasecolor"}, {Operation::SmoothEdges, QT_TR_NOOP("Smooth Edges..."), "smoothedges"},
        {Operation::Opacity, QT_TR_NOOP("Opacity..."), "opacity"}, {Operation::Alpha, QT_TR_NOOP("Set Alpha from File..."), "alpha"}, {Operation::Fade, QT_TR_NOOP("Fade..."), "fade"},
        {Operation::Blur, QT_TR_NOOP("Blur..."), "blur"}, {Operation::Sharpen, QT_TR_NOOP("Sharpen..."), "sharpen"}, {Operation::Outline, QT_TR_NOOP("Outline..."), "outline"},
        {Operation::Shadow, QT_TR_NOOP("Shadow..."), "shadow"}, {Operation::Glow, QT_TR_NOOP("Glow..."), "glow"}, {Operation::Button, QT_TR_NOOP("Buttonize..."), "buttonize"}, {Operation::Gradient, QT_TR_NOOP("Gradient Fill..."), "gradient"}};
    auto operationAction = [this, &action](QMenu *menu, const OperationEntry &entry) {
        const QString title = tr(entry.label); const Operation operation = entry.operation;
        action(menu, title, QLatin1String(entry.icon), QKeySequence(), [this, operation, title] { applyOperation(operation, QString(title).remove(QLatin1Char('&'))); });
    };
    for (const auto &entry : transforms) { if (entry.operation == Operation::Resize) transformMenu->addSeparator(); operationAction(transformMenu, entry); }
    for (const auto &entry : effects) { if (entry.operation == Operation::Opaque || entry.operation == Operation::Outline) imageMenu->addSeparator(); operationAction(imageMenu, entry); }
    auto *toolbar = new QToolBar(this); toolbar->setMovable(false); toolbar->setIconSize(QSize(16, 16));
    toolbar->addActions({acceptAction, newAction, openAction, pasteFileAction, saveAction}); toolbar->addSeparator();
    toolbar->addActions({undoAction, redoAction, cutAction, copyAction, pasteAction}); toolbar->addSeparator();
    toolbar->addActions({zoomOut, zoomReset, zoomIn, m_gridAction, previewAction}); toolbar->addSeparator();
    toolbar->addActions({m_previousAction, m_nextAction}); addToolBar(toolbar);

    auto *body = new QWidget(this); auto *layout = new QHBoxLayout(body); layout->setContentsMargins(4, 4, 4, 4); layout->setSpacing(6);
    auto *left = new QWidget; left->setFixedWidth(126); auto *leftLayout = new QVBoxLayout(left); leftLayout->setContentsMargins(0, 0, 0, 0);
    auto *tools = new QGroupBox(tr("Tools")); auto *toolLayout = new QGridLayout(tools); toolLayout->setContentsMargins(5, 14, 5, 5); toolLayout->setSpacing(3);
    auto *toolGroup = new QActionGroup(this);
    struct ToolEntry { ImageCanvas::Tool tool; const char *name; const char *icon; Qt::Key key; };
    using Tool = ImageCanvas::Tool;
    const ToolEntry entries[] = {
        {Tool::Brush, QT_TR_NOOP("Pencil"), "brush", Qt::Key_D}, {Tool::Airbrush, QT_TR_NOOP("Airbrush"), "airbrush", Qt::Key_A}, {Tool::Eraser, QT_TR_NOOP("Eraser"), "eraser", Qt::Key_E},
        {Tool::Picker, QT_TR_NOOP("Color Picker"), "picker", Qt::Key_C}, {Tool::Line, QT_TR_NOOP("Line"), "line", Qt::Key_L}, {Tool::Polygon, QT_TR_NOOP("Polygon"), "polygon", Qt::Key_P},
        {Tool::Rectangle, QT_TR_NOOP("Rectangle"), "rectangle", Qt::Key_R}, {Tool::Ellipse, QT_TR_NOOP("Ellipse"), "ellipse", Qt::Key_I}, {Tool::RoundedRectangle, QT_TR_NOOP("Rounded Rectangle"), "roundedrectangle", Qt::Key_O},
        {Tool::Selection, QT_TR_NOOP("Rectangle Selection"), "selection", Qt::Key_S}, {Tool::Fill, QT_TR_NOOP("Flood Fill"), "fill", Qt::Key_F}, {Tool::ReplaceColor, QT_TR_NOOP("Change Color"), "replacecolor", Qt::Key_H},
        {Tool::Text, QT_TR_NOOP("Text"), "text", Qt::Key_T}, {Tool::BrushSelection, QT_TR_NOOP("Brush Selection"), "brushselection", Qt::Key_Y}, {Tool::Wand, QT_TR_NOOP("Magic Wand"), "wand", Qt::Key_W}};
    for (int i = 0; i < 15; ++i) {
        const auto entry = entries[i]; auto *item = new QAction(tr(entry.name), toolGroup); item->setData(static_cast<int>(entry.tool));
        item->setIcon(QIcon(QStringLiteral(":/images/editor/%1.png").arg(QLatin1String(entry.icon)))); item->setCheckable(true); item->setShortcut(QKeySequence(entry.key));
        item->setToolTip(tr("%1 (%2)").arg(tr(entry.name), item->shortcut().toString())); addAction(item); m_toolActions.append(item);
        auto *button = new QToolButton; button->setDefaultAction(item); button->setIconSize(QSize(16, 16)); button->setFixedSize(34, 34); toolLayout->addWidget(button, i / 3, i % 3);
        connect(item, &QAction::triggered, this, [this, entry] { m_tool = entry.tool; m_canvas->setTool(m_tool); updateTools(); });
        if (entry.tool == Tool::Eraser) { button->setContextMenuPolicy(Qt::CustomContextMenu); connect(button, &QWidget::customContextMenuRequested, this, [this] { m_rightErase->setChecked(!m_rightErase->isChecked()); }); }
    }
    leftLayout->addWidget(tools);
    auto groupForm = [leftLayout](const QString &title, QGroupBox *&group) {
        group = new QGroupBox(title); auto *form = new QVBoxLayout(group); form->setContentsMargins(6, 14, 6, 6); form->setSpacing(3); leftLayout->addWidget(group); return form;
    };
    auto *sizes = groupForm(tr("Size"), m_sizeGroup); auto *presets = new QGridLayout;
    const int values[] = {1, 3, 5, 9, 13, 19};
    for (int i = 0; i < 6; ++i) {
        const int value = values[i]; auto *button = new QToolButton; button->setFixedSize(33, 33);
        QPixmap icon(22, 22); icon.fill(Qt::transparent); QPainter painter(&icon); painter.setPen(Qt::NoPen); painter.setBrush(Qt::black); painter.drawEllipse(QRect((22 - value) / 2, (22 - value) / 2, value, value)); painter.end();
        button->setIcon(QIcon(icon)); button->setToolTip(tr("%1 pixels").arg(value)); presets->addWidget(button, i / 3, i % 3); connect(button, &QToolButton::clicked, this, [this, value] { m_sizeSpin->setValue(value); });
    }
    sizes->addLayout(presets); m_sizeSpin->setRange(1, 256); m_sizeSpin->setValue(1); sizes->addWidget(m_sizeSpin);
    auto *hardness = groupForm(tr("Hardness"), m_hardnessGroup); m_hardnessSpin->setRange(0, 100); m_hardnessSpin->setSuffix(tr("%")); m_hardnessSpin->setValue(100); hardness->addWidget(m_hardnessSpin);
    auto *tolerance = groupForm(tr("Tolerance"), m_toleranceGroup); m_toleranceSpin->setRange(0, 255); tolerance->addWidget(m_toleranceSpin); tolerance->addWidget(m_colorOnly);
    auto *shape = groupForm(tr("Shape"), m_shapeGroup); m_shapeStyle->addItems({tr("Outline"), tr("Outline and fill"), tr("Filled")}); shape->addWidget(m_shapeStyle);
    auto *line = groupForm(tr("Line"), m_lineGroup); m_arrowMode->addItems({tr("Straight line"), tr("End arrow"), tr("Start arrow"), tr("Double arrow")}); line->addWidget(m_arrowMode); leftLayout->addWidget(m_antialias);
    auto *font = groupForm(tr("Font"), m_fontGroup); auto *fontButton = new QPushButton(tr("Choose Font...")); font->addWidget(fontButton); m_textAlignment->addItems({tr("Left"), tr("Center"), tr("Right")}); font->addWidget(m_textAlignment);
    connect(fontButton, &QPushButton::clicked, this, [this] { bool ok; const QFont selected = QFontDialog::getFont(&ok, m_textFont, this); if (ok) m_textFont = selected; });
    leftLayout->addWidget(m_rightErase); leftLayout->addStretch();
    auto *leftScroll = new QScrollArea; leftScroll->setWidget(left); leftScroll->setWidgetResizable(true); leftScroll->setFixedWidth(146); leftScroll->setFrameShape(QFrame::NoFrame);
    layout->addWidget(leftScroll); m_scroll->setAlignment(Qt::AlignTop | Qt::AlignLeft); layout->addWidget(m_scroll, 1);

    auto *right = new QWidget; right->setFixedWidth(140); auto *rightLayout = new QVBoxLayout(right); rightLayout->setContentsMargins(0, 0, 0, 0);
    auto *colors = new QGroupBox(tr("Colors")); auto *colorLayout = new QGridLayout(colors); colorLayout->setContentsMargins(8, 15, 8, 6);
    colorLayout->addWidget(new QLabel(tr("Left:")), 0, 0, Qt::AlignHCenter); colorLayout->addWidget(new QLabel(tr("Right:")), 0, 1, Qt::AlignHCenter);
    m_foregroundButton->setFixedSize(48, 48); m_backgroundButton->setFixedSize(48, 48);
    colorLayout->addWidget(m_foregroundButton, 1, 0, Qt::AlignHCenter); colorLayout->addWidget(m_backgroundButton, 1, 1, Qt::AlignHCenter);
    colorLayout->addWidget(m_palette, 2, 0, 1, 2, Qt::AlignHCenter); rightLayout->addWidget(colors);
    connect(m_foregroundButton, &QPushButton::clicked, this, [this] { chooseColor(false); });
    connect(m_backgroundButton, &QPushButton::clicked, this, [this] { chooseColor(true); });
    connect(m_palette, &ImagePalette::colorSelected, this, [this](const QColor &color, bool secondary) {
        (secondary ? m_background : m_foreground) = color; if (secondary) m_rightErase->setChecked(false); updateColors();
    });
    auto *opacity = new QGroupBox(tr("Opacity")); auto *opacityLayout = new QVBoxLayout(opacity);
    m_opacitySpin->setRange(0, 255); m_opacitySpin->setValue(255); opacityLayout->addWidget(m_opacitySpin);
    auto *opacitySlider = new QSlider(Qt::Horizontal); opacitySlider->setRange(0, 255); opacitySlider->setValue(255); opacityLayout->addWidget(opacitySlider);
    connect(opacitySlider, &QSlider::valueChanged, m_opacitySpin, &QSpinBox::setValue);
    connect(m_opacitySpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), opacitySlider, &QSlider::setValue); rightLayout->addWidget(opacity);
    auto *mode = new QGroupBox(tr("Color Mode")); auto *modeLayout = new QVBoxLayout(mode);
    auto *blend = new QRadioButton(tr("Blend")); auto *replace = new QRadioButton(tr("Replace")); blend->setChecked(true); modeLayout->addWidget(blend); modeLayout->addWidget(replace); rightLayout->addWidget(mode);
    connect(blend, &QRadioButton::toggled, this, [this](bool value) { m_blend = value; updateTools(); });
    auto *onion = new QGroupBox(tr("Onion Skin")); auto *onionLayout = new QVBoxLayout(onion);
    m_onionOpacity->setRange(0, 255); m_onionOpacity->setValue(64); m_onionForward->setRange(0, 4); m_onionBackward->setRange(0, 4);
    onionLayout->addWidget(m_onionOpacity); auto *onionSlider = new QSlider(Qt::Horizontal); onionSlider->setRange(0, 255); onionSlider->setValue(64); onionLayout->addWidget(onionSlider);
    connect(onionSlider, &QSlider::valueChanged, m_onionOpacity, &QSpinBox::setValue);
    connect(m_onionOpacity, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), onionSlider, &QSlider::setValue);
    onionLayout->addWidget(new QLabel(tr("Forward"))); onionLayout->addWidget(m_onionForward); onionLayout->addWidget(new QLabel(tr("Backwards"))); onionLayout->addWidget(m_onionBackward); rightLayout->addWidget(onion);
    auto *preview = new QGroupBox(tr("Preview")); auto *previewLayout = new QVBoxLayout(preview); auto *previewScroll = new QScrollArea;
    m_preview->setEditable(false); previewScroll->setWidget(m_preview); previewScroll->setMinimumHeight(90); previewLayout->addWidget(previewScroll); rightLayout->addWidget(preview);
    connect(previewAction, &QAction::toggled, preview, &QWidget::setVisible);
    connect(m_preview, &ImageCanvas::doubleClicked, this, [this] { m_previewZoom = m_previewZoom % 3 + 1; m_preview->setZoom(m_previewZoom); });
    rightLayout->addStretch(); auto *rightScroll = new QScrollArea; rightScroll->setWidget(right); rightScroll->setWidgetResizable(true); rightScroll->setFixedWidth(160); rightScroll->setFrameShape(QFrame::NoFrame); layout->addWidget(rightScroll);
    setCentralWidget(body); statusBar()->addWidget(m_toolLabel, 1); statusBar()->addPermanentWidget(m_statusLabel);
    for (auto *spin : {m_sizeSpin, m_opacitySpin, m_hardnessSpin, m_toleranceSpin})
        connect(spin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this] { updateTools(); });
    for (auto *spin : {m_onionOpacity, m_onionForward, m_onionBackward})
        connect(spin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this] { updateOnion(); });
    for (auto *combo : {m_shapeStyle, m_arrowMode})
        connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this] { updateTools(); });
    for (auto *check : {m_antialias, m_colorOnly, m_rightErase}) connect(check, &QCheckBox::toggled, this, [this] { updateTools(); updateColors(); });
    m_frames.append(createCanvas(image)); m_originalImages.append(image.convertToFormat(QImage::Format_ARGB32));
    activateCanvas(m_frames.first()); updateColors();
    connect(m_history, &QUndoStack::indexChanged, this, [this] { refresh(); updateOnion(); });
}

ImageCanvas *ImageEditorWindow::createCanvas(const QImage &image)
{
    auto *canvas = new ImageCanvas(this); canvas->setImage(image); canvas->setUndoStack(m_history); canvas->hide();
    connect(canvas, &ImageCanvas::imageChanged, this, [this, canvas] { if (canvas == m_canvas) refresh(); });
    connect(canvas, &ImageCanvas::zoomChanged, this, [this, canvas] { if (canvas == m_canvas) refresh(); });
    connect(canvas, &ImageCanvas::positionChanged, this, [this](const QPoint &position) { m_mousePosition = position; refresh(); });
    connect(canvas, &ImageCanvas::zoomRequested, this, [canvas](int steps) { canvas->setZoom(canvas->zoom() * (steps > 0 ? 2 : 0.5)); });
    connect(canvas, &ImageCanvas::panRequested, this, [this](const QPoint &delta) {
        m_scroll->horizontalScrollBar()->setValue(m_scroll->horizontalScrollBar()->value() - delta.x());
        m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->value() - delta.y());
    });
    connect(canvas, &ImageCanvas::colorPicked, this, [this](QColor color, bool secondary) {
        m_opacitySpin->setValue(color.alpha()); color.setAlpha(255); (secondary ? m_background : m_foreground) = color;
        if (secondary) m_rightErase->setChecked(false); updateColors();
    });
    connect(canvas, &ImageCanvas::toolChanged, this, [this, canvas] { if (canvas == m_canvas) { m_tool = canvas->tool(); updateTools(); } });
    connect(canvas, &ImageCanvas::textRequested, this, &ImageEditorWindow::drawText);
    return canvas;
}
void ImageEditorWindow::setFrames(const QList<QImage> &frames, int index, const QString &name)
{
    if (frames.isEmpty() || index < 0 || index >= frames.size()) return;
    m_scroll->takeWidget(); m_history->clear(); qDeleteAll(m_frames); m_frames.clear(); m_originalImages.clear(); m_canvas = nullptr;
    for (const QImage &frame : frames) { m_frames.append(createCanvas(frame)); m_originalImages.append(frame.convertToFormat(QImage::Format_ARGB32)); }
    m_frameIndex = index; m_resourceName = name; activateCanvas(m_frames.at(index));
}
void ImageEditorWindow::activateCanvas(ImageCanvas *canvas)
{
    if (m_canvas) m_canvas->finishGesture();
    if (auto *previous = m_scroll->takeWidget()) { previous->setParent(this); previous->hide(); }
    m_canvas = canvas; m_scroll->setWidget(canvas); canvas->show();
    canvas->setTool(m_tool); updateTools(); updateColors(); updateOnion(); refresh(); canvas->setFocus();
}
void ImageEditorWindow::changeFrame(int offset)
{
    if (m_frames.size() < 2) return;
    m_frameIndex = (m_frameIndex + offset + m_frames.size()) % m_frames.size();
    m_scratchAction->setChecked(false); activateCanvas(m_frames.at(m_frameIndex));
}
void ImageEditorWindow::toggleScratch()
{
    if (m_canvas == m_scratch) { m_scratchAction->setChecked(false); activateCanvas(m_frames.at(m_frameIndex)); return; }
    if (!m_scratch) { QImage image(m_canvas->image().size(), QImage::Format_ARGB32); image.fill(Qt::transparent); m_scratch = createCanvas(image); }
    m_scratchAction->setChecked(true); activateCanvas(m_scratch);
}
void ImageEditorWindow::updateTools()
{
    if (!m_canvas) return;
    using Tool = ImageCanvas::Tool;
    const bool shape = m_tool == Tool::Rectangle || m_tool == Tool::RoundedRectangle || m_tool == Tool::Ellipse || m_tool == Tool::Polygon;
    const bool tolerance = m_tool == Tool::Fill || m_tool == Tool::ReplaceColor || m_tool == Tool::Wand;
    m_sizeGroup->setVisible(!tolerance && m_tool != Tool::Text && m_tool != Tool::Selection && m_tool != Tool::Picker);
    m_hardnessGroup->setVisible(m_tool == Tool::Airbrush || m_tool == Tool::Eraser || m_rightErase->isChecked());
    m_toleranceGroup->setVisible(tolerance); m_shapeGroup->setVisible(shape); m_lineGroup->setVisible(m_tool == Tool::Line);
    m_fontGroup->setVisible(m_tool == Tool::Text); m_antialias->setVisible(shape || m_tool == Tool::Line || m_tool == Tool::Text);
    for (auto *action : m_toolActions) action->setChecked(action->data().toInt() == static_cast<int>(m_tool));
    m_canvas->setBrushSize(m_sizeSpin->value()); m_canvas->setHardness(m_hardnessSpin->value()); m_canvas->setOpacity(m_opacitySpin->value());
    m_canvas->setBlend(m_blend); m_canvas->setTolerance(m_toleranceSpin->value()); m_canvas->setColorOnly(m_colorOnly->isChecked());
    m_canvas->setAntialiasing(m_antialias->isChecked()); m_canvas->setRightErase(m_rightErase->isChecked());
    m_canvas->setShapeStyle(static_cast<ImageCanvas::ShapeStyle>(m_shapeStyle->currentIndex())); m_canvas->setArrowMode(m_arrowMode->currentIndex());
    m_canvas->setGridVisible(m_gridAction->isChecked()); m_canvas->setGrid(m_gridSize, m_gridOffset, m_gridColor);
    m_canvas->setTransparencyBackground(m_checkerFirst, m_checkerSecond, m_checkerSize);
    m_preview->setTransparencyBackground(m_checkerFirst, m_checkerSecond, m_checkerSize);
    if (m_tool == Tool::Polygon) m_toolLabel->setText(tr("Click vertices; Enter / Esc finishes; Shift constrains"));
    else if (m_tool == Tool::Selection || m_tool == Tool::BrushSelection || m_tool == Tool::Wand) m_toolLabel->setText(tr("Shift adds; Ctrl subtracts; right drag copies"));
    else m_toolLabel->setText(tr("Shift constrains; Ctrl picks color; middle drag pans"));
}
void ImageEditorWindow::refresh()
{
    if (!m_canvas) return;
    if (m_preview->image() != m_canvas->image()) m_preview->setImage(m_canvas->image());
    m_preview->setZoom(m_previewZoom);
    m_statusLabel->setText(tr("(%1, %2)    Zoom: %3%    Size: %4 x %5    Memory: %6 KB")
        .arg(m_mousePosition.x()).arg(m_mousePosition.y()).arg(qRound(m_canvas->zoom() * 100))
        .arg(m_canvas->image().width()).arg(m_canvas->image().height()).arg(m_canvas->image().byteCount() / 1024));
    const QRect selection = m_canvas->selectionBounds();
    if (!selection.isEmpty()) m_statusLabel->setText(m_statusLabel->text() + tr("    Selection: %1 x %2").arg(selection.width()).arg(selection.height()));
    m_previousAction->setEnabled(m_frames.size() > 1); m_nextAction->setEnabled(m_frames.size() > 1);
    m_onionForward->setEnabled(m_frames.size() > 1 && m_canvas != m_scratch); m_onionBackward->setEnabled(m_frames.size() > 1 && m_canvas != m_scratch);
    setWindowTitle((m_canvas == m_scratch ? tr("Image Editor: Scratch Page")
        : tr("Image Editor: %1 (%2/%3)").arg(m_resourceName).arg(m_frameIndex + 1).arg(m_frames.size()))
        + (m_history->isClean() ? QString() : QStringLiteral(" *")));
}
void ImageEditorWindow::updateOnion()
{
    if (!m_canvas) return;
    QList<QImage> images;
    if (m_canvas != m_scratch) {
        for (int i = qMax(0, m_frameIndex - m_onionBackward->value()); i < m_frameIndex; ++i) images.append(m_frames.at(i)->image());
        for (int i = m_frameIndex + 1; i <= qMin(m_frames.size() - 1, m_frameIndex + m_onionForward->value()); ++i) images.append(m_frames.at(i)->image());
    }
    m_canvas->setOnionImages(images, m_onionOpacity->value());
}
void ImageEditorWindow::chooseColor(bool secondary)
{
    QColor initial = secondary ? m_background : m_foreground; initial.setAlpha(m_opacitySpin->value());
    QColor color = EditorColorDialog::getColor(initial, this, tr("Drawing Color"), QColorDialog::ShowAlphaChannel);
    if (!color.isValid()) return;
    m_opacitySpin->setValue(color.alpha()); color.setAlpha(255); (secondary ? m_background : m_foreground) = color;
    if (secondary) m_rightErase->setChecked(false); updateColors();
}
void ImageEditorWindow::updateColors()
{
    if (!m_canvas) return;
    m_canvas->setForeground(m_foreground); m_canvas->setBackground(m_background);
    m_foregroundButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(m_foreground.name()));
    m_backgroundButton->setStyleSheet(m_rightErase->isChecked() ? QStringLiteral("background-color: #999;") : QStringLiteral("background-color: %1;").arg(m_background.name()));
    m_backgroundButton->setText(m_rightErase->isChecked() ? tr("Erase") : QString());
}
void ImageEditorWindow::publishImages()
{
    m_canvas->finishGesture(); QList<QImage> images;
    for (auto *canvas : m_frames) images.append(canvas->image());
    emit imagesAccepted(images, m_origin); emit imageAccepted(images.at(m_frameIndex));
}
void ImageEditorWindow::acceptImage() { publishImages(); m_accepted = true; close(); }
void ImageEditorWindow::closeEvent(QCloseEvent *event)
{
    m_canvas->finishGesture(); bool modified = m_origin != m_originalOrigin;
    for (int i = 0; i < m_frames.size(); ++i) modified = modified || m_frames.at(i)->image() != m_originalImages.value(i);
    if (!m_accepted && modified) {
        const auto answer = EditorMessageBox::question(this, tr("Image Editor"), tr("Apply changes to these images?"),
            EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel) { event->ignore(); return; }
        if (answer == EditorMessageBox::Save) publishImages();
    }
    event->accept();
}
void ImageEditorWindow::exportImage()
{
    m_canvas->finishGesture(); const QString path = QFileDialog::getSaveFileName(this, tr("Save Image"), QString(), tr("PNG Images (*.png)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !m_canvas->image().save(&file, "PNG") || !file.commit()) EditorMessageBox::critical(this, tr("Cannot Save Image"), file.errorString());
}
void ImageEditorWindow::newImage()
{
    QImage image(m_canvas->image().size(), QImage::Format_ARGB32); if (image.isNull()) return;
    image.fill(Qt::transparent); m_canvas->replaceImage(image, tr("New image"));
}
void ImageEditorWindow::openImage(bool paste)
{
    const QString path = QFileDialog::getOpenFileName(this, paste ? tr("Paste from File") : tr("Open Image"), QString(), tr("Images (*.png *.bmp *.jpg *.jpeg *.gif *.tif *.tiff)"));
    if (path.isEmpty()) return;
    QImageReader reader(path); const QImage image = reader.read();
    if (image.isNull()) { EditorMessageBox::critical(this, tr("Cannot Read Image"), reader.errorString()); return; }
    if (paste) { m_canvas->pasteImage(image, m_canvas->selectionBounds().topLeft(), tr("Paste from file")); return; }
    m_canvas->finishGesture();
    const QList<ImageCanvas *> targets = m_canvas == m_scratch ? QList<ImageCanvas *>({m_scratch}) : m_frames;
    if (image.width() > 8192 || image.height() > 8192) {
        EditorMessageBox::warning(this, tr("Open Image"), tr("Images must not exceed 8192 pixels on either axis.")); return;
    }
    ImageOperationSettings settings; settings.size = image.size(); settings.anchor = 0;
    QList<QImage> results;
    for (auto *canvas : targets) {
        const QImage result = canvas == m_canvas ? image : ImageOperations::apply(canvas->image(), ImageOperations::Operation::Resize, settings);
        if (result.isNull()) { EditorMessageBox::warning(this, tr("Open Image"), tr("Cannot allocate the resulting image.")); return; }
        results.append(result);
    }
    m_history->beginMacro(tr("Open image"));
    for (int i = 0; i < targets.size(); ++i) targets.at(i)->replaceImage(results.at(i), tr("Resize canvas"));
    m_history->endMacro(); updateOnion();
}

void ImageEditorWindow::applyOperation(ImageOperations::Operation operation, const QString &title)
{
    m_canvas->finishGesture();
    const bool resize = operation == ImageOperations::Operation::Resize
        || operation == ImageOperations::Operation::Stretch || operation == ImageOperations::Operation::Trim;
    const QImage source = resize ? m_canvas->image() : m_canvas->editingImage();
    ImageOperationDialog dialog(source, operation, title, m_foreground, m_background, this, m_hasOrigin && m_canvas != m_scratch);
    if (dialog.exec() != QDialog::Accepted) return;
    const ImageOperationSettings settings = dialog.settings();
    if (!resize) {
        const QImage result = ImageOperations::apply(source, operation, settings);
        if (result.isNull()) { EditorMessageBox::warning(this, title, tr("Cannot allocate the resulting image.")); return; }
        m_canvas->replaceSelection(result, title); return;
    }
    const QList<ImageCanvas *> targets = m_canvas == m_scratch ? QList<ImageCanvas *>({m_scratch}) : m_frames;
    QRect bounds;
    if (operation == ImageOperations::Operation::Trim) {
        for (auto *canvas : targets) bounds = bounds.united(ImageOperations::opaqueBounds(canvas->image()));
        if (bounds.isEmpty()) bounds = QRect(0, 0, 1, 1);
        bounds.adjust(-settings.radius, -settings.radius, settings.radius, settings.radius);
        if (bounds.isEmpty() || bounds.width() > 8192 || bounds.height() > 8192) {
            EditorMessageBox::warning(this, title, tr("The resulting size must be between 1 and 8192 pixels on each axis.")); return;
        }
    }
    QList<QImage> results;
    for (auto *canvas : targets) {
        const QImage result = operation == ImageOperations::Operation::Trim ? canvas->image().copy(bounds)
            : ImageOperations::apply(canvas->image(), operation, settings);
        if (result.isNull()) { EditorMessageBox::warning(this, title, tr("Cannot allocate the resulting image.")); return; }
        results.append(result);
    }
    m_history->beginMacro(title);
    for (int index = 0; index < targets.size(); ++index) targets.at(index)->replaceImage(results.at(index), title);
    if (m_hasOrigin && m_canvas != m_scratch && settings.adjustOrigin) {
        QPoint origin = m_origin;
        if (operation == ImageOperations::Operation::Trim) origin -= bounds.topLeft();
        else if (operation == ImageOperations::Operation::Resize)
            origin += QPoint((settings.size.width() - source.width()) * (settings.anchor % 3) / 2,
                (settings.size.height() - source.height()) * (settings.anchor / 3) / 2);
        else origin = QPoint(qRound(double(origin.x()) * settings.size.width() / source.width()),
            qRound(double(origin.y()) * settings.size.height() / source.height()));
        if (origin != m_origin) m_history->push(new ImageOriginCommand(&m_origin, origin));
    }
    m_history->endMacro(); updateOnion();
}

void ImageEditorWindow::paletteFile(bool save)
{
    const QString filter = tr("JASC color palettes (*.pal)");
    const QString path = save ? QFileDialog::getSaveFileName(this, tr("Save Color Palette"), QString(), filter)
        : QFileDialog::getOpenFileName(this, tr("Load Color Palette"), QString(), filter);
    if (path.isEmpty()) return;
    QString error;
    if (!(save ? m_palette->save(path, error) : m_palette->load(path, error)))
        EditorMessageBox::critical(this, tr("Color Palette"), error);
}

void ImageEditorWindow::configureGrid()
{
    EditorDialog dialog(this); dialog.setWindowTitle(tr("Grid Settings"));
    auto *layout = new QVBoxLayout(dialog.bodyWidget()); auto *form = new QFormLayout; layout->addLayout(form);
    auto spin = [&](const QString &label, int value, int minimum) {
        auto *widget = new QSpinBox; widget->setRange(minimum, 8192); widget->setValue(value); form->addRow(label, widget); return widget;
    };
    auto *width = spin(tr("Width:"), m_gridSize.width(), 1); auto *height = spin(tr("Height:"), m_gridSize.height(), 1);
    auto *horizontal = spin(tr("Horizontal offset:"), m_gridOffset.x(), 0); auto *vertical = spin(tr("Vertical offset:"), m_gridOffset.y(), 0);
    QColor color = m_gridColor; auto *button = new QPushButton(tr("Choose...")); form->addRow(tr("Color:"), button);
    connect(button, &QPushButton::clicked, &dialog, [&] {
        const QColor chosen = EditorColorDialog::getColor(color, &dialog, tr("Grid Color")); if (chosen.isValid()) color = chosen;
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    m_gridSize = QSize(width->value(), height->value()); m_gridOffset = QPoint(horizontal->value(), vertical->value()); m_gridColor = color; updateTools();
}

void ImageEditorWindow::configureBackground()
{
    EditorDialog dialog(this); dialog.setWindowTitle(tr("Background Settings"));
    auto *layout = new QVBoxLayout(dialog.bodyWidget()); auto *form = new QFormLayout; layout->addLayout(form);
    QColor first = m_checkerFirst, second = m_checkerSecond;
    auto colorButton = [&](const QString &label, QColor &color) {
        auto *button = new QPushButton(tr("Choose...")); form->addRow(label, button); QColor *target = &color;
        connect(button, &QPushButton::clicked, &dialog, [&, target] {
            const QColor chosen = EditorColorDialog::getColor(*target, &dialog, tr("Background Color")); if (chosen.isValid()) *target = chosen;
        });
    };
    colorButton(tr("First color:"), first); colorButton(tr("Second color:"), second);
    auto *solid = new QCheckBox(tr("Solid color")); solid->setChecked(first == second); form->addRow(solid);
    auto *size = new QSpinBox; size->setRange(1, 64); size->setValue(m_checkerSize); form->addRow(tr("Checker size:"), size);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    m_checkerFirst = first; m_checkerSecond = solid->isChecked() ? first : second; m_checkerSize = size->value(); updateTools();
}

void ImageEditorWindow::drawText(const QPoint &position, bool secondary, bool editing)
{
    ImageText settings;
    if (editing) settings = m_canvas->floatingText();
    else {
        settings.text = m_text; settings.font = m_textFont; settings.alignment = m_textAlignment->currentIndex();
        settings.color = secondary ? m_background : m_foreground; settings.color.setAlpha(m_opacitySpin->value());
        settings.antialias = m_antialias->isChecked();
    }
    EditorDialog dialog(this); dialog.setWindowTitle(tr("Draw Text")); auto *layout = new QVBoxLayout(dialog.bodyWidget());
    auto *text = new QPlainTextEdit(settings.text); text->setFont(settings.font); text->setMinimumSize(360, 150); layout->addWidget(text);
    auto *format = new QHBoxLayout; layout->addLayout(format);
    auto *font = new QPushButton(tr("Font...")); format->addWidget(font);
    connect(font, &QPushButton::clicked, &dialog, [&] {
        bool ok; const QFont value = QFontDialog::getFont(&ok, settings.font, &dialog);
        if (ok) { settings.font = value; text->setFont(value); }
    });
    auto *color = new QPushButton(tr("Color...")); format->addWidget(color);
    connect(color, &QPushButton::clicked, &dialog, [&] {
        const QColor value = EditorColorDialog::getColor(settings.color, &dialog, tr("Text Color"), QColorDialog::ShowAlphaChannel);
        if (value.isValid()) settings.color = value;
    });
    auto *alignment = new QComboBox; alignment->addItems({tr("Left"), tr("Center"), tr("Right")});
    alignment->setCurrentIndex(settings.alignment); format->addWidget(alignment);
    auto *antialias = new QCheckBox(tr("Anti-alias")); antialias->setChecked(settings.antialias); format->addWidget(antialias);
    layout->addWidget(new QLabel(tr("Use # or a line break to start a new line.")));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || text->toPlainText().isEmpty()) return;
    settings.alignment = alignment->currentIndex(); settings.antialias = antialias->isChecked();
    m_textFont = settings.font; m_textAlignment->setCurrentIndex(settings.alignment);
    settings.text = m_text = text->toPlainText(); const QString rendered = QString(m_text).replace(QLatin1Char('#'), QLatin1Char('\n'));
    const QFontMetrics metrics(settings.font); const QStringList lines = rendered.split(QLatin1Char('\n')); int width = 1;
    for (const QString &line : lines) width = qMax(width, metrics.boundingRect(line).width() + 4);
    const int height = metrics.lineSpacing() * lines.size() + 4;
    if (width > 8192 || height > 8192) { EditorMessageBox::warning(this, tr("Draw Text"), tr("The text image is too large.")); return; }
    QImage image(width, height, QImage::Format_ARGB32); if (image.isNull()) return; image.fill(Qt::transparent);
    QPainter painter(&image); painter.setFont(settings.font); painter.setRenderHint(QPainter::TextAntialiasing, settings.antialias);
    painter.setPen(settings.color);
    const int alignments[] = {Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight};
    painter.drawText(image.rect().adjusted(2, 2, -2, -2), alignments[settings.alignment] | Qt::AlignTop, rendered); painter.end();
    m_canvas->pasteText(image, position, settings, editing);
}
