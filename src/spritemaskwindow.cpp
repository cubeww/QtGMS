#include "spritemaskwindow.h"
#include "spritedocument.h"
#include "imagecanvas.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>

SpriteMaskWindow::SpriteMaskWindow(SpriteDocument *document, QWidget *parent)
    : EditorWindow(parent), m_document(document), m_preview(new ImageCanvas),
      m_showMask(new QCheckBox(tr("Show collision mask"))), m_separate(new QCheckBox(tr("Separate collision masks"))),
      m_shape(new QComboBox), m_boxMode(new QComboBox), m_tolerance(new QSpinBox),
      m_left(new QSpinBox), m_right(new QSpinBox), m_top(new QSpinBox), m_bottom(new QSpinBox),
      m_frameLabel(new QLabel)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Sprite Mask: %1").arg(document->name()));
    editorMenuBar()->hide();
    resize(605, 365); setMinimumSize(520, 365);
    auto *body = new QWidget(this);
    auto *layout = new QHBoxLayout(body);
    layout->setContentsMargins(4, 4, 4, 4);
    auto *settings = new QWidget(body); settings->setFixedWidth(324);
    auto *grid = new QGridLayout(settings); grid->setContentsMargins(4, 4, 4, 4);
    auto *imageGroup = new QGroupBox(tr("Image"));
    auto *imageLayout = new QVBoxLayout(imageGroup);
    auto *navigation = new QHBoxLayout;
    auto *previous = new QPushButton(QStringLiteral("<")); auto *next = new QPushButton(QStringLiteral(">"));
    previous->setFixedWidth(25); next->setFixedWidth(25);
    navigation->addWidget(previous); navigation->addWidget(m_frameLabel); navigation->addWidget(next);
    imageLayout->addLayout(navigation);
    m_showMask->setChecked(true); imageLayout->addWidget(m_showMask);
    auto *zoom = new QHBoxLayout;
    for (const QString &text : {QStringLiteral("-"), QStringLiteral("1:1"), QStringLiteral("+")}) {
        auto *button = new QPushButton(text); zoom->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, text] {
            m_preview->setZoom(text == QStringLiteral("1:1") ? 1 : m_preview->zoom() * (text == QStringLiteral("+") ? 2 : 0.5));
        });
    }
    imageLayout->addLayout(zoom); grid->addWidget(imageGroup, 0, 0);
    auto *box = new QGroupBox(tr("Bounding Box")); auto *boxLayout = new QFormLayout(box);
    m_boxMode->addItems({tr("Automatic"), tr("Full Image"), tr("Manual")});
    boxLayout->addRow(m_boxMode);
    boxLayout->addRow(tr("Left:"), m_left); boxLayout->addRow(tr("Right:"), m_right);
    boxLayout->addRow(tr("Top:"), m_top); boxLayout->addRow(tr("Bottom:"), m_bottom);
    grid->addWidget(box, 0, 1);
    auto *general = new QGroupBox(tr("General")); auto *generalLayout = new QVBoxLayout(general);
    generalLayout->addWidget(m_separate); generalLayout->addWidget(new QLabel(tr("Alpha Tolerance:")));
    m_tolerance->setRange(0, 255); generalLayout->addWidget(m_tolerance); grid->addWidget(general, 1, 0);
    auto *shape = new QGroupBox(tr("Shape")); auto *shapeLayout = new QVBoxLayout(shape);
    m_shape->addItems({tr("Precise"), tr("Rectangle"), tr("Ellipse"), tr("Diamond")});
    for (int i = 0; i < 4; ++i) m_shape->setItemData(i, i);
    m_shape->addItem(tr("Rotated Rectangle"), 5);
    shapeLayout->addWidget(m_shape); shapeLayout->addStretch(); grid->addWidget(shape, 1, 1);
    auto *ok = new QPushButton(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("&OK"));
    grid->addWidget(ok, 2, 0, 1, 2, Qt::AlignCenter);
    connect(ok, &QPushButton::clicked, this, &QWidget::close);
    layout->addWidget(settings);
    auto *scroll = new QScrollArea(body); scroll->setWidget(m_preview); m_preview->setEditable(false);
    layout->addWidget(scroll, 1); setCentralWidget(body);
    for (QSpinBox *spin : {m_left, m_right, m_top, m_bottom, m_tolerance}) {
        spin->setKeyboardTracking(false);
        connect(spin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this] { applySettings(); });
    }
    for (QComboBox *combo : {m_shape, m_boxMode})
        connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this] { applySettings(); });
    connect(m_separate, &QCheckBox::toggled, this, [this] { applySettings(); });
    connect(m_showMask, &QCheckBox::toggled, this, [this] { refresh(); });
    connect(previous, &QPushButton::clicked, this, [this] { --m_frameIndex; refresh(); });
    connect(next, &QPushButton::clicked, this, [this] { ++m_frameIndex; refresh(); });
    connect(document, &SpriteDocument::changed, this, [this] { refresh(); });
    refresh();
}
void SpriteMaskWindow::refresh()
{
    m_refreshing = true;
    const SpriteState &state = m_document->state();
    const int count = state.frames.size();
    m_frameIndex = count ? (m_frameIndex % count + count) % count : 0;
    m_frameLabel->setText(QStringLiteral("%1 / %2").arg(count ? m_frameIndex + 1 : 0).arg(count));
    m_shape->setCurrentIndex(m_shape->findData(state.collisionKind)); m_boxMode->setCurrentIndex(state.boundingBoxMode);
    m_separate->setChecked(state.separateMasks); m_tolerance->setValue(state.alphaTolerance);
    for (QSpinBox *spin : {m_left, m_right}) spin->setRange(0, qMax(0, state.size.width() - 1));
    for (QSpinBox *spin : {m_top, m_bottom}) spin->setRange(0, qMax(0, state.size.height() - 1));
    const QRect box = m_document->boundingBox(m_frameIndex);
    m_left->setValue(box.left()); m_right->setValue(qMax(0, box.right()));
    m_top->setValue(box.top()); m_bottom->setValue(qMax(0, box.bottom()));
    for (QSpinBox *spin : {m_left, m_right, m_top, m_bottom}) spin->setEnabled(state.boundingBoxMode == 2);
    m_preview->setImage(count ? state.frames.at(m_frameIndex).image : QImage());
    m_preview->setOverlay(m_showMask->isChecked() ? m_document->collisionMask(m_frameIndex) : QImage());
    m_refreshing = false;
}
void SpriteMaskWindow::applySettings()
{
    if (m_refreshing) return;
    SpriteState state = m_document->state();
    if (m_shape->currentIndex() >= 0) state.collisionKind = m_shape->currentData().toInt();
    state.boundingBoxMode = m_boxMode->currentIndex();
    state.separateMasks = m_separate->isChecked(); state.alphaTolerance = m_tolerance->value();
    state.boundingBox = QRect(QPoint(qMin(m_left->value(), m_right->value()), qMin(m_top->value(), m_bottom->value())),
        QPoint(qMax(m_left->value(), m_right->value()), qMax(m_top->value(), m_bottom->value())));
    m_document->edit(state, tr("Change collision mask"));
}
