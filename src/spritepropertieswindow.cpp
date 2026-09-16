#include <QSignalBlocker>
#include "editorstandarddialogs.h"
#include "spritepropertieswindow.h"
#include "spritedocument.h"
#include "spriteframeswindow.h"
#include "spritemaskwindow.h"
#include "imagecanvas.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>

SpritePropertiesWindow::SpritePropertiesWindow(SpriteDocument *document, const QStringList &textureGroups, QWidget *parent)
    : ResourceEditorWindow(parent), m_document(document), m_preview(new ImageCanvas), m_dimensions(new QLabel),
      m_count(new QLabel), m_frameLabel(new QLabel), m_xOrigin(new QSpinBox), m_yOrigin(new QSpinBox),
      m_precise(new QCheckBox(tr("Precise collision checking"))), m_separate(new QCheckBox(tr("Separate collision masks"))),
      m_modifiedMaskLabel(new QLabel(tr("Modified"))),
      m_horizontal(new QCheckBox(tr("Tile: Horizontal"))), m_vertical(new QCheckBox(tr("Tile: Vertical"))),
      m_for3D(new QCheckBox(tr("Used for 3D"))), m_textureGroup(new QComboBox)
{
    setObjectName(QStringLiteral("spriteProperties"));
    document->setParent(this);
    setAttribute(Qt::WA_DeleteOnClose);
    editorMenuBar()->hide();
    resize(605, 370); setMinimumSize(520, 370);
    auto *body = new QWidget(this);
    auto *layout = new QHBoxLayout(body); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(3);
    auto *settings = new QWidget(body); settings->setFixedWidth(334); settings->setMinimumHeight(334);
    const auto place = [settings](QWidget *widget, int x, int y, int width, int height) {
        widget->setParent(settings); widget->setGeometry(x, y, width, height);
    };
    auto *nameLabel = new QLabel(tr("&Name:")); auto *nameEdit = new QLineEdit(document->name());
    enableResourceRenaming(nameEdit, ResourceType::Sprite); nameLabel->setBuddy(nameEdit);
    place(nameLabel, 8, 8, 36, 19); place(nameEdit, 48, 8, 97, 21);
    auto *load = new QPushButton(QIcon(QStringLiteral(":/images/open.png")), tr("&Load Sprite"));
    auto *edit = new QPushButton(QIcon(QStringLiteral(":/images/editor/edit.png")), tr("&Edit Sprite"));
    load->setProperty("leftAligned", true);
    edit->setProperty("leftAligned", true);
    place(load, 24, 40, 121, 25); place(edit, 24, 71, 121, 25);
    place(m_dimensions, 24, 111, 143, 16); place(m_count, 24, 129, 143, 16);
    auto *show = new QLabel(tr("Show:"));
    m_previousFrameButton = new QPushButton(QIcon(QStringLiteral(":/images/editor/spriteprevious.png")), QString());
    m_nextFrameButton = new QPushButton(QIcon(QStringLiteral(":/images/editor/spritenext.png")), QString());
    m_previousFrameButton->setToolTip(tr("Show the previous subimage"));
    m_nextFrameButton->setToolTip(tr("Show the next subimage"));
    for (QPushButton *button : {m_previousFrameButton, m_nextFrameButton}) button->setIconSize(QSize(16, 16));
    place(show, 24, 151, 45, 20); place(m_previousFrameButton, 72, 151, 25, 20);
    place(m_frameLabel, 96, 151, 24, 20); m_frameLabel->setAlignment(Qt::AlignCenter);
    place(m_nextFrameButton, 120, 151, 25, 20);
    auto *origin = new QGroupBox(tr("Origin")); place(origin, 8, 213, 153, 69);
    auto *xLabel = new QLabel(tr("X"), origin); xLabel->setGeometry(10, 18, 17, 20);
    auto *yLabel = new QLabel(tr("Y"), origin); yLabel->setGeometry(80, 18, 17, 20);
    m_xOrigin->setParent(origin); m_xOrigin->setGeometry(27, 17, 47, 21);
    m_yOrigin->setParent(origin); m_yOrigin->setGeometry(97, 17, 47, 21);
    for (QSpinBox *spin : {m_xOrigin, m_yOrigin}) {
        spin->setRange(-1000000, 1000000); spin->setButtonSymbols(QAbstractSpinBox::NoButtons); spin->setKeyboardTracking(false);
    }
    auto *center = new QPushButton(tr("&Center"), origin); center->setGeometry(40, 44, 65, 19);
    auto *collision = new QGroupBox(tr("Collision Checking"));
    place(collision, 174, 8, 154, 121);
    m_precise->setParent(collision);
    m_separate->setParent(collision);
    m_precise->ensurePolished();
    m_separate->ensurePolished();
    // The outline font and Qt checkbox spacing differ from the Delphi controls.
    const int checkWidth = qMax(142, qMax(m_precise->sizeHint().width(), m_separate->sizeHint().width()));
    const int columnWidth = checkWidth + 16;
    collision->setFixedWidth(columnWidth);
    settings->setFixedWidth(collision->x() + columnWidth + 6);
    m_precise->setGeometry(8, 20, checkWidth, 18);
    m_separate->setGeometry(8, 43, checkWidth, 18);
    m_modifiedMaskLabel->setParent(collision);
    m_modifiedMaskLabel->setObjectName(QStringLiteral("spriteMaskModified"));
    m_modifiedMaskLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_modifiedMaskLabel->setGeometry(8, 65, columnWidth - 24, 18);
    auto *mask = new QPushButton(tr("&Modify Mask"), collision); mask->setGeometry(16, 86, 121, 25);
    auto *texture = new QGroupBox(tr("Texture Settings")); place(texture, 174, 135, columnWidth, 154);
    m_horizontal->setParent(texture); m_horizontal->setGeometry(8, 19, 137, 18);
    m_vertical->setParent(texture); m_vertical->setGeometry(8, 42, 137, 18);
    m_for3D->setParent(texture); m_for3D->setGeometry(8, 67, 137, 18);
    auto *note = new QLabel(tr("(Must be a power of 2)"), texture); note->setGeometry(23, 84, 126, 18);
    auto *textureLabel = new QLabel(tr("Texture Group:"), texture); textureLabel->setGeometry(8, 107, 130, 18);
    m_textureGroup->setParent(texture); m_textureGroup->setGeometry(8, 126, 137, 21);
    for (int i = 0; i < textureGroups.size(); ++i) m_textureGroup->addItem(textureGroups.at(i), i);
    if (m_textureGroup->findData(document->state().textureGroup) < 0)
        m_textureGroup->addItem(QString::number(document->state().textureGroup), document->state().textureGroup);
    auto *ok = new QPushButton(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("&OK")); place(ok, 41, 297, 81, 25);
    layout->addWidget(settings);
    auto *scroll = new QScrollArea(body); m_preview->setEditable(false); scroll->setWidget(m_preview);
    layout->addWidget(scroll, 1); setCentralWidget(body);
    connect(edit, &QPushButton::clicked, this, &SpritePropertiesWindow::editSprite);
    connect(load, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Load Sprite"), QString(),
            tr("Images (*.png *.bmp *.jpg *.jpeg *.gif *.tif *.tiff);;All Files (*)"));
        if (files.isEmpty()) return;
        QList<SpriteFrame> frames; QString error;
        if (!SpriteDocument::importImages(files, frames, error)) {
            EditorMessageBox::critical(this, tr("Cannot Load Sprite"), error); return;
        }
        SpriteState state = m_document->state();
        state.frames = frames; state.size = frames.first().image.size();
        m_document->edit(state, tr("Load sprite"));
    });
    connect(mask, &QPushButton::clicked, this, [this] {
        if (!m_maskWindow) { m_maskWindow = new SpriteMaskWindow(m_document, this); m_maskWindow->setWindowModality(Qt::WindowModal); }
        m_maskWindow->show(); m_maskWindow->raise(); m_maskWindow->activateWindow();
    });
    connect(ok, &QPushButton::clicked, this, [this] { if (save()) close(); });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &SpritePropertiesWindow::saveProjectRequested);
    auto *undo = new QShortcut(QKeySequence::Undo, this); auto *redo = new QShortcut(QKeySequence::Redo, this);
    connect(undo, &QShortcut::activated, document->undoStack(), &QUndoStack::undo);
    connect(redo, &QShortcut::activated, document->undoStack(), &QUndoStack::redo);
    connect(m_previousFrameButton, &QPushButton::clicked, this, [this] { --m_frameIndex; refresh(); });
    connect(m_nextFrameButton, &QPushButton::clicked, this, [this] { ++m_frameIndex; refresh(); });
    connect(center, &QPushButton::clicked, this, [this] {
        SpriteState state = m_document->state(); const QPoint point(state.size.width() / 2, state.size.height() / 2);
        if (point != state.origin) { state.origin = point; m_document->edit(state, tr("Center origin")); }
    });
    connect(m_preview, &ImageCanvas::positionPicked, this, [this](const QPoint &position) {
        SpriteState state = m_document->state();
        if (state.origin != position) { state.origin = position; m_document->edit(state, tr("Move origin")); }
    });
    for (QCheckBox *check : {m_precise, m_separate, m_horizontal, m_vertical, m_for3D})
        connect(check, &QCheckBox::toggled, this, [this] { applySettings(); });
    for (QSpinBox *spin : {m_xOrigin, m_yOrigin})
        connect(spin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this] { applySettings(); });
    connect(m_textureGroup, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this] { applySettings(); });
    connect(document, &SpriteDocument::changed, this, &SpritePropertiesWindow::refresh);
    connect(document, &SpriteDocument::saved, this, [this] {
        emit resourceSaved(ResourceType::Sprite, m_document->filePath(), m_document->thumbnailPath());
    });
    connect(document->undoStack(), &QUndoStack::cleanChanged, this, [this] { refresh(); });
    refresh();
}
void SpritePropertiesWindow::refresh()
{
    m_refreshing = true;
    const SpriteState &state = m_document->state();
    setWindowTitle(tr("Sprite Properties: %1%2").arg(m_document->name(), m_document->isModified() ? QStringLiteral(" *") : QString()));
    m_dimensions->setText(tr("Width: %1   Height: %2").arg(state.size.width()).arg(state.size.height()));
    m_count->setText(tr("Number of subimages: %1").arg(state.frames.size()));
    const int count = state.frames.size();
    m_frameIndex = count ? qBound(0, m_frameIndex, count - 1) : 0;
    m_frameLabel->setText(QString::number(m_frameIndex));
    m_previousFrameButton->setVisible(m_frameIndex > 0);
    m_nextFrameButton->setVisible(m_frameIndex + 1 < count);
    m_xOrigin->setValue(state.origin.x()); m_yOrigin->setValue(state.origin.y());
    m_precise->setChecked(state.collisionKind == 0); m_separate->setChecked(state.separateMasks);
    m_separate->setEnabled(m_precise->isChecked());
    m_modifiedMaskLabel->setVisible(state.collisionKind > 1 || state.alphaTolerance != 0 || state.boundingBoxMode != 0);
    m_horizontal->setChecked(state.tileHorizontal); m_vertical->setChecked(state.tileVertical); m_for3D->setChecked(state.for3D);
    const auto isPowerOfTwo = [](int value) { return value > 0 && (value & (value - 1)) == 0; };
    m_for3D->setEnabled(isPowerOfTwo(state.size.width()) && isPowerOfTwo(state.size.height()));
    m_horizontal->setEnabled(!state.for3D);
    m_vertical->setEnabled(!state.for3D);
    m_textureGroup->setCurrentIndex(m_textureGroup->findData(state.textureGroup));
    m_preview->setImage(count ? state.frames.at(m_frameIndex).image : QImage());
    m_preview->setCrosshair(state.origin, count > 0);
    m_refreshing = false;
}
void SpritePropertiesWindow::applySettings()
{
    if (m_refreshing) return;
    SpriteState state = m_document->state();
    state.origin = QPoint(m_xOrigin->value(), m_yOrigin->value());
    if (m_precise->isChecked()) state.collisionKind = 0;
    else if (state.collisionKind == 0) state.collisionKind = 1;
    state.separateMasks = m_separate->isChecked(); state.tileHorizontal = m_horizontal->isChecked();
    state.tileVertical = m_vertical->isChecked(); state.for3D = m_for3D->isChecked();
    state.textureGroup = m_textureGroup->currentData().toInt();
    m_document->edit(state, tr("Change sprite properties"));
}
void SpritePropertiesWindow::editSprite()
{
    if (!m_framesWindow) m_framesWindow = new SpriteFramesWindow(m_document, this);
    m_framesWindow->show(); m_framesWindow->raise(); m_framesWindow->activateWindow();
}
QString SpritePropertiesWindow::filePath() const { return m_document->filePath(); }
void SpritePropertiesWindow::relocate(const QString &oldDirectory, const QString &newDirectory)
{ m_document->relocate(oldDirectory, newDirectory); }

bool SpritePropertiesWindow::save()
{
    m_xOrigin->interpretText();
    m_yOrigin->interpretText();
    QString error;
    if (m_document->save(error)) return true;
    EditorMessageBox::critical(this, tr("Cannot Save Sprite"), error); return false;
}
void SpritePropertiesWindow::closeEvent(QCloseEvent *event)
{
    if (m_framesWindow && !m_framesWindow->close()) { event->ignore(); return; }
    if (m_maskWindow) m_maskWindow->close();
    if (m_document->isModified()) {
        const auto answer = EditorMessageBox::question(this, tr("Sprite Properties"), tr("Save changes to %1?").arg(m_document->name()),
            EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel || (answer == EditorMessageBox::Save && !save())) { event->ignore(); return; }
    }
    event->accept();
}

void SpritePropertiesWindow::setTextureGroups(const QStringList &groups)
{
    const QSignalBlocker blocker(m_textureGroup); const int group = m_textureGroup->currentData().toInt();
    m_textureGroup->clear();
    for (int i = 0; i < groups.size(); ++i) m_textureGroup->addItem(groups.at(i), i);
    if (m_textureGroup->findData(group) < 0) m_textureGroup->addItem(QString::number(group), group);
    m_textureGroup->setCurrentIndex(m_textureGroup->findData(group));
}
