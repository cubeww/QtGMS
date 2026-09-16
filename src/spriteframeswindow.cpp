#include "editordialog.h"
#include "editorstandarddialogs.h"
#include "spriteframeswindow.h"
#include "spritedocument.h"
#include "imagecanvas.h"
#include "imageeditorwindow.h"
#include "imageoperationdialog.h"
#include "imageresizer.h"
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QSet>
#include <QFileInfo>

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QPainter>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

// Keep small frames at their native size, including their transparency pattern.
class SpriteFrameDelegate : public QStyledItemDelegate
{
public:
    explicit SpriteFrameDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        const bool selected = option.state & QStyle::State_Selected;
        if (selected) painter->fillRect(option.rect.adjusted(1, 1, -1, -1), option.palette.highlight());
        const QPixmap image = index.data(Qt::DecorationRole).value<QPixmap>();
        painter->drawPixmap(option.rect.center().x() - image.width() / 2, option.rect.top() + 5, image);
        painter->setPen(selected ? option.palette.highlightedText().color() : option.palette.text().color());
        painter->drawText(option.rect.adjusted(2, image.height() + 9, -2, -2), Qt::AlignHCenter | Qt::AlignTop, index.data().toString());
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override
    {
        return index.data(Qt::SizeHintRole).toSize();
    }
};

SpriteFramesWindow::SpriteFramesWindow(SpriteDocument *document, QWidget *parent)
    : EditorWindow(parent), m_document(document), m_frames(new QListWidget),
      m_preview(new ImageCanvas), m_status(new QLabel(this)), m_timer(new QTimer(this))
{
    setObjectName(QStringLiteral("spriteFramesEditor"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Sprite Editor: %1").arg(document->name()));
    resize(671, 680);
    setMinimumSize(460, 340);
    auto *fileMenu = editorMenuBar()->addMenu(tr("&File"));
    auto *editMenu = editorMenuBar()->addMenu(tr("&Edit"));
    auto *transformMenu = editorMenuBar()->addMenu(tr("&Transform"));
    auto *imagesMenu = editorMenuBar()->addMenu(tr("&Images"));
    auto *animationMenu = editorMenuBar()->addMenu(tr("&Animation"));
    auto *toolbar = new QToolBar(this);
    toolbar->setMovable(false); toolbar->setFloatable(false);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setFixedHeight(30);
    addToolBar(toolbar);
    const auto action = [this](QMenu *menu, const QString &text, const QString &icon, const QKeySequence &shortcut,
                               const std::function<void()> &callback) {
        auto *result = menu->addAction(QIcon(QStringLiteral(":/images/%1.png").arg(icon)), text);
        result->setIconVisibleInMenu(true);
        result->setShortcut(shortcut);
        connect(result, &QAction::triggered, this, callback);
        return result;
    };
    QAction *create = action(fileMenu, tr("&New..."), QStringLiteral("editor/spritenew"), QKeySequence::New, [this] { newSprite(); });
    QAction *load = action(fileMenu, tr("&Create from File..."), QStringLiteral("open"), QKeySequence::Open, [this] { importFrames(true); });
    QAction *add = action(fileMenu, tr("&Add from File..."), QStringLiteral("editor/add"), QKeySequence(Qt::CTRL | Qt::Key_A), [this] { importFrames(false); });
    fileMenu->addSeparator();
    QAction *save = action(fileMenu, tr("&Save as PNG File..."), QStringLiteral("save"), QKeySequence::Save, [this] { exportStrip(); });
    m_imageActions.append(save);
    fileMenu->addSeparator();
    action(fileMenu, tr("Create from Str&ip..."), QStringLiteral("editor/createstrip"), QKeySequence(), [this] { importStrip(true); });
    action(fileMenu, tr("Add from Stri&p..."), QStringLiteral("editor/addstrip"), QKeySequence(), [this] { importStrip(false); });
    fileMenu->addSeparator();
    QAction *ok = action(fileMenu, tr("&Close Saving Changes"), QStringLiteral("editor/ok"), QKeySequence(Qt::CTRL | Qt::Key_Return), [this] { close(); });
    auto *undo = document->undoStack()->createUndoAction(this, tr("&Undo"));
    auto *redo = document->undoStack()->createRedoAction(this, tr("&Redo"));
    undo->setIcon(QIcon(QStringLiteral(":/images/editor/undo.png")));
    redo->setIcon(QIcon(QStringLiteral(":/images/editor/redo.png")));
    undo->setIconVisibleInMenu(true); redo->setIconVisibleInMenu(true);
    undo->setShortcut(QKeySequence::Undo); redo->setShortcut(QKeySequence::Redo);
    editMenu->addActions({undo, redo});
    editMenu->addSeparator();
    QAction *cut = action(editMenu, tr("Cu&t"), QStringLiteral("editor/cut"), QKeySequence::Cut, [this] { copyFrames(true); });
    QAction *copy = action(editMenu, tr("&Copy"), QStringLiteral("editor/copy"), QKeySequence::Copy, [this] { copyFrames(false); });
    m_pasteAction = action(editMenu, tr("&Paste"), QStringLiteral("editor/paste"), QKeySequence::Paste, [this] { pasteFrames(); });
    editMenu->addSeparator();
    QAction *erase = action(editMenu, tr("&Erase..."), QStringLiteral("editor/erase"), QKeySequence(), [this] { eraseFrames(); });
    QAction *remove = action(editMenu, tr("&Delete"), QStringLiteral("editor/delete"), QKeySequence::Delete, [this] { deleteFrame(); });
    editMenu->addSeparator();
    m_moveLeftAction = action(editMenu, tr("Move &Left"), QStringLiteral("editor/left"), QKeySequence(Qt::ALT | Qt::Key_Left), [this] { moveFrame(-1); });
    m_moveRightAction = action(editMenu, tr("Move &Right"), QStringLiteral("editor/right"), QKeySequence(Qt::ALT | Qt::Key_Right), [this] { moveFrame(1); });
    editMenu->addSeparator();
    QAction *append = action(editMenu, tr("&Add Empty"), QStringLiteral("editor/spriteadd"), QKeySequence(Qt::SHIFT | Qt::Key_Insert), [this] { insertFrame(true); });
    QAction *insert = action(editMenu, tr("&Insert Empty"), QStringLiteral("editor/spriteinsert"), QKeySequence(Qt::Key_Insert), [this] { insertFrame(); });
    editMenu->addSeparator();
    QAction *edit = action(editMenu, tr("Edi&t..."), QStringLiteral("editor/edit"), QKeySequence(Qt::ALT | Qt::Key_Return), [this] { editFrame(); });
    editMenu->addSeparator();
    action(editMenu, tr("Set &Transparency Background..."), QStringLiteral("editor/transparencybackground"), QKeySequence(), [this] { setTransparencyBackground(); });
    m_selectionActions << cut << copy << erase << remove << edit;

    using Operation = ImageOperations::Operation;
    const auto effect = [this, &action](QMenu *menu, const QString &text, const QString &icon, Operation operation) {
        m_imageActions.append(action(menu, text, QStringLiteral("editor/") + icon, QKeySequence(), [this, operation, text] {
            QString title = text; title.remove(QLatin1Char('&')); title.remove(QStringLiteral("..."));
            applyOperation(operation, title);
        }));
    };
    effect(transformMenu, tr("&Shift..."), QStringLiteral("shift"), Operation::Shift);
    effect(transformMenu, tr("&Mirror/Flip..."), QStringLiteral("mirror"), Operation::Mirror);
    effect(transformMenu, tr("&Rotate..."), QStringLiteral("rotate"), Operation::Rotate);
    effect(transformMenu, tr("Sc&ale..."), QStringLiteral("scale"), Operation::Scale);
    effect(transformMenu, tr("S&kew..."), QStringLiteral("skew"), Operation::Skew);
    transformMenu->addSeparator();
    effect(transformMenu, tr("Resize &Canvas..."), QStringLiteral("resize"), Operation::Resize);
    effect(transformMenu, tr("Str&etch..."), QStringLiteral("stretch"), Operation::Stretch);
    effect(transformMenu, tr("&Trim..."), QStringLiteral("trim"), Operation::Trim);
    m_imageActions.append(action(imagesMenu, tr("Cycle &Left"), QStringLiteral("editor/cycleleft"), QKeySequence(), [this] { cycleFrames(-1); }));
    m_imageActions.append(action(imagesMenu, tr("Cycle &Right"), QStringLiteral("editor/cycleright"), QKeySequence(), [this] { cycleFrames(1); }));
    imagesMenu->addSeparator();
    effect(imagesMenu, tr("Black and &White..."), QStringLiteral("grayscale"), Operation::Grayscale);
    effect(imagesMenu, tr("&Colorize..."), QStringLiteral("colorize"), Operation::Colorize);
    effect(imagesMenu, tr("C&olorize Partial..."), QStringLiteral("colorize"), Operation::ColorizePartial);
    effect(imagesMenu, tr("&Intensity..."), QStringLiteral("intensity"), Operation::Intensity);
    effect(imagesMenu, tr("In&vert..."), QStringLiteral("invert"), Operation::Invert);
    imagesMenu->addSeparator();
    effect(imagesMenu, tr("&Make Opaque..."), QStringLiteral("opaque"), Operation::Opaque);
    effect(imagesMenu, tr("&Erase a Color..."), QStringLiteral("erasecolor"), Operation::EraseColor);
    effect(imagesMenu, tr("&Smooth Edges..."), QStringLiteral("smoothedges"), Operation::SmoothEdges);
    effect(imagesMenu, tr("O&pacity..."), QStringLiteral("opacity"), Operation::Opacity);
    effect(imagesMenu, tr("Set &Alpha from File..."), QStringLiteral("alpha"), Operation::Alpha);
    effect(imagesMenu, tr("&Fade..."), QStringLiteral("fade"), Operation::Fade);
    effect(imagesMenu, tr("&Blur..."), QStringLiteral("blur"), Operation::Blur);
    effect(imagesMenu, tr("S&harpen..."), QStringLiteral("sharpen"), Operation::Sharpen);
    imagesMenu->addSeparator();
    effect(imagesMenu, tr("O&utline..."), QStringLiteral("outline"), Operation::Outline);
    effect(imagesMenu, tr("Sha&dow..."), QStringLiteral("shadow"), Operation::Shadow);
    effect(imagesMenu, tr("&Glow..."), QStringLiteral("glow"), Operation::Glow);
    effect(imagesMenu, tr("B&uttonize..."), QStringLiteral("buttonize"), Operation::Button);
    effect(imagesMenu, tr("G&radient Fill..."), QStringLiteral("gradient"), Operation::Gradient);

    const auto animation = [this](QMenu *menu, const QString &text, const std::function<void()> &callback) {
        auto *result = menu->addAction(text); connect(result, &QAction::triggered, this, callback); m_imageActions.append(result);
    };
    animation(animationMenu, tr("Set &Length..."), [this] { changeAnimationLength(false); });
    animation(animationMenu, tr("&Stretch..."), [this] { changeAnimationLength(true); });
    animationMenu->addSeparator();
    animation(animationMenu, tr("&Reverse"), [this] { reverseAnimation(false); });
    animation(animationMenu, tr("&Add Reverse"), [this] { reverseAnimation(true); });
    animationMenu->addSeparator();
    using Kind = SpriteAnimation::Kind;
    animation(animationMenu, tr("&Translation Sequence..."), [this] { createAnimation(Kind::Translation); });
    auto *rotation = animationMenu->addMenu(tr("R&otation Sequence"));
    animation(rotation, tr("&Clockwise"), [this] { createAnimation(Kind::Rotation, 0); });
    animation(rotation, tr("C&ounter-Clockwise"), [this] { createAnimation(Kind::Rotation, 1); });
    animationMenu->addSeparator();
    animation(animationMenu, tr("&Colorize..."), [this] { createAnimation(Kind::Colorize); });
    animation(animationMenu, tr("&Fade to Color..."), [this] { createAnimation(Kind::Fade); });
    animation(animationMenu, tr("&Disappear"), [this] { createAnimation(Kind::Disappear); });
    animationMenu->addSeparator();
    const auto directional = [&animation, this, animationMenu](const QString &title, Kind kind, bool center) {
        auto *menu = animationMenu->addMenu(title);
        const QStringList names = {tr("Center"), tr("Left"), tr("Right"), tr("Top"), tr("Bottom")};
        for (int direction = center ? 0 : 1; direction < names.size(); ++direction)
            animation(menu, names.at(direction), [this, kind, direction] { createAnimation(kind, direction); });
    };
    directional(tr("S&hrink"), Kind::Shrink, true); directional(tr("&Grow"), Kind::Grow, true);
    directional(tr("Flatte&n"), Kind::Flatten, false); directional(tr("Ra&ise"), Kind::Raise, false);
    animationMenu->addSeparator();
    animation(animationMenu, tr("&Overlay..."), [this] { blendAnimation(false); });
    animation(animationMenu, tr("&Morph..."), [this] { blendAnimation(true); });
    toolbar->addAction(ok); toolbar->addSeparator();
    toolbar->addActions({create, load, add, save}); toolbar->addSeparator();
    toolbar->addActions({insert, append}); toolbar->addSeparator();
    toolbar->addActions({undo, redo}); toolbar->addSeparator();
    toolbar->addActions({cut, copy, m_pasteAction}); toolbar->addSeparator();
    toolbar->addActions({m_moveLeftAction, m_moveRightAction}); toolbar->addSeparator();
    toolbar->addAction(edit);
    auto *premultiply = toolbar->addAction(QIcon(QStringLiteral(":/images/editor/opacity.png")), tr("Pre-Multiply Alpha"));
    connect(premultiply, &QAction::triggered, this, &SpriteFramesWindow::premultiplyAlpha);
    m_selectionActions.append(premultiply);

    auto *splitter = new QSplitter(this);
    splitter->setHandleWidth(3); splitter->setChildrenCollapsible(false);
    auto *left = new QWidget(splitter); left->setMinimumWidth(129);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(6, 6, 6, 6);
    auto *showPreview = new QCheckBox(tr("Show Preview"), left);
    leftLayout->addWidget(showPreview);
    auto *previewPanel = new QWidget(left);
    auto *previewLayout = new QVBoxLayout(previewPanel); previewLayout->setContentsMargins(0, 0, 0, 0);
    auto *previewScroll = new QScrollArea(previewPanel);
    m_preview->setEditable(false); previewScroll->setWidget(m_preview);
    previewLayout->addWidget(previewScroll, 1);
    auto *speed = new QSpinBox(previewPanel); speed->setRange(1, 1000); speed->setValue(30);
    auto *speedLayout = new QFormLayout; speedLayout->addRow(tr("Speed:"), speed); previewLayout->addLayout(speedLayout);
    auto *note = new QLabel(tr("This is not the speed in the game! Use only for preview."), previewPanel);
    note->setWordWrap(true); previewLayout->addWidget(note);
    auto *colorButton = new QPushButton(tr("Background Color"), previewPanel); previewLayout->addWidget(colorButton);
    connect(colorButton, &QPushButton::clicked, this, [this] {
        const QColor color = EditorColorDialog::getColor(m_previewColor, this);
        if (color.isValid()) { m_previewColor = color; refreshPreview(); }
    });
    previewLayout->addWidget(new QLabel(tr("Background:"), previewPanel));
    auto *backgroundButton = new QPushButton(tr("<no image>"), previewPanel); previewLayout->addWidget(backgroundButton);
    auto *backgroundMenu = new QMenu(backgroundButton); backgroundButton->setMenu(backgroundMenu);
    backgroundMenu->addAction(tr("None"), this, [this, backgroundButton] {
        m_previewBackground = QImage(); backgroundButton->setText(tr("<no image>")); refreshPreview();
    });
    backgroundMenu->addAction(tr("Load Image..."), this, [this, backgroundButton] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Preview Background"), QString(), tr("Images (*.png *.bmp *.jpg *.gif)"));
        if (path.isEmpty()) return;
        QImageReader reader(path); const QImage image = reader.read();
        if (image.isNull()) { EditorMessageBox::warning(this, tr("Cannot Load Image"), reader.errorString()); return; }
        m_previewBackground = image; backgroundButton->setText(QFileInfo(path).fileName()); refreshPreview();
    });
    auto *stretchBackground = new QCheckBox(tr("Stretch background"), previewPanel); previewLayout->addWidget(stretchBackground);
    connect(stretchBackground, &QCheckBox::toggled, this, [this](bool stretch) { m_stretchBackground = stretch; refreshPreview(); });
    leftLayout->addWidget(previewPanel, 1);
    leftLayout->addStretch(); previewPanel->hide();
    m_timer->setInterval(33);
    connect(showPreview, &QCheckBox::toggled, this, [this, previewPanel, leftLayout](bool visible) {
        previewPanel->setVisible(visible); leftLayout->setStretch(2, visible ? 0 : 1);
        if (visible) { refreshPreview(); m_timer->start(); } else m_timer->stop();
    });
    connect(speed, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int fps) { m_timer->setInterval(qMax(1, 1000 / fps)); });
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (m_document->state().frames.size() > 1) { ++m_previewFrame; refreshPreview(); }
    });
    m_frames->setViewMode(QListView::IconMode); m_frames->setMovement(QListView::Static);
    m_frames->setResizeMode(QListView::Adjust); m_frames->setSpacing(4);
    m_frames->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_frames->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_frames->setItemDelegate(new SpriteFrameDelegate(m_frames));
    m_frames->setStyleSheet(QStringLiteral("QListWidget { background: #141414; border: none; }"));
    m_frames->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_frames, &QWidget::customContextMenuRequested, this, [this, editMenu](const QPoint &position) {
        editMenu->exec(m_frames->viewport()->mapToGlobal(position));
    });
    splitter->addWidget(left); splitter->addWidget(m_frames);
    splitter->setSizes({129, 531}); splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);
    statusBar()->addWidget(m_status);
    connect(m_frames, &QListWidget::itemActivated, this, [this] { editFrame(); });
    connect(m_frames, &QListWidget::currentRowChanged, this, [this](int row) { m_previewFrame = qMax(0, row); refreshPreview(); });
    connect(m_frames, &QListWidget::itemSelectionChanged, this, &SpriteFramesWindow::updateActions);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &SpriteFramesWindow::updateActions);
    connect(m_document, &SpriteDocument::changed, this, &SpriteFramesWindow::refresh);
    refresh();
}

QList<int> SpriteFramesWindow::selectedRows() const
{
    QList<int> rows;
    for (QListWidgetItem *item : m_frames->selectedItems()) rows.append(m_frames->row(item));
    std::sort(rows.begin(), rows.end());
    return rows;
}

void SpriteFramesWindow::updateActions()
{
    const QList<int> rows = selectedRows();
    for (QAction *action : m_selectionActions) action->setEnabled(!rows.isEmpty());
    for (QAction *action : m_imageActions) action->setEnabled(m_frames->count() > 0);
    m_moveLeftAction->setEnabled(!rows.isEmpty() && rows.first() > 0);
    m_moveRightAction->setEnabled(!rows.isEmpty() && rows.last() + 1 < m_frames->count());
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    m_pasteAction->setEnabled(mime && (mime->hasImage() || mime->hasFormat(QStringLiteral("application/x-qtgms-sprite-frames"))));
}

void SpriteFramesWindow::refresh()
{
    QSet<QString> selected;
    for (QListWidgetItem *item : m_frames->selectedItems()) selected.insert(item->data(Qt::UserRole).toString());
    const QString current = m_frames->currentItem() ? m_frames->currentItem()->data(Qt::UserRole).toString() : QString();
    const int oldRow = m_frames->currentRow();
    const QSignalBlocker blocker(m_frames);
    m_frames->clear();
    int currentRow = -1;
    const SpriteState &state = m_document->state();
    QSize thumbnailSize = state.size;
    if (thumbnailSize.width() > 128 || thumbnailSize.height() > 128) thumbnailSize.scale(128, 128, Qt::KeepAspectRatio);
    const QSize cell(qMax(64, thumbnailSize.width() + 16), thumbnailSize.height() + 30);
    m_frames->setGridSize(cell);
    qint64 memory = 0;
    for (int i = 0; i < state.frames.size(); ++i) {
        const SpriteFrame &frame = state.frames.at(i);
        QImage thumbnail(thumbnailSize, QImage::Format_ARGB32);
        if (!thumbnail.isNull()) {
            QPainter painter(&thumbnail);
            for (int y = 0; y < thumbnail.height(); y += m_checkerSize)
                for (int x = 0; x < thumbnail.width(); x += m_checkerSize)
                    painter.fillRect(x, y, m_checkerSize, m_checkerSize,
                        (x / m_checkerSize + y / m_checkerSize) % 2 ? m_checkerSecond : m_checkerFirst);
            painter.drawImage(thumbnail.rect(), frame.image);
        }
        auto *item = new QListWidgetItem(tr("image %1").arg(i), m_frames);
        item->setData(Qt::DecorationRole, QPixmap::fromImage(thumbnail));
        item->setData(Qt::UserRole, frame.id); item->setData(Qt::SizeHintRole, cell);
        item->setSelected(selected.contains(frame.id));
        if (frame.id == current) currentRow = i;
        memory += qint64(frame.image.bytesPerLine()) * frame.image.height();
    }
    if (currentRow < 0 && !state.frames.isEmpty()) currentRow = qBound(0, oldRow, state.frames.size() - 1);
    m_frames->setCurrentRow(currentRow, QItemSelectionModel::NoUpdate);
    if (m_frames->selectedItems().isEmpty() && currentRow >= 0) m_frames->item(currentRow)->setSelected(true);
    m_status->setText(tr("Frames: %1      Size: %2 x %3      Memory: %4 KB")
        .arg(state.frames.size()).arg(state.size.width()).arg(state.size.height()).arg((memory + 1023) / 1024));
    refreshPreview(); updateActions();
}

void SpriteFramesWindow::refreshPreview()
{
    const QList<SpriteFrame> &frames = m_document->state().frames;
    if (frames.isEmpty()) { m_preview->setImage(QImage()); return; }
    m_previewFrame %= frames.size();
    QImage preview(frames.at(m_previewFrame).image.size(), QImage::Format_ARGB32);
    if (preview.isNull()) return;
    preview.fill(m_previewColor);
    QPainter painter(&preview);
    if (!m_previewBackground.isNull()) {
        if (m_stretchBackground) painter.drawImage(preview.rect(), m_previewBackground);
        else painter.drawTiledPixmap(preview.rect(), QPixmap::fromImage(m_previewBackground));
    }
    painter.drawImage(QPoint(), frames.at(m_previewFrame).image); painter.end();
    m_preview->setImage(preview);
}
void SpriteFramesWindow::editFrame()
{
    if (m_imageEditor) { m_imageEditor->raise(); m_imageEditor->activateWindow(); return; }
    const int index = m_frames->currentRow();
    if (index < 0 || index >= m_document->state().frames.size()) return;
    const SpriteFrame &frame = m_document->state().frames.at(index);
    m_imageEditor = new ImageEditorWindow(frame.image, this);
    QList<QImage> images;
    for (const SpriteFrame &item : m_document->state().frames) images.append(item.image);
    m_imageEditor->setFrames(images, index, m_document->name());
    m_imageEditor->setSpriteOrigin(m_document->state().origin);
    m_imageEditor->setWindowModality(Qt::WindowModal);
    connect(m_imageEditor, &ImageEditorWindow::imagesAccepted, m_document, [this](const QList<QImage> &images, const QPoint &origin) {
        SpriteState state = m_document->state();
        if (images.size() != state.frames.size() || images.isEmpty()) return;
        bool changed = state.origin != origin;
        state.origin = origin;
        for (int index = 0; index < images.size(); ++index) {
            SpriteFrame &frame = state.frames[index];
            if (frame.image == images.at(index)) continue;
            frame.image = images.at(index); frame.modified = true; changed = true;
        }
        if (!changed) return;
        state.size = images.first().size();
        m_document->edit(state, tr("Edit sprite images"));
    });
    m_imageEditor->show();
}
void SpriteFramesWindow::importFrames(bool replace)
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Load Sprite"), QString(),
        tr("Images (*.png *.bmp *.jpg *.jpeg *.gif *.tif *.tiff);;All Files (*)"));
    if (files.isEmpty()) return;
    QList<SpriteFrame> frames; QString error;
    if (!SpriteDocument::importImages(files, frames, error)) { EditorMessageBox::critical(this, tr("Cannot Import Sprite"), error); return; }
    insertFrames(frames, replace, true, replace ? tr("Load sprite") : tr("Add subimages"));
}
void SpriteFramesWindow::newSprite()
{
    EditorDialog dialog(this);
    dialog.setWindowTitle(tr("New Sprite"));
    auto *layout = new QFormLayout(dialog.bodyWidget());
    auto *width = new QSpinBox(&dialog); auto *height = new QSpinBox(&dialog);
    width->setRange(1, 4096); height->setRange(1, 4096); width->setValue(32); height->setValue(32);
    layout->addRow(tr("Width:"), width); layout->addRow(tr("Height:"), height);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    QImage image(width->value(), height->value(), QImage::Format_ARGB32); image.fill(Qt::transparent);
    if (image.isNull()) { EditorMessageBox::critical(this, tr("New Sprite"), tr("Not enough memory to create the image.")); return; }
    SpriteState state = m_document->state(); state.size = image.size(); state.frames = {SpriteDocument::createFrame(image)};
    m_document->edit(state, tr("New sprite image"));
}
void SpriteFramesWindow::insertFrame(bool append)
{
    if (m_document->state().size.isEmpty()) { newSprite(); return; }
    SpriteState state = m_document->state();
    QImage image(state.size, QImage::Format_ARGB32); image.fill(Qt::transparent);
    if (image.isNull()) { EditorMessageBox::critical(this, tr("Insert Image"), tr("Not enough memory to create the image.")); return; }
    const int row = append ? state.frames.size() : qMax(0, m_frames->currentRow());
    state.frames.insert(row, SpriteDocument::createFrame(image));
    m_document->edit(state, tr("Insert subimage")); m_frames->setCurrentRow(row);
}
void SpriteFramesWindow::deleteFrame()
{
    QList<int> rows = selectedRows();
    if (rows.isEmpty()) return;
    SpriteState state = m_document->state();
    for (int index = rows.size() - 1; index >= 0; --index) state.frames.removeAt(rows.at(index));
    m_document->edit(state, tr("Delete subimages"));
}
void SpriteFramesWindow::moveFrame(int direction)
{
    QList<int> rows = selectedRows();
    SpriteState state = m_document->state();
    if (rows.isEmpty() || rows.first() + direction < 0 || rows.last() + direction >= state.frames.size()) return;
    if (direction > 0) std::reverse(rows.begin(), rows.end());
    for (int row : rows) state.frames.swap(row, row + direction);
    m_document->edit(state, tr("Move subimages"));
}
void SpriteFramesWindow::importStrip(bool replace)
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Strip"), QString(), tr("Images (*.png *.bmp *.gif *.jpg)"));
    if (path.isEmpty()) return;
    QImageReader reader(path); const QImage strip = reader.read();
    if (strip.isNull()) { EditorMessageBox::critical(this, tr("Cannot Load Strip"), reader.errorString()); return; }
    EditorDialog dialog(this); dialog.setWindowTitle(tr("Loading a Strip Image")); dialog.resize(650, 450);
    auto *layout = new QHBoxLayout(dialog.bodyWidget());
    auto *controls = new QFormLayout; layout->addLayout(controls);
    auto *canvas = new ImageCanvas; canvas->setEditable(false); canvas->setImage(strip);
    auto *scroll = new QScrollArea; scroll->setWidget(canvas); scroll->setMinimumSize(260, 250); layout->addWidget(scroll, 1);
    const auto number = [controls](const QString &label, int minimum, int maximum, int value) {
        auto *spin = new QSpinBox; spin->setRange(minimum, maximum); spin->setValue(value);
        spin->setKeyboardTracking(false); controls->addRow(label, spin); return spin;
    };
    const QSize initial = m_document->state().size.isEmpty() ? QSize(32, 32) : m_document->state().size;
    const int initialWidth = qMin(initial.width(), strip.width()), initialHeight = qMin(initial.height(), strip.height());
    auto *count = number(tr("Number of images:"), 1, 10000, qMin(10000, (strip.width() / initialWidth) * (strip.height() / initialHeight)));
    auto *columns = number(tr("Images per row:"), 1, 10000, strip.width() / initialWidth);
    auto *width = number(tr("Image width:"), 1, 8192, initialWidth);
    auto *height = number(tr("Image height:"), 1, 8192, initialHeight);
    auto *cellX = number(tr("Horizontal cell offset:"), 0, 10000, 0);
    auto *cellY = number(tr("Vertical cell offset:"), 0, 10000, 0);
    auto *offsetX = number(tr("Horizontal pixel offset:"), 0, strip.width(), 0);
    auto *offsetY = number(tr("Vertical pixel offset:"), 0, strip.height(), 0);
    auto *gapX = number(tr("Horizontal separation:"), 0, strip.width(), 0);
    auto *gapY = number(tr("Vertical separation:"), 0, strip.height(), 0);
    auto *error = new QLabel; error->setWordWrap(true); error->setMaximumWidth(240); controls->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setIcon(QIcon(QStringLiteral(":/images/editor/ok.png")));
    buttons->button(QDialogButtonBox::Cancel)->setIcon(QIcon(QStringLiteral(":/images/close.png")));
    controls->addRow(buttons);
    QList<QRect> rectangles;
    const auto update = [=, &rectangles] {
        rectangles.clear(); bool valid = true;
        for (int index = 0; index < count->value(); ++index) {
            const qint64 x = qint64(index % columns->value() + cellX->value()) * (width->value() + gapX->value()) + offsetX->value();
            const qint64 y = qint64(index / columns->value() + cellY->value()) * (height->value() + gapY->value()) + offsetY->value();
            if (x + width->value() > strip.width() || y + height->value() > strip.height()) { valid = false; break; }
            rectangles.append(QRect(int(x), int(y), width->value(), height->value()));
        }
        QImage overlay(strip.size(), QImage::Format_ARGB32); overlay.fill(Qt::transparent);
        if (!overlay.isNull()) {
            QPainter painter(&overlay); painter.setPen(QColor(120, 210, 20));
            for (const QRect &rectangle : rectangles) painter.drawRect(rectangle.adjusted(0, 0, -1, -1));
        }
        canvas->setOverlay(overlay);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
        error->setText(valid ? QString() : tr("The selected frames extend beyond the strip image."));
    };
    for (auto *spin : {count, columns, width, height, cellX, cellY, offsetX, offsetY, gapX, gapY})
        connect(spin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), &dialog, update);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    update();
    if (dialog.exec() != QDialog::Accepted) return;
    QList<SpriteFrame> frames;
    for (const QRect &rectangle : rectangles) {
        const QImage image = strip.copy(rectangle);
        if (image.isNull()) { EditorMessageBox::critical(this, tr("Cannot Load Strip"), tr("Not enough memory to extract the images.")); return; }
        frames.append(SpriteDocument::createFrame(image));
    }
    insertFrames(frames, replace, true, replace ? tr("Create sprite from strip") : tr("Add sprite from strip"));
}
void SpriteFramesWindow::exportStrip()
{
    const SpriteState &state = m_document->state();
    if (state.frames.isEmpty()) return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Export PNG Strip"), m_document->name() + tr("_strip%1.png").arg(state.frames.size()), tr("PNG Images (*.png)"));
    if (path.isEmpty()) return;
    const qint64 width = qint64(state.size.width()) * state.frames.size();
    if (width > 32767) { EditorMessageBox::warning(this, tr("Cannot Export Strip"), tr("The resulting strip is too wide.")); return; }
    QImage strip(int(width), state.size.height(), QImage::Format_ARGB32);
    if (strip.isNull()) { EditorMessageBox::critical(this, tr("Cannot Export Strip"), tr("Not enough memory for the strip.")); return; }
    strip.fill(Qt::transparent);
    QPainter painter(&strip);
    for (int i = 0; i < state.frames.size(); ++i) painter.drawImage(i * state.size.width(), 0, state.frames.at(i).image);
    painter.end();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !strip.save(&file, "PNG") || !file.commit())
        EditorMessageBox::critical(this, tr("Cannot Export Strip"), file.errorString());
}
void SpriteFramesWindow::closeEvent(QCloseEvent *event)
{
    if (m_imageEditor && !m_imageEditor->close()) { event->ignore(); return; }
    event->accept();
}
