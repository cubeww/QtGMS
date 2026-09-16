#include "spritemaskwindow.h"
#include "spritedocument.h"
#include "imagecanvas.h"

#include <QCheckBox>
#include <QButtonGroup>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QRubberBand>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtMath>

SpriteMaskWindow::SpriteMaskWindow(SpriteDocument *document, QWidget *parent)
    : EditorWindow(parent), m_document(document), m_preview(new ImageCanvas),
      m_boxOutline(new QRubberBand(QRubberBand::Rectangle, m_preview)),
      m_showMask(new QCheckBox(tr("Show collision mask"))), m_separate(new QCheckBox(tr("Separate collision masks"))),
      m_shape(new QButtonGroup(this)), m_boxMode(new QButtonGroup(this)), m_tolerance(new QSpinBox),
      m_toleranceSlider(new QSlider(Qt::Horizontal)),
      m_left(new QSpinBox), m_right(new QSpinBox), m_top(new QSpinBox), m_bottom(new QSpinBox),
      m_frameLabel(new QLabel), m_dimensionsLabel(new QLabel), m_countLabel(new QLabel),
      m_frameNavigation(new QWidget)
{
    setAttribute(Qt::WA_DeleteOnClose);
    editorMenuBar()->hide();
    auto *body = new QWidget(this);
    auto *layout = new QHBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(3);
    auto *settings = new QWidget(body);
    auto *grid = new QGridLayout(settings); grid->setContentsMargins(10, 10, 8, 16);
    grid->setHorizontalSpacing(10); grid->setVerticalSpacing(10);
    grid->setColumnStretch(0, 1); grid->setColumnStretch(1, 1);
    auto *imageGroup = new QGroupBox(tr("Image"));
    auto *imageLayout = new QVBoxLayout(imageGroup);
    imageLayout->addWidget(m_dimensionsLabel); imageLayout->addWidget(m_countLabel);
    auto *navigation = new QHBoxLayout(m_frameNavigation); navigation->setContentsMargins(0, 0, 0, 0);
    auto *previous = new QPushButton(QIcon(QStringLiteral(":/images/editor/spriteprevious.png")), QString());
    auto *next = new QPushButton(QIcon(QStringLiteral(":/images/editor/spritenext.png")), QString());
    previous->setFixedSize(23, 23); next->setFixedSize(23, 23);
    m_frameLabel->setAlignment(Qt::AlignCenter);
    navigation->addWidget(previous); navigation->addWidget(m_frameLabel); navigation->addWidget(next); navigation->addStretch();
    imageLayout->addWidget(m_frameNavigation); imageLayout->addStretch();
    m_showMask->setChecked(true); imageLayout->addWidget(m_showMask);
    auto *zoom = new QHBoxLayout;
    const QStringList zoomIcons = {QStringLiteral("zoomout"), QStringLiteral("zoomreset"), QStringLiteral("zoomin")};
    const QStringList zoomTips = {tr("Zoom Out"), tr("Actual Size"), tr("Zoom In")};
    for (int index = 0; index < zoomIcons.size(); ++index) {
        auto *button = new QPushButton(QIcon(QStringLiteral(":/images/room/%1.png").arg(zoomIcons.at(index))), QString());
        button->setFixedSize(23, 23); button->setIconSize(QSize(16, 16)); button->setToolTip(zoomTips.at(index));
        zoom->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, index] {
            m_preview->setZoom(index == 1 ? 1 : m_preview->zoom() * (index == 2 ? 2 : 0.5));
        });
    }
    zoom->addStretch(); imageLayout->addLayout(zoom); grid->addWidget(imageGroup, 0, 0);
    auto *box = new QGroupBox(tr("Bounding Box")); auto *boxLayout = new QVBoxLayout(box);
    const QStringList boxModes = {tr("Automatic"), tr("Full Image"), tr("Manual")};
    for (int index = 0; index < boxModes.size(); ++index) {
        auto *button = new QRadioButton(boxModes.at(index)); m_boxMode->addButton(button, index); boxLayout->addWidget(button);
    }
    boxLayout->addStretch();
    auto *coordinates = new QGridLayout;
    const QStringList coordinateLabels = {tr("Left:"), tr("Right:"), tr("Top:"), tr("Bottom:")};
    const QList<QSpinBox *> coordinateEdits = {m_left, m_right, m_top, m_bottom};
    for (int index = 0; index < coordinateEdits.size(); ++index) {
        auto *label = new QLabel(coordinateLabels.at(index)); label->setBuddy(coordinateEdits.at(index));
        coordinates->addWidget(label, index / 2, (index % 2) * 2);
        coordinates->addWidget(coordinateEdits.at(index), index / 2, (index % 2) * 2 + 1);
        coordinateEdits.at(index)->setMinimumWidth(56);
        coordinateEdits.at(index)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    boxLayout->addLayout(coordinates);
    grid->addWidget(box, 0, 1);
    auto *general = new QGroupBox(tr("General")); auto *generalLayout = new QVBoxLayout(general);
    generalLayout->addWidget(m_separate); generalLayout->addWidget(new QLabel(tr("Alpha Tolerance:")));
    m_tolerance->setRange(0, 255); m_tolerance->setFixedWidth(56);
    m_toleranceSlider->setRange(0, 255);
    auto *toleranceLayout = new QHBoxLayout;
    toleranceLayout->addWidget(m_toleranceSlider, 1); toleranceLayout->addWidget(m_tolerance);
    generalLayout->addLayout(toleranceLayout); generalLayout->addStretch(); grid->addWidget(general, 1, 0);
    auto *shape = new QGroupBox(tr("Shape")); auto *shapeLayout = new QVBoxLayout(shape);
    const QStringList shapes = {tr("Precise"), tr("Rectangle"), tr("Ellipse"), tr("Diamond")};
    for (int index = 0; index < shapes.size(); ++index) {
        auto *button = new QRadioButton(shapes.at(index)); m_shape->addButton(button, index); shapeLayout->addWidget(button);
    }
    shapeLayout->addStretch(); grid->addWidget(shape, 1, 1);
    for (QVBoxLayout *groupLayout : {imageLayout, boxLayout, generalLayout, shapeLayout}) {
        groupLayout->setContentsMargins(10, 18, 10, 12); groupLayout->setSpacing(6);
    }
    imageGroup->setMinimumHeight(180); general->setMinimumHeight(140);
    auto *ok = new QPushButton(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("&OK"));
    ok->setFixedSize(80, 24); grid->addWidget(ok, 2, 0, 1, 2, Qt::AlignCenter);
    grid->setRowMinimumHeight(2, 36); grid->setRowStretch(3, 1);
    connect(ok, &QPushButton::clicked, this, &QWidget::close);
    layout->addWidget(settings);
    auto *scroll = new QScrollArea(body); scroll->setWidget(m_preview); m_preview->setEditable(false);
    m_boxOutline->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_preview->installEventFilter(this);
    connect(m_preview, &ImageCanvas::zoomChanged, this, &SpriteMaskWindow::updateBoxOutline);
    layout->addWidget(scroll, 1); setCentralWidget(body);
    for (QSpinBox *spin : {m_left, m_right, m_top, m_bottom, m_tolerance}) {
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setKeyboardTracking(false);
        connect(spin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this] { applySettings(); });
    }
    for (QButtonGroup *group : {m_shape, m_boxMode})
        connect(group, static_cast<void (QButtonGroup::*)(int)>(&QButtonGroup::buttonClicked), this, [this] { applySettings(); });
    connect(m_toleranceSlider, &QSlider::valueChanged, m_tolerance, &QSpinBox::setValue);
    connect(m_separate, &QCheckBox::toggled, this, [this] { applySettings(); });
    connect(m_showMask, &QCheckBox::toggled, this, [this] { refresh(); });
    connect(previous, &QPushButton::clicked, this, [this] { --m_frameIndex; refresh(); });
    connect(next, &QPushButton::clicked, this, [this] { ++m_frameIndex; refresh(); });
    connect(document, &SpriteDocument::changed, this, [this] { refresh(); });
    refresh();
    ensurePolished();
    settings->setFixedWidth(qMax(420, settings->sizeHint().width()));
    setMinimumSize(QSize(settings->width() + 260, 460).expandedTo(minimumSizeHint()));
    resize(QSize(760, 460).expandedTo(minimumSize()));
}
void SpriteMaskWindow::refresh()
{
    m_selectingBox = false;
    m_refreshing = true;
    const SpriteState &state = m_document->state();
    setWindowTitle(tr("Mask Properties: %1").arg(m_document->name()));
    const int count = state.frames.size();
    m_frameIndex = count ? (m_frameIndex % count + count) % count : 0;
    m_frameLabel->setText(QStringLiteral("%1 / %2").arg(count ? m_frameIndex + 1 : 0).arg(count));
    m_frameNavigation->setVisible(count > 1);
    m_dimensionsLabel->setText(tr("Width: %1   Height: %2").arg(state.size.width()).arg(state.size.height()));
    m_countLabel->setText(tr("Number of subimages: %1").arg(count));
    if (QAbstractButton *button = m_shape->button(state.collisionKind)) button->setChecked(true);
    if (QAbstractButton *button = m_boxMode->button(state.boundingBoxMode)) button->setChecked(true);
    m_separate->setChecked(state.separateMasks); m_tolerance->setValue(state.alphaTolerance);
    m_separate->setEnabled(state.collisionKind == 0);
    m_toleranceSlider->setValue(state.alphaTolerance);
    for (QSpinBox *spin : {m_left, m_right}) spin->setRange(0, qMax(0, state.size.width() - 1));
    for (QSpinBox *spin : {m_top, m_bottom}) spin->setRange(0, qMax(0, state.size.height() - 1));
    const QRect box = m_document->boundingBox(m_frameIndex);
    m_left->setValue(box.left()); m_right->setValue(qMax(0, box.right()));
    m_top->setValue(box.top()); m_bottom->setValue(qMax(0, box.bottom()));
    for (QSpinBox *spin : {m_left, m_right, m_top, m_bottom}) spin->setEnabled(state.boundingBoxMode == 2);
    m_preview->setImage(count ? state.frames.at(m_frameIndex).image : QImage());
    m_preview->setOverlay(m_showMask->isChecked() ? m_document->collisionMask(m_frameIndex) : QImage());
    updateBoxOutline();
    m_refreshing = false;
}
void SpriteMaskWindow::updateBoxOutline()
{
    const QRect box = m_selectingBox ? m_selectedBox : m_document->boundingBox(m_frameIndex);
    const bool visible = m_document->state().boundingBoxMode == 2 && !m_preview->image().isNull() && !box.isEmpty();
    m_boxOutline->setVisible(visible);
    if (!visible) return;
    const qreal zoom = m_preview->zoom();
    m_boxOutline->setGeometry(QRectF(box.x() * zoom, box.y() * zoom,
        box.width() * zoom, box.height() * zoom).toAlignedRect());
    m_boxOutline->raise();
}
void SpriteMaskWindow::updateBoxSelection(const QPoint &position)
{
    const QImage &image = m_preview->image();
    const QPoint point(qBound(0, qFloor(position.x() / m_preview->zoom()), image.width() - 1),
        qBound(0, qFloor(position.y() / m_preview->zoom()), image.height() - 1));
    m_selectedBox = QRect(QPoint(qMin(m_boxStart.x(), point.x()), qMin(m_boxStart.y(), point.y())),
        QPoint(qMax(m_boxStart.x(), point.x()), qMax(m_boxStart.y(), point.y())));
    m_refreshing = true;
    m_left->setValue(m_selectedBox.left()); m_right->setValue(m_selectedBox.right());
    m_top->setValue(m_selectedBox.top()); m_bottom->setValue(m_selectedBox.bottom());
    m_refreshing = false;
    updateBoxOutline();
}
bool SpriteMaskWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_preview) return EditorWindow::eventFilter(watched, event);
    if (m_selectingBox && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        refresh();
        return true;
    }
    if (m_document->state().boundingBoxMode != 2 || m_preview->image().isNull())
        return EditorWindow::eventFilter(watched, event);
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            m_preview->setFocus();
            m_boxStart = QPoint(qFloor(mouse->pos().x() / m_preview->zoom()), qFloor(mouse->pos().y() / m_preview->zoom()));
            if (!m_preview->image().rect().contains(m_boxStart)) return true;
            m_selectingBox = true;
            updateBoxSelection(mouse->pos());
            return true;
        }
    } else if (m_selectingBox && event->type() == QEvent::MouseMove) {
        updateBoxSelection(static_cast<QMouseEvent *>(event)->pos());
        return true;
    } else if (m_selectingBox && event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            updateBoxSelection(mouse->pos());
            m_selectingBox = false;
            SpriteState state = m_document->state();
            state.boundingBox = m_selectedBox;
            m_document->edit(state, tr("Change collision mask"));
            updateBoxOutline();
            return true;
        }
    }
    return EditorWindow::eventFilter(watched, event);
}
void SpriteMaskWindow::applySettings()
{
    if (m_refreshing) return;
    SpriteState state = m_document->state();
    if (m_shape->checkedId() >= 0) state.collisionKind = m_shape->checkedId();
    state.boundingBoxMode = m_boxMode->checkedId();
    state.separateMasks = m_separate->isChecked(); state.alphaTolerance = m_tolerance->value();
    state.boundingBox = QRect(QPoint(qMin(m_left->value(), m_right->value()), qMin(m_top->value(), m_bottom->value())),
        QPoint(qMax(m_left->value(), m_right->value()), qMax(m_top->value(), m_bottom->value())));
    m_document->edit(state, tr("Change collision mask"));
}
