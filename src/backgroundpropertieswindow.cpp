#include <QSignalBlocker>
#include "editordialog.h"
#include "editorstandarddialogs.h"
#include "backgroundpropertieswindow.h"
#include "backgrounddocument.h"
#include "imagecanvas.h"
#include "imageeditorwindow.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QVBoxLayout>

// Tile guides belong to the background preview, not to the shared paint tools.
class BackgroundPreview : public ImageCanvas
{
public:
    void setBackgroundState(const BackgroundState &state) { m_state = state; setImage(state.image); update(); }
protected:
    void paintEvent(QPaintEvent *event) override
    {
        ImageCanvas::paintEvent(event);
        if (!m_state.isTileSet || image().isNull()) return;
        const int stepX = m_state.tileWidth + m_state.horizontalSeparation;
        const int stepY = m_state.tileHeight + m_state.verticalSeparation;
        // Avoid a solid mass of guides when tiles are only one or two pixels apart.
        if (stepX < 3 || stepY < 3) return;
        const QRect visible = event->rect().intersected(image().rect());
        const int firstX = m_state.horizontalOffset + qMax(0, (visible.left() - m_state.horizontalOffset) / stepX) * stepX;
        const int firstY = m_state.verticalOffset + qMax(0, (visible.top() - m_state.verticalOffset) / stepY) * stepY;
        QPainter painter(this);
        painter.setClipRect(image().rect());
        painter.setPen(QPen(QColor(150, 150, 150), 1, Qt::DotLine));
        for (int y = firstY; y <= visible.bottom(); y += stepY)
            for (int x = firstX; x <= visible.right(); x += stepX)
                painter.drawRect(x, y, m_state.tileWidth - 1, m_state.tileHeight - 1);
    }
private:
    BackgroundState m_state;
};

BackgroundPropertiesWindow::BackgroundPropertiesWindow(BackgroundDocument *document, const QStringList &textureGroups, QWidget *parent)
    : ResourceEditorWindow(parent), m_document(document), m_preview(new BackgroundPreview), m_dimensions(new QLabel),
      m_tileSetCheck(new QCheckBox(tr("&Use as tile set"))), m_horizontalCheck(new QCheckBox(tr("Tile: Horizontal"))),
      m_verticalCheck(new QCheckBox(tr("Tile: Vertical"))), m_for3DCheck(new QCheckBox(tr("Used for 3D"))),
      m_textureGroup(new QComboBox), m_tileProperties(new QGroupBox(tr("Tile Properties")))
{
    document->setParent(this);
    setAttribute(Qt::WA_DeleteOnClose);
    editorMenuBar()->hide();
    resize(503, 375);
    setMinimumSize(503, 375);
    auto *body = new QWidget(this);
    auto *layout = new QHBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(3);
    auto *settings = new QWidget(body); settings->setMinimumHeight(337);
    const auto place = [settings](QWidget *widget, int x, int y, int width, int height) {
        widget->setParent(settings); widget->setGeometry(x, y, width, height);
    };
    auto *nameLabel = new QLabel(tr("&Name:")); auto *name = new QLineEdit(document->name());
    enableResourceRenaming(name, ResourceType::Background); nameLabel->setBuddy(name);
    place(nameLabel, 8, 8, 36, 19); place(name, 48, 8, 89, 21);
    auto *load = new QPushButton(QIcon(QStringLiteral(":/images/open.png")), tr("&Load Background"));
    auto *edit = new QPushButton(QIcon(QStringLiteral(":/images/editor/edit.png")), tr("&Edit Background"));
    load->setProperty("leftAligned", true); edit->setProperty("leftAligned", true);
    place(load, 16, 40, 121, 25); place(edit, 16, 73, 121, 25);
    place(m_dimensions, 16, 115, 132, 16); place(m_tileSetCheck, 16, 134, 132, 18);
    auto *texture = new QGroupBox(tr("Texture Settings")); place(texture, 4, 157, 142, 139);
    auto *textureLayout = new QVBoxLayout(texture);
    textureLayout->setContentsMargins(8, 14, 8, 6); textureLayout->setSpacing(2);
    for (QCheckBox *check : {m_horizontalCheck, m_verticalCheck, m_for3DCheck}) textureLayout->addWidget(check);
    textureLayout->addWidget(new QLabel(tr("(Must be a power of 2)"), texture));
    textureLayout->addWidget(new QLabel(tr("Texture Group:"), texture));
    textureLayout->addWidget(m_textureGroup);
    for (int i = 0; i < textureGroups.size(); ++i) m_textureGroup->addItem(textureGroups.at(i), i);
    if (m_textureGroup->findData(document->state().textureGroup) < 0)
        m_textureGroup->addItem(QString::number(document->state().textureGroup), document->state().textureGroup);
    place(m_tileProperties, 152, 8, 145, 233);
    auto *tiles = new QFormLayout(m_tileProperties);
    tiles->setContentsMargins(8, 18, 8, 12); tiles->setSpacing(8);
    const QStringList labels = {tr("Tile Width:"), tr("Tile Height:"), tr("Horizontal Offset:"), tr("Vertical Offset:"),
                               tr("Horizontal Sep:"), tr("Vertical Sep:")};
    for (int i = 0; i < labels.size(); ++i) {
        auto *spin = new QSpinBox(m_tileProperties);
        spin->setRange(i < 2 ? 1 : 0, 32000); spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setKeyboardTracking(false); spin->setMinimumWidth(42);
        tiles->addRow(labels.at(i), spin); m_tileValues.append(spin);
        connect(spin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this] { applySettings(); });
    }
    m_tileProperties->ensurePolished();
    m_tileProperties->setFixedWidth(qMax(145, m_tileProperties->sizeHint().width()));
    auto *ok = new QPushButton(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("&OK")); place(ok, 43, 302, 75, 25);
    layout->addWidget(settings);
    auto *scroll = new QScrollArea(body); m_preview->setEditable(false); scroll->setWidget(m_preview);
    layout->addWidget(scroll, 1); setCentralWidget(body);
    connect(load, &QPushButton::clicked, this, &BackgroundPropertiesWindow::loadImage);
    connect(edit, &QPushButton::clicked, this, &BackgroundPropertiesWindow::editImage);
    connect(ok, &QPushButton::clicked, this, [this] { if (save()) close(); });
    for (QCheckBox *check : {m_tileSetCheck, m_horizontalCheck, m_verticalCheck, m_for3DCheck})
        connect(check, &QCheckBox::toggled, this, [this] { applySettings(); });
    connect(m_textureGroup, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this] { applySettings(); });
    connect(document, &BackgroundDocument::changed, this, &BackgroundPropertiesWindow::refresh);
    connect(document->undoStack(), &QUndoStack::cleanChanged, this, [this] { refresh(); });
    connect(document, &BackgroundDocument::saved, this, [this] {
        emit resourceSaved(ResourceType::Background, m_document->filePath(), m_document->thumbnailPath());
    });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &BackgroundPropertiesWindow::saveProjectRequested);
    auto *undo = new QShortcut(QKeySequence::Undo, this); auto *redo = new QShortcut(QKeySequence::Redo, this);
    connect(undo, &QShortcut::activated, document->undoStack(), &QUndoStack::undo);
    connect(redo, &QShortcut::activated, document->undoStack(), &QUndoStack::redo);
    refresh();
}

QString BackgroundPropertiesWindow::filePath() const { return m_document->filePath(); }
void BackgroundPropertiesWindow::relocate(const QString &oldDirectory, const QString &newDirectory)
{ m_document->relocate(oldDirectory, newDirectory); }

void BackgroundPropertiesWindow::refresh()
{
    m_refreshing = true;
    const BackgroundState &state = m_document->state();
    setWindowTitle(tr("Background Properties: %1%2").arg(m_document->name(), m_document->isModified() ? QStringLiteral(" *") : QString()));
    m_dimensions->setText(tr("Width: %1   Height: %2").arg(state.image.width()).arg(state.image.height()));
    m_tileSetCheck->setChecked(state.isTileSet);
    m_tileProperties->setVisible(state.isTileSet);
    m_tileProperties->parentWidget()->setFixedWidth(state.isTileSet
        ? m_tileProperties->geometry().right() + 6 : m_tileProperties->x());
    m_horizontalCheck->setChecked(state.tileHorizontal); m_verticalCheck->setChecked(state.tileVertical); m_for3DCheck->setChecked(state.for3D);
    const auto isPowerOfTwo = [](int value) { return value > 0 && (value & (value - 1)) == 0; };
    m_for3DCheck->setEnabled(isPowerOfTwo(state.image.width()) && isPowerOfTwo(state.image.height()));
    m_horizontalCheck->setEnabled(!state.for3D);
    m_verticalCheck->setEnabled(!state.for3D);
    m_textureGroup->setCurrentIndex(m_textureGroup->findData(state.textureGroup));
    const QList<int> values = {state.tileWidth, state.tileHeight, state.horizontalOffset, state.verticalOffset, state.horizontalSeparation, state.verticalSeparation};
    for (int i = 0; i < values.size(); ++i) m_tileValues.at(i)->setValue(values.at(i));
    m_preview->setBackgroundState(state);
    m_refreshing = false;
}

void BackgroundPropertiesWindow::applySettings()
{
    if (m_refreshing) return;
    BackgroundState state = m_document->state();
    state.isTileSet = m_tileSetCheck->isChecked();
    state.tileHorizontal = m_horizontalCheck->isChecked(); state.tileVertical = m_verticalCheck->isChecked(); state.for3D = m_for3DCheck->isChecked();
    state.textureGroup = m_textureGroup->currentData().toInt();
    state.tileWidth = m_tileValues.at(0)->value(); state.tileHeight = m_tileValues.at(1)->value();
    state.horizontalOffset = m_tileValues.at(2)->value(); state.verticalOffset = m_tileValues.at(3)->value();
    state.horizontalSeparation = m_tileValues.at(4)->value(); state.verticalSeparation = m_tileValues.at(5)->value();
    m_document->edit(state, tr("Change background properties"));
}

void BackgroundPropertiesWindow::loadImage()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Background"), QString(),
        tr("Images (*.png *.bmp *.jpg *.jpeg *.gif *.tif *.tiff);;All Files (*)"));
    if (path.isEmpty()) return;
    QImageReader reader(path);
    const QImage image = reader.read();
    if (image.isNull()) { EditorMessageBox::critical(this, tr("Cannot Load Background"), reader.errorString()); return; }
    m_document->replaceImage(image);
}

void BackgroundPropertiesWindow::editImage()
{
    if (m_imageEditor) { m_imageEditor->raise(); m_imageEditor->activateWindow(); return; }
    QImage image = m_document->state().image;
    if (image.isNull()) {
        EditorDialog dialog(this); dialog.setWindowTitle(tr("New Background Image"));
        auto *layout = new QFormLayout(dialog.bodyWidget());
        auto *width = new QSpinBox(&dialog); auto *height = new QSpinBox(&dialog);
        for (QSpinBox *spin : {width, height}) { spin->setRange(1, 4096); spin->setValue(32); }
        layout->addRow(tr("Width:"), width); layout->addRow(tr("Height:"), height);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        image = QImage(width->value(), height->value(), QImage::Format_ARGB32);
        if (image.isNull()) { EditorMessageBox::critical(this, tr("Cannot Create Image"), tr("Not enough memory for this image.")); return; }
        image.fill(Qt::transparent);
    }
    m_imageEditor = new ImageEditorWindow(image, this);
    m_imageEditor->setFrames({image}, 0, m_document->name());
    m_imageEditor->setWindowModality(Qt::WindowModal);
    connect(m_imageEditor, &ImageEditorWindow::imageAccepted, m_document, &BackgroundDocument::replaceImage);
    m_imageEditor->show();
}

bool BackgroundPropertiesWindow::save()
{
    for (QSpinBox *spin : m_tileValues) spin->interpretText();
    QString error;
    if (m_document->save(error)) return true;
    EditorMessageBox::critical(this, tr("Cannot Save Background"), error); return false;
}

void BackgroundPropertiesWindow::closeEvent(QCloseEvent *event)
{
    if (m_imageEditor && !m_imageEditor->close()) { event->ignore(); return; }
    if (m_document->isModified()) {
        const auto answer = EditorMessageBox::question(this, tr("Background Properties"), tr("Save changes to %1?").arg(m_document->name()),
            EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel || (answer == EditorMessageBox::Save && !save())) { event->ignore(); return; }
    }
    event->accept();
}

void BackgroundPropertiesWindow::setTextureGroups(const QStringList &groups)
{
    const QSignalBlocker blocker(m_textureGroup); const int group = m_textureGroup->currentData().toInt();
    m_textureGroup->clear();
    for (int i = 0; i < groups.size(); ++i) m_textureGroup->addItem(groups.at(i), i);
    if (m_textureGroup->findData(group) < 0) m_textureGroup->addItem(QString::number(group), group);
    m_textureGroup->setCurrentIndex(m_textureGroup->findData(group));
}
