#include "resourceselector.h"
#include "roompropertieswindow.h"
#include "roomdocument.h"
#include "roomcanvas.h"
#include "roomoverview.h"
#include "actionxml.h"
#include "editordialog.h"
#include "editorstandarddialogs.h"
#include "codesnippeteditorwindow.h"
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSignalBlocker>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>

class RoomObjectPreview : public QLabel
{
public:
    RoomObjectPreview() : m_checker(16, 16)
    {
        m_checker.fill(QColor(192, 192, 192));
        QPainter painter(&m_checker);
        painter.fillRect(0, 0, 8, 8, QColor(128, 128, 128));
        painter.fillRect(8, 8, 8, 8, QColor(128, 128, 128));
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        const QPixmap *image = pixmap();
        if (!image || image->isNull()) return;
        // Paint both layers on the image widget itself: the scroll area's
        // palette background can be replaced by the application stylesheet.
        QPainter painter(this);
        painter.drawTiledPixmap(image->rect(), m_checker);
        painter.drawPixmap(0, 0, *image);
    }
private:
    QPixmap m_checker;
};

class RoomTilePicker : public QWidget
{
public:
    explicit RoomTilePicker(QWidget *parent = nullptr) : QWidget(parent) { setMinimumHeight(150); setMaximumHeight(200); }
    RoomVisual visual;
    QRect selected;
    std::function<void(const QRect &)> changed;
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this); painter.fillRect(rect(), QColor(56, 56, 56)); painter.setPen(QColor(165, 165, 155)); painter.drawRect(rect().adjusted(0, 0, -1, -1));
        if (visual.image.isNull()) return;
        const qreal zoom = imageScale(); painter.scale(zoom, zoom); painter.drawPixmap(0, 0, visual.image);
        QPen pen(QColor(0, 180, 255)); pen.setCosmetic(true); painter.setPen(pen); painter.setBrush(Qt::NoBrush); painter.drawRect(selected);
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || visual.image.isNull()) return;
        m_anchor = selected.topLeft();
        selectAt(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if ((event->buttons() & Qt::LeftButton) && !visual.image.isNull()) selectAt(event);
    }
private:
    void selectAt(QMouseEvent *event)
    {
        const QPoint point = (QPointF(event->pos()) / imageScale()).toPoint(); const QPoint step = QPoint(visual.tileSize.width(), visual.tileSize.height()) + visual.tileSeparation;
        const int x = visual.tileOffset.x() + int(std::floor(double(point.x() - visual.tileOffset.x()) / qMax(1, step.x()))) * step.x();
        const int y = visual.tileOffset.y() + int(std::floor(double(point.y() - visual.tileOffset.y()) / qMax(1, step.y()))) * step.y();
        QRect tile(QPoint(x, y), visual.tileSize);
        if (event->modifiers() & Qt::ControlModifier) {
            const QPoint end = event->modifiers() & Qt::AltModifier ? point : tile.bottomRight();
            tile = QRect(m_anchor, end).normalized().intersected(visual.image.rect());
        }
        if (!tile.isEmpty() && visual.image.rect().contains(tile)) { selected = tile; if (changed) changed(tile); update(); }
    }
    QPoint m_anchor;
    qreal imageScale() const { return qMin(1.0, qMin(double(width()) / qMax(1, visual.image.width()), double(height()) / qMax(1, visual.image.height()))); }
};
class RoomNumberEdit : public QDoubleSpinBox
{
public:
    explicit RoomNumberEdit(QWidget *parent) : QDoubleSpinBox(parent) {}
protected:
    QString textFromValue(double value) const override
    {
        QString text = QDoubleSpinBox::textFromValue(value);
        if (text.contains(locale().decimalPoint())) {
            while (text.endsWith(locale().zeroDigit())) text.chop(1);
            if (text.endsWith(locale().decimalPoint())) text.chop(1);
        }
        return text;
    }
};
static QDomElement roomSection(QDomDocument &xml, RoomPropertyScope scope, int index)
{
    QDomElement root = xml.documentElement();
    if (scope == RoomPropertyScope::Room) return root;
    const QString tag = scope == RoomPropertyScope::Background ? QStringLiteral("background") : QStringLiteral("view");
    QDomElement container = ActionXml::child(root, tag + QLatin1Char('s'));
    auto entries = ActionXml::elements(container, tag);
    while (entries.size() <= index) { QDomElement entry = xml.createElement(tag); container.appendChild(entry); entries.append(entry); }
    return entries.at(index);
}
RoomPropertiesWindow::RoomPropertiesWindow(RoomDocument *document, Project *project, QWidget *parent)
    : ResourceEditorWindow(parent), m_document(document), m_project(project), m_assets(project),
      m_canvas(new RoomCanvas(document, &m_assets)), m_status(new QLabel), m_preview(new RoomObjectPreview), m_tilePicker(new RoomTilePicker), m_pages(new QStackedWidget)
{
    document->setParent(this); setAttribute(Qt::WA_DeleteOnClose); editorMenuBar()->hide(); resize(1060, 780); setMinimumSize(870, 560);
    auto *body = new QWidget; body->setObjectName(QStringLiteral("roomProperties")); auto *layout = new QVBoxLayout(body); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto *toolbar = new QToolBar; toolbar->setIconSize(QSize(16, 16)); toolbar->setFixedHeight(28); toolbar->setMovable(false); layout->addWidget(toolbar);
    auto *ok = toolbar->addAction(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("OK, Save changes")); connect(ok, &QAction::triggered, this, [this] { if (save()) close(); });
    auto *undo = document->undoStack()->createUndoAction(this); undo->setIcon(QIcon(QStringLiteral(":/images/editor/undo.png"))); undo->setShortcut(QKeySequence::Undo); addAction(undo); toolbar->addAction(undo);
    auto *redo = document->undoStack()->createRedoAction(this); redo->setIcon(QIcon(QStringLiteral(":/images/editor/redo.png"))); redo->setShortcut(QKeySequence::Redo); addAction(redo); toolbar->addAction(redo);
    toolbar->addSeparator();
    auto *clear = toolbar->addAction(QIcon(QStringLiteral(":/images/new.png")), tr("Clear all instances and tiles"));
    connect(clear, &QAction::triggered, this, [this] {
        if (EditorMessageBox::question(this, tr("Clear Room"), tr("Delete all unlocked instances and tiles?"), EditorMessageBox::Yes | EditorMessageBox::No, EditorMessageBox::No) != EditorMessageBox::Yes) return;
        QVector<RoomEntity> removed; for (const auto &entity : m_document->entities()) if (!entity.xml.attribute(QStringLiteral("locked")).toInt()) { auto copy = entity.copy(); copy.xml = QDomElement(); removed.append(copy); }
        m_document->editEntities(removed, tr("Clear room"));
    });
    auto *shift = toolbar->addAction(QIcon(QStringLiteral(":/images/room/shift.png")), tr("Shift room items"));
    connect(shift, &QAction::triggered, this, [this] {
        EditorDialog dialog(this); dialog.setWindowTitle(tr("Shift Room")); auto *form = new QFormLayout(dialog.bodyWidget());
        auto *x = new QSpinBox, *y = new QSpinBox; x->setRange(-10000000, 10000000); y->setRange(-10000000, 10000000); form->addRow(tr("Horizontal shift:"), x); form->addRow(tr("Vertical shift:"), y);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); form->addRow(buttons); connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        QVector<RoomEntity> records; for (const auto &item : m_document->entities()) if (!item.xml.attribute(QStringLiteral("locked")).toInt()) { auto record = item.copy(); record.xml.setAttribute(QStringLiteral("x"), record.xml.attribute(QStringLiteral("x")).toDouble() + x->value()); record.xml.setAttribute(QStringLiteral("y"), record.xml.attribute(QStringLiteral("y")).toDouble() + y->value()); records.append(record); }
        m_document->editEntities(records, tr("Shift room items"));
    });
    auto *lock = toolbar->addAction(QIcon(QStringLiteral(":/images/room/lock.png")), tr("Lock selection (or all items)")), *unlock = toolbar->addAction(QIcon(QStringLiteral(":/images/room/unlock.png")), tr("Unlock selection (or all items)"));
    connect(lock, &QAction::triggered, this, [this] { changeLock(true); }); connect(unlock, &QAction::triggered, this, [this] { changeLock(false); });
    for (const QString &key : {QStringLiteral("hsnap"), QStringLiteral("vsnap")}) {
        toolbar->addWidget(new QLabel(key == QStringLiteral("hsnap") ? tr(" Snap X: ") : tr(" Snap Y: ")));
        auto *spin = new QSpinBox; spin->setRange(1, 100000); spin->setFixedSize(48, 21); spin->setButtonSymbols(QAbstractSpinBox::NoButtons); toolbar->addWidget(spin);
        connect(spin, &QSpinBox::editingFinished, this, [this, spin, key] { if (!m_refreshing) setValue(RoomPropertyScope::Room, key, QString::number(spin->value())); });
        m_refreshers.append([this, spin, key] { spin->setValue(value(RoomPropertyScope::Room, key).toInt()); });
    }
    auto *grid = toolbar->addAction(QIcon(QStringLiteral(":/images/room/grid.png")), tr("Grid")); grid->setCheckable(true); grid->setChecked(true); connect(grid, &QAction::toggled, this, [this](bool value) { m_canvas->setDisplay(QStringLiteral("grid"), value); });
    auto *show = new QToolButton; show->setIcon(QIcon(QStringLiteral(":/images/room/show.png"))); show->setToolTip(tr("Show")); show->setPopupMode(QToolButton::InstantPopup); auto *menu = new QMenu(show);
    for (const QString &key : QStringLiteral("objects|tiles|backgrounds|foregrounds|views").split(QLatin1Char('|'))) {
        QAction *item = menu->addAction(key); item->setCheckable(true); item->setChecked(key != QStringLiteral("views")); connect(item, &QAction::toggled, this, [this, key](bool enabled) { m_canvas->setDisplay(key, enabled); });
    }
    auto *isometric = menu->addAction(tr("Isometric grid")); isometric->setCheckable(true); connect(isometric, &QAction::toggled, this, [this](bool enabled) { setValue(RoomPropertyScope::Room, QStringLiteral("isometric"), enabled ? QStringLiteral("-1") : QStringLiteral("0")); });
    m_refreshers.append([this, isometric] { isometric->setChecked(value(RoomPropertyScope::Room, QStringLiteral("isometric")).toInt() != 0); });
    show->setMenu(menu); toolbar->addWidget(show); toolbar->addSeparator();
    connect(toolbar->addAction(QIcon(QStringLiteral(":/images/room/zoomout.png")), tr("Zoom out")), &QAction::triggered, this, [this] { m_canvas->zoomBy(0.8); });
    connect(toolbar->addAction(QIcon(QStringLiteral(":/images/room/zoomreset.png")), tr("Reset zoom")), &QAction::triggered, m_canvas, &RoomCanvas::resetZoom);
    connect(toolbar->addAction(QIcon(QStringLiteral(":/images/room/zoomin.png")), tr("Zoom in")), &QAction::triggered, this, [this] { m_canvas->zoomBy(1.25); });
    toolbar->addSeparator(); auto *order = toolbar->addAction(QIcon(QStringLiteral(":/images/settings.png")), tr("Instance creation order"));
    connect(order, &QAction::triggered, this, [this] {
        EditorDialog dialog(this); dialog.setWindowTitle(tr("Instance Creation Order")); dialog.resize(420, 480); auto *box = new QVBoxLayout(dialog.bodyWidget());
        box->addWidget(new QLabel(tr("Drag instances to change their creation order."))); auto *list = new QListWidget; list->setDragDropMode(QAbstractItemView::InternalMove); box->addWidget(list, 1);
        QVector<RoomEntity> ordered; for (const auto &record : m_document->entities()) if (!record.tile) ordered.append(record);
        std::sort(ordered.begin(), ordered.end(), [](const RoomEntity &a, const RoomEntity &b) { return a.order < b.order; });
        for (const auto &record : ordered) { auto *item = new QListWidgetItem(record.xml.attribute(QStringLiteral("objName")) + QStringLiteral("  (") + record.id.mid(2) + QLatin1Char(')'), list); item->setData(Qt::UserRole, record.id); item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled); }
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); box->addWidget(buttons); connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        QVector<RoomEntity> records; for (int i = 0; i < list->count(); ++i) { auto record = m_document->entity(list->item(i)->data(Qt::UserRole).toString()); record.order = ordered.at(i).order; records.append(record); }
        m_document->editEntities(records, tr("Change instance creation order"));
    });
    auto *splitter = new QSplitter; splitter->setHandleWidth(3); splitter->setChildrenCollapsible(false); layout->addWidget(splitter, 1);
    auto *sidebar = new QWidget;
    sidebar->setMinimumWidth(150);
    m_canvas->setMinimumWidth(200);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(0, 0, 0, 0); sideLayout->setSpacing(0);
    auto *tabs = new QGridLayout;
    tabs->setContentsMargins(2, 0, 2, 0); tabs->setSpacing(0);
    sideLayout->addLayout(tabs);
    auto *sideSplitter = new QSplitter(Qt::Vertical);
    sideSplitter->setHandleWidth(4); sideSplitter->setChildrenCollapsible(false);
    m_pages->setMinimumHeight(80); sideSplitter->addWidget(m_pages);
    auto *overview = new RoomOverview(m_canvas);
    sideSplitter->addWidget(overview);
    sideSplitter->setStretchFactor(0, 1); sideSplitter->setStretchFactor(1, 0);
    sideLayout->addWidget(sideSplitter, 1);
    const QStringList tabNames = {tr("objects"), tr("settings"), tr("tiles"), tr("backgrounds"), tr("views"), tr("physics")};
    const QList<QWidget *> pages = {createObjectsPage(), createSettingsPage(order), createTilesPage(), createBackgroundsPage(), createViewsPage(), createPhysicsPage()};
    QList<QPushButton *> tabButtons;
    for (int i = 0; i < tabNames.size(); ++i) {
        auto *button = new QPushButton(tabNames.at(i));
        button->setCheckable(true); button->setFixedHeight(22); button->setAutoDefault(false);
        button->setStyleSheet(QStringLiteral("QPushButton { background: #141414; color: #dddddd; border: 1px solid #383838; } QPushButton:checked { border-bottom: 2px solid #62952b; }"));
        tabs->addWidget(button, i < 3 ? 1 : 0, i % 3); tabButtons.append(button);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(pages.at(i)); m_pages->addWidget(scroll);
    }
    for (int i = 0; i < tabButtons.size(); ++i) connect(tabButtons.at(i), &QPushButton::clicked, this, [this, i, tabs, tabButtons] {
        for (int n = 0; n < tabButtons.size(); ++n) {
            tabButtons.at(n)->setChecked(n == i);
            tabs->removeWidget(tabButtons.at(n));
        }
        for (int n = 0; n < tabButtons.size(); ++n) tabs->addWidget(tabButtons.at(n), n / 3 == i / 3 ? 1 : 0, n % 3);
        m_canvas->finishInteraction();
        m_pages->setCurrentIndex(i);
        m_canvas->setMode(i == 0 ? RoomCanvas::Mode::Objects : i == 2 ? RoomCanvas::Mode::Tiles : RoomCanvas::Mode::Inspect);
    });
    tabButtons.first()->setChecked(true);
    splitter->addWidget(sidebar); splitter->addWidget(m_canvas); splitter->setStretchFactor(1, 1); splitter->setSizes({250, 790});
    layout->addWidget(m_status); setCentralWidget(body);
    connect(m_canvas, &RoomCanvas::cursorMoved, this, [this](const QPointF &point) { m_status->setText(tr("x: %1    y: %2    Selected: %3").arg(qRound(point.x())).arg(qRound(point.y())).arg(m_selectionCount)); });
    connect(m_canvas, &RoomCanvas::selectionChanged, this, &RoomPropertiesWindow::refreshSelection);
    connect(m_canvas, &RoomCanvas::creationCodeRequested, this, &RoomPropertiesWindow::editCode);
    connect(m_canvas, &RoomCanvas::openObjectRequested, this, [this](const QString &name) { for (const auto &node : ActionXml::resourceList(*m_project, ResourceType::Object)) if (node.name == name) { emit openResourceRequested(ResourceType::Object, node.filePath); break; } });
    connect(document, &RoomDocument::settingsChanged, this, &RoomPropertiesWindow::refresh);
    connect(document->undoStack(), &QUndoStack::cleanChanged, this, [this] { setWindowTitle(tr("Room Properties: %1%2").arg(m_document->name(), m_document->isModified() ? QStringLiteral(" *") : QString())); });
    connect(document, &RoomDocument::saved, this, [this] { emit resourceSaved(ResourceType::Room, filePath(), QString()); });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this); connect(saveShortcut, &QShortcut::activated, this, &RoomPropertiesWindow::saveProjectRequested);
    refresh(); refreshSelection(); refreshObjectPreview();
}
RoomPropertiesWindow::~RoomPropertiesWindow()
{
    // QWidget deletes its children after our members have been destroyed.
    // Scene selection / undo-stack notifications must not refresh the inspector
    // during that teardown. Delete the view while its document and assets live.
    for (QObject *child : findChildren<QObject *>())
        QObject::disconnect(child, nullptr, this, nullptr);
    delete takeCentralWidget();
}
QString RoomPropertiesWindow::value(RoomPropertyScope scope, const QString &key) const
{
    if (scope == RoomPropertyScope::Entity) return m_document->entities().value(m_selectedId).xml.attribute(key, key.startsWith(QStringLiteral("scale")) ? QStringLiteral("1") : QStringLiteral("0"));
    const QDomDocument xml = m_refreshing ? m_propertySettings : m_document->settings();
    const QDomElement root = xml.documentElement();
    const auto sections = ActionXml::elements(root.firstChildElement(scope == RoomPropertyScope::Background ? QStringLiteral("backgrounds") : QStringLiteral("views")), scope == RoomPropertyScope::Background ? QStringLiteral("background") : QStringLiteral("view"));
    const QDomElement section = scope == RoomPropertyScope::Room ? root : sections.value(scope == RoomPropertyScope::Background ? m_backgroundIndex : m_viewIndex);
    return scope == RoomPropertyScope::Room ? ActionXml::text(section, key) : section.attribute(key);
}
void RoomPropertiesWindow::setValue(RoomPropertyScope scope, const QString &key, const QString &text)
{
    if (m_refreshing) return;
    if (scope == RoomPropertyScope::Entity) {
        QVector<RoomEntity> records;
        for (const QString &id : m_canvas->selectedIds()) { auto record = m_document->entity(id); if (record.xml.attribute(QStringLiteral("locked")).toInt()) continue; record.xml.setAttribute(key, text); records.append(record); }
        m_document->editEntities(records, tr("Edit instance properties"));
    } else {
        QDomDocument xml = m_document->settings(); QDomElement section = roomSection(xml, scope, scope == RoomPropertyScope::Background ? m_backgroundIndex : m_viewIndex);
        if (scope == RoomPropertyScope::Room) ActionXml::setText(section, key, text); else section.setAttribute(key, text);
        m_document->editSettings(xml, tr("Edit room properties"));
    }
}
QDoubleSpinBox *RoomPropertiesWindow::numberControl(QWidget *parent, RoomPropertyScope scope, const QString &key, double minimum, double maximum, int decimals)
{
    auto *spin = new RoomNumberEdit(parent); spin->setDecimals(decimals); spin->setRange(minimum, maximum); spin->setKeyboardTracking(false); spin->setButtonSymbols(QAbstractSpinBox::NoButtons); spin->setFixedHeight(19);
    connect(spin, &QDoubleSpinBox::editingFinished, this, [this, spin, scope, key] { setValue(scope, key, QString::number(spin->value(), 'g', 12)); });
    m_refreshers.append([this, spin, scope, key] { spin->setValue(value(scope, key).toDouble()); spin->setEnabled(scope != RoomPropertyScope::Entity || !m_selectedId.isEmpty()); });
    return spin;
}
QCheckBox *RoomPropertiesWindow::checkControl(QWidget *parent, const QString &label, RoomPropertyScope scope, const QString &key)
{
    auto *check = new QCheckBox(label, parent);
    connect(check, &QCheckBox::toggled, this, [this, scope, key](bool enabled) { setValue(scope, key, enabled ? QStringLiteral("-1") : QStringLiteral("0")); });
    m_refreshers.append([this, check, scope, key] { check->setChecked(value(scope, key).toInt() != 0); });
    return check;
}
ResourceComboBox *RoomPropertiesWindow::resourceCombo(ResourceType type)
{
    auto *combo = new ResourceComboBox; combo->setMinimumWidth(0); combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); combo->setMinimumContentsLength(8);
    combo->setResources(*m_project, type, QString(), tr("<none>"), QString());
    m_resourceCombos.append(qMakePair(combo, type)); return combo;
}
QWidget *RoomPropertiesWindow::resourceControl(QWidget *parent, RoomPropertyScope scope, const QString &key, ResourceType type)
{
    auto *combo = resourceCombo(type);
    auto *picker = resourcePicker(parent, combo, 112);
    connect(combo, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, combo, scope, key] { setValue(scope, key, combo->currentData().toString()); });
    m_refreshers.append([this, combo, scope, key] { const QString name = value(scope, key); if (combo->findData(name) < 0) combo->addItem(name, name); combo->setCurrentIndex(combo->findData(name)); });
    return picker;
}
QPushButton *RoomPropertiesWindow::colorControl(QWidget *parent, RoomPropertyScope scope, const QString &key, bool alpha)
{
    auto *button = new QPushButton(parent); button->setToolTip(tr("Colour")); button->setAutoDefault(false);
    connect(button, &QPushButton::clicked, this, [this, scope, key, alpha] {
        if (scope == RoomPropertyScope::Entity && m_selectedId.isEmpty()) return;
        const uint bgr = value(scope, key).toUInt(); QColor color(bgr & 255, (bgr >> 8) & 255, (bgr >> 16) & 255, alpha ? bgr >> 24 : 255);
        color = EditorColorDialog::getColor(color, this, tr("Room Colour"), alpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions());
        if (color.isValid()) setValue(scope, key, QString::number(uint(color.red()) | uint(color.green() << 8) | uint(color.blue() << 16) | (alpha ? uint(color.alpha()) << 24 : 0)));
    });
    m_refreshers.append([this, button, scope, key] { const uint bgr = value(scope, key).toUInt(); const QColor color(bgr & 255, (bgr >> 8) & 255, (bgr >> 16) & 255); button->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid #141414; padding: 0; }").arg(color.name())); });
    return button;
}
void RoomPropertiesWindow::refresh()
{ m_propertySettings = m_document->settings(); m_refreshing = true; for (const auto &update : m_refreshers) update(); m_refreshing = false; setWindowTitle(tr("Room Properties: %1%2").arg(m_document->name(), m_document->isModified() ? QStringLiteral(" *") : QString())); }
void RoomPropertiesWindow::refreshSelection()
{
    const QStringList ids = m_canvas->selectedIds(); m_selectionCount = ids.size(); m_selectedId = ids.isEmpty() ? QString() : ids.first();
    refresh();
}
void RoomPropertiesWindow::refreshObjectPreview()
{
    const QPixmap image = m_assets.object(m_objectChoice->currentData().toString()).image;
    if (!m_preview->pixmap() || m_preview->pixmap()->cacheKey() != image.cacheKey()) {
        m_preview->setPixmap(image);
        m_preview->resize(image.size().expandedTo(QSize(1, 1)));
        auto *scroll = qobject_cast<QScrollArea *>(m_preview->parentWidget()->parentWidget());
        scroll->horizontalScrollBar()->setValue(0); scroll->verticalScrollBar()->setValue(0);
    }
}

bool RoomPropertiesWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease && (watched == m_preview || watched == m_preview->parentWidget())) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            m_objectChoice->showPopupAt(mouse->globalPos());
            return true;
        }
    }
    return ResourceEditorWindow::eventFilter(watched, event);
}
void RoomPropertiesWindow::updateResources()
{
    m_refreshing = true;
    for (const auto &entry : m_resourceCombos) entry.first->setResources(*m_project, entry.second, entry.first->currentData().toString(), tr("<none>"), QString());
    m_refreshing = false; m_canvas->reloadAssets(); m_tilePicker->visual = m_assets.background(m_tileChoice->currentData().toString()); m_tilePicker->update(); refreshSelection(); refreshObjectPreview();
}
void RoomPropertiesWindow::changeLock(bool locked)
{
    QVector<RoomEntity> records; const QStringList selected = m_canvas->selectedIds();
    const QStringList ids = selected.isEmpty() ? QStringList(m_document->entities().keys()) : selected;
    for (const QString &id : ids) { auto record = m_document->entity(id); record.xml.setAttribute(QStringLiteral("locked"), locked ? -1 : 0); records.append(record); }
    m_document->editEntities(records, tr("Change room locks"));
}
void RoomPropertiesWindow::editCode(const QString &id)
{
    showCode(id, -1, 0);
}
void RoomPropertiesWindow::showCode(const QString &id, int offset, int length)
{
    auto entity = m_document->entity(id); if (!id.isEmpty() && (entity.tile || entity.xml.isNull())) return;
    if (!id.isEmpty() && entity.xml.attribute(QStringLiteral("locked")).toInt() && offset < 0) return;
    const QString title = id.isEmpty() ? tr("Room Creation Code: %1").arg(m_document->name()) : tr("Instance Creation Code: %1").arg(id.mid(2));
    const QString source = id.isEmpty() ? value(RoomPropertyScope::Room, QStringLiteral("code")) : entity.xml.attribute(QStringLiteral("code"));
    CodeSnippetEditorWindow editor(title, tr("creation code"), source, id.isEmpty() ? m_document->name() : id.mid(2), this);
    editor.setResources(*m_project);
    if (offset >= 0) editor.selectRange(offset, length);
    const bool locked = !id.isEmpty() && entity.xml.attribute(QStringLiteral("locked")).toInt();
    editor.setReadOnly(locked);
    if (!editor.exec() || editor.code() == source) return;
    if (locked) return;
    if (id.isEmpty()) setValue(RoomPropertyScope::Room, QStringLiteral("code"), editor.code());
    else { entity.xml.setAttribute(QStringLiteral("code"), editor.code()); m_document->editEntities({entity}, tr("Edit instance creation code")); }
}
QString RoomPropertiesWindow::filePath() const { return m_document->filePath(); }
void RoomPropertiesWindow::relocate(const QString &oldDirectory, const QString &newDirectory) { m_document->relocate(oldDirectory, newDirectory); updateResources(); }
bool RoomPropertiesWindow::save() { m_canvas->finishInteraction(); QString error; if (m_document->save(error)) return true; EditorMessageBox::critical(this, tr("Cannot Save Room"), error); return false; }
void RoomPropertiesWindow::closeEvent(QCloseEvent *event)
{
    m_canvas->finishInteraction();
    if (m_document->isModified()) {
        const auto answer = EditorMessageBox::question(this, tr("Room Properties"), tr("Save changes to %1?").arg(m_document->name()), EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel || (answer == EditorMessageBox::Save && !save())) { event->ignore(); return; }
    }
    event->accept();
}


// Keep the original compact control sizes while allowing the sidebar splitters
// to resize independently. Scroll areas handle narrower or shorter panels.
static QWidget *roomPropertyPage()
{
    auto *page = new QWidget;
    page->setMinimumSize(230, 458);
    return page;
}
static QLabel *roomLabel(QWidget *parent, const QString &text, int x, int y, int width, int height = 19)
{
    auto *label = new QLabel(text, parent);
    label->setGeometry(x, y, width, height);
    return label;
}
static QPushButton *roomButton(QWidget *parent, const QString &text, const QRect &rect, const QString &icon = QString())
{
    auto *button = new QPushButton(text, parent);
    button->setAutoDefault(false); button->setGeometry(rect);
    if (!icon.isEmpty()) { button->setIcon(QIcon(icon)); button->setIconSize(QSize(16, 16)); }
    return button;
}
QWidget *RoomPropertiesWindow::resourcePicker(QWidget *parent, QComboBox *combo, int fieldWidth)
{
    auto *picker = new QWidget(parent);
    picker->setFixedSize(fieldWidth + 28, 21);
    combo->setParent(picker); combo->setGeometry(0, 1, fieldWidth, 19);
    combo->setStyleSheet(QStringLiteral("QComboBox { background: #383838; border: 1px solid #a5a59b; padding: 0 2px; } QComboBox::drop-down { width: 0; border: none; } QComboBox::down-arrow { image: none; }"));
    auto *select = new QToolButton(picker);
    select->setGeometry(fieldWidth + 6, 0, 21, 21);
    select->setIcon(QIcon(QStringLiteral(":/object/controls/selectresource.png")));
    select->setIconSize(QSize(16, 16)); select->setAutoRaise(true);
    select->setToolTip(tr("Select resource"));
    connect(select, &QToolButton::clicked, combo, &QComboBox::showPopup);
    return picker;
}
QWidget *RoomPropertiesWindow::createObjectsPage()
{
    auto *page = roomPropertyPage();
    auto *previewScroll = new QScrollArea(page); previewScroll->setGeometry(6, 8, 222, 128);
    previewScroll->setWidgetResizable(false); previewScroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    previewScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    previewScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    previewScroll->setStyleSheet(QStringLiteral("QScrollArea { border: 1px solid #a5a59b; }"));
    m_preview->setAlignment(Qt::AlignLeft | Qt::AlignTop); m_preview->setScaledContents(false);
    m_preview->setStyleSheet(QStringLiteral("background: transparent; border: none; padding: 0;"));
    m_preview->resize(1, 1); previewScroll->setWidget(m_preview);
    m_preview->setAutoFillBackground(false);
    m_preview->setToolTip(tr("Click to select an object"));
    previewScroll->viewport()->setToolTip(tr("Click to select an object"));
    m_preview->installEventFilter(this); previewScroll->viewport()->installEventFilter(this);
    const QStringList keys = QStringLiteral("x|y|rotation|scaleX|scaleY|alpha").split(QLatin1Char('|'));
    const QStringList labels = {tr("Position X"), tr("Y"), tr("Rotation"), tr("Scale X"), tr("Y"), tr("Alpha")};
    const int labelX[] = {4, 98, 148}, fieldX[] = {56, 108, 192};
    for (int n = 0; n < 6; ++n) {
        const int y = 144 + (n / 3) * 27, column = n % 3;
        roomLabel(page, labels.at(n), labelX[column], y, fieldX[column] - labelX[column]);
        QDoubleSpinBox *spin;
        if (n != 5) spin = numberControl(page, RoomPropertyScope::Entity, keys.at(n), -10000000, 10000000, n < 2 ? 0 : 3);
        else {
            spin = new RoomNumberEdit(page); spin->setRange(0, 1); spin->setDecimals(3);
            spin->setButtonSymbols(QAbstractSpinBox::NoButtons); spin->setKeyboardTracking(false);
            connect(spin, &QDoubleSpinBox::editingFinished, this, [this, spin] {
                if (m_refreshing) return;
                QVector<RoomEntity> records;
                for (const QString &id : m_canvas->selectedIds()) {
                    auto record = m_document->entity(id);
                    if (record.xml.attribute(QStringLiteral("locked")).toInt()) continue;
                    const uint color = record.xml.attribute(QStringLiteral("colour"), QStringLiteral("4294967295")).toUInt();
                    record.xml.setAttribute(QStringLiteral("colour"), QString::number((color & 0xffffff) | (uint(qRound(spin->value() * 255)) << 24)));
                    records.append(record);
                }
                m_document->editEntities(records, tr("Change instance alpha"));
            });
            m_refreshers.append([this, spin] {
                spin->setEnabled(!m_selectedId.isEmpty());
                spin->setValue((value(RoomPropertyScope::Entity, QStringLiteral("colour")).toUInt() >> 24) / 255.0);
            });
        }
        spin->setGeometry(fieldX[column], y, 36, 19);
    }
    for (int n = 0; n < 2; ++n) {
        const QString key = n == 0 ? QStringLiteral("scaleX") : QStringLiteral("scaleY");
        auto *flip = roomButton(page, n == 0 ? tr("Flip X") : tr("Flip Y"), QRect(6 + n * 78, 198, 72, 21));
        connect(flip, &QPushButton::clicked, this, [this, key] {
            QVector<RoomEntity> records;
            for (const QString &id : m_canvas->selectedIds()) {
                auto record = m_document->entity(id);
                if (record.xml.attribute(QStringLiteral("locked")).toInt()) continue;
                record.xml.setAttribute(key, -record.xml.attribute(key, QStringLiteral("1")).toDouble()); records.append(record);
            }
            m_document->editEntities(records, tr("Flip room items"));
        });
    }
    roomLabel(page, tr("Colour"), 158, 199, 34);
    colorControl(page, RoomPropertyScope::Entity, QStringLiteral("colour"), true)->setGeometry(192, 199, 36, 19);
    roomLabel(page, tr("Object to add with left mouse:"), 12, 257, 214);
    m_objectChoice = resourceCombo(ResourceType::Object);
    resourcePicker(page, m_objectChoice, 130)->move(12, 280);
    connect(m_objectChoice, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this] {
        m_canvas->setObject(m_objectChoice->currentData().toString()); refreshObjectPreview();
    });
    auto *under = new QCheckBox(tr("Delete underlying"), page); under->setGeometry(12, 307, 210, 18);
    connect(under, &QCheckBox::toggled, this, [this](bool enabled) { if (m_pages->currentIndex() == 0) m_canvas->setDeleteUnderlying(enabled); });
    connect(m_pages, &QStackedWidget::currentChanged, this, [this, under](int index) { if (index == 0) m_canvas->setDeleteUnderlying(under->isChecked()); });
    auto *hint = roomLabel(page, tr("Left mouse button = move/add\n  + <Alt> = no snap\n  + <Shift><Ctrl> = add multiple\n  + <Shift> = Select multiple\n  + <Ctrl> = add\n  + <Space> = scroll room\nRight mouse button = menu\nDouble click = creation code\nMiddle mouse button = scroll"), 12, 330, 218, 126);
    hint->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    return page;
}
QWidget *RoomPropertiesWindow::createSettingsPage(QAction *orderAction)
{
    auto *page = roomPropertyPage();
    roomLabel(page, tr("Name:"), 8, 12, 40);
    auto *name = new QLineEdit(m_document->name(), page); enableResourceRenaming(name, ResourceType::Room); name->setGeometry(48, 12, 104, 19);
    m_refreshers.append([this, name] { name->setText(m_document->name()); });
    const QStringList keys = {QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("speed")};
    const QStringList labels = {tr("Width:"), tr("Height:"), tr("Speed:")};
    const int y[] = {49, 73, 111};
    for (int n = 0; n < 3; ++n) {
        roomLabel(page, labels.at(n), 8, y[n], 40);
        numberControl(page, RoomPropertyScope::Room, keys.at(n), 1, n == 2 ? 10000 : 10000000)->setGeometry(48, y[n], n == 2 ? 64 : 104, 19);
    }
    checkControl(page, tr("Persistent"), RoomPropertyScope::Room, QStringLiteral("persistent"))->setGeometry(8, 138, 220, 18);
    checkControl(page, tr("Clear Display Buffer with Window Colour"), RoomPropertyScope::Room, QStringLiteral("clearDisplayBuffer"))->setGeometry(8, 158, 236, 18);
    auto *code = roomButton(page, tr("Creation code"), QRect(48, 181, 104, 23), QStringLiteral(":/images/script.png"));
    connect(code, &QPushButton::clicked, this, [this] { editCode(QString()); });
    auto *order = roomButton(page, tr("Instance Order"), QRect(48, 220, 104, 23));
    connect(order, &QPushButton::clicked, orderAction, &QAction::trigger);
    return page;
}
QWidget *RoomPropertiesWindow::createTilesPage()
{
    auto *page = roomPropertyPage();
    m_tilePicker->setParent(page); m_tilePicker->setMaximumHeight(QWIDGETSIZE_MAX); m_tilePicker->setGeometry(2, 4, 226, 227);
    m_tileChoice = resourceCombo(ResourceType::Background);
    resourcePicker(page, m_tileChoice, 112)->move(12, 237);
    auto updateStamp = [this] {
        m_canvas->setTile(m_tileChoice->currentData().toString(), m_tileSource, m_tileDepth);
        m_tilePicker->selected = m_tileSource; m_tilePicker->update();
    };
    for (int n = 0; n < 2; ++n) {
        roomLabel(page, n == 0 ? tr("X") : tr("Y"), 172, 238 + n * 24, 12);
        auto *spin = new QSpinBox(page); spin->setRange(0, 100000); spin->setButtonSymbols(QAbstractSpinBox::NoButtons); spin->setGeometry(184, 238 + n * 24, 36, 19);
        connect(spin, &QSpinBox::editingFinished, this, [this, spin, n, updateStamp] { if (n == 0) m_tileSource.moveLeft(spin->value()); else m_tileSource.moveTop(spin->value()); updateStamp(); });
        m_refreshers.append([this, spin, n] { spin->setValue(n == 0 ? m_tileSource.x() : m_tileSource.y()); });
    }
    auto *under = new QCheckBox(tr("Delete underlying"), page); under->setChecked(true); under->setGeometry(12, 264, 155, 18);
    connect(under, &QCheckBox::toggled, this, [this](bool enabled) { if (m_pages->currentIndex() == 2) m_canvas->setDeleteUnderlying(enabled); });
    connect(m_pages, &QStackedWidget::currentChanged, this, [this, under](int index) { if (index == 2) m_canvas->setDeleteUnderlying(under->isChecked()); });
    auto *hint = roomLabel(page, tr("In tile selection:\nLeft Mouse Button = select tile\n                 + <Ctrl> = resize selection\n                 + <Alt> = no snap resize"), 12, 287, 215, 65);
    hint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    roomLabel(page, tr("Current Tile Layer:"), 12, 354, 142);
    m_tileLayerChoice = new QComboBox(page); m_tileLayerChoice->setGeometry(12, 375, 139, 21);
    m_emptyTileLayers.insert(m_tileDepth);
    for (const auto &record : m_document->entities()) if (record.tile) m_emptyTileLayers.insert(record.xml.attribute(QStringLiteral("depth")).toInt());
    connect(m_tileLayerChoice, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, updateStamp](int index) {
        if (index < 0) return; m_tileDepth = m_tileLayerChoice->itemData(index).toInt(); updateStamp();
    });
    auto *hide = new QCheckBox(tr("Hide other\nlayers"), page); hide->setGeometry(157, 374, 71, 28);
    connect(hide, &QCheckBox::toggled, m_canvas, &RoomCanvas::setHideOtherTileLayers);
    auto *add = roomButton(page, tr("Add"), QRect(12, 405, 64, 23), QStringLiteral(":/object/controls/add.png"));
    auto *remove = roomButton(page, tr("Delete"), QRect(84, 405, 64, 23), QStringLiteral(":/object/controls/delete.png"));
    auto *change = roomButton(page, tr("Change"), QRect(48, 436, 64, 23), QStringLiteral(":/object/controls/change.png"));
    connect(add, &QPushButton::clicked, this, [this] { editTileLayer(true); });
    connect(remove, &QPushButton::clicked, this, &RoomPropertiesWindow::deleteTileLayer);
    connect(change, &QPushButton::clicked, this, [this] { editTileLayer(false); });
    connect(m_document, &RoomDocument::entitiesChanged, this, [this](const QStringList &ids) {
        bool added = false;
        for (const QString &id : ids) {
            if (!id.startsWith(QStringLiteral("t:"))) continue;
            const auto found = m_document->entities().constFind(id);
            if (found == m_document->entities().constEnd()) continue;
            const int depth = found->xml.attribute(QStringLiteral("depth")).toInt();
            if (!m_emptyTileLayers.contains(depth)) { m_emptyTileLayers.insert(depth); added = true; }
        }
        if (added) refreshTileLayers();
    });
    connect(m_tileChoice, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, updateStamp] {
        m_tilePicker->visual = m_assets.background(m_tileChoice->currentData().toString());
        m_tileSource = QRect(m_tilePicker->visual.tileOffset, m_tilePicker->visual.tileSize); updateStamp(); refresh();
    });
    m_tilePicker->changed = [this, updateStamp](const QRect &rect) { m_tileSource = rect; updateStamp(); refresh(); };
    refreshTileLayers();
    return page;
}
QWidget *RoomPropertiesWindow::createBackgroundsPage()
{
    auto *page = roomPropertyPage();
    checkControl(page, tr("Draw background color"), RoomPropertyScope::Room, QStringLiteral("showcolour"))->setGeometry(8, 8, 220, 19);
    roomLabel(page, tr("Color:"), 48, 31, 32);
    colorControl(page, RoomPropertyScope::Room, QStringLiteral("colour"), false)->setGeometry(80, 32, 72, 17);
    auto *layers = new QListWidget(page); layers->setGeometry(8, 57, 144, 113); layers->setUniformItemSizes(true);
    for (int n = 0; n < 8; ++n) layers->addItem(tr("Background %1").arg(n));
    layers->setCurrentRow(0);
    connect(layers, &QListWidget::currentRowChanged, this, [this](int index) { m_backgroundIndex = index; refresh(); });
    checkControl(page, tr("Visible when room starts"), RoomPropertyScope::Background, QStringLiteral("visible"))->setGeometry(8, 178, 220, 19);
    checkControl(page, tr("Foreground image"), RoomPropertyScope::Background, QStringLiteral("foreground"))->setGeometry(8, 202, 220, 19);
    resourceControl(page, RoomPropertyScope::Background, QStringLiteral("name"), ResourceType::Background)->move(8, 221);
    for (int n = 0; n < 2; ++n) {
        checkControl(page, n == 0 ? tr("Tile Hor.") : tr("Tile Vert."), RoomPropertyScope::Background, n == 0 ? QStringLiteral("htiled") : QStringLiteral("vtiled"))->setGeometry(8, 247 + n * 24, 83, 19);
        roomLabel(page, n == 0 ? tr("X:") : tr("Y:"), 94, 247 + n * 24, 18);
        numberControl(page, RoomPropertyScope::Background, n == 0 ? QStringLiteral("x") : QStringLiteral("y"), -10000000, 10000000)->setGeometry(112, 247 + n * 24, 40, 19);
    }
    checkControl(page, tr("Stretch"), RoomPropertyScope::Background, QStringLiteral("stretch"))->setGeometry(8, 297, 130, 19);
    roomLabel(page, tr("Hor. Speed:"), 48, 326, 64);
    numberControl(page, RoomPropertyScope::Background, QStringLiteral("hspeed"), -10000000, 10000000)->setGeometry(112, 326, 40, 19);
    roomLabel(page, tr("Vert. Speed:"), 48, 352, 64);
    numberControl(page, RoomPropertyScope::Background, QStringLiteral("vspeed"), -10000000, 10000000)->setGeometry(112, 352, 40, 19);
    return page;
}
QWidget *RoomPropertiesWindow::createViewsPage()
{
    auto *page = roomPropertyPage();
    checkControl(page, tr("Enable the use of Views"), RoomPropertyScope::Room, QStringLiteral("enableViews"))->setGeometry(8, 8, 228, 19);
    checkControl(page, tr("Clear Background with Window Colour"), RoomPropertyScope::Room, QStringLiteral("clearViewBackground"))->setGeometry(8, 29, 236, 19);
    auto *views = new QListWidget(page); views->setGeometry(8, 53, 154, 88); views->setUniformItemSizes(true);
    for (int n = 0; n < 8; ++n) views->addItem(tr("View %1").arg(n));
    views->setCurrentRow(0);
    connect(views, &QListWidget::currentRowChanged, this, [this](int index) { m_viewIndex = index; refresh(); });
    checkControl(page, tr("Visible when room starts"), RoomPropertyScope::View, QStringLiteral("visible"))->setGeometry(8, 148, 220, 19);
    for (int n = 0; n < 2; ++n) {
        auto *group = new QGroupBox(n == 0 ? tr("View in room") : tr("Port on screen"), page); group->setGeometry(8, 175 + n * 73, 152, 63);
        const QString suffix = n == 0 ? QStringLiteral("view") : QStringLiteral("port");
        const QStringList keys = {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("w"), QStringLiteral("h")};
        for (int k = 0; k < 4; ++k) {
            const int x = 12 + (k / 2) * 70, y = 18 + (k % 2) * 24;
            roomLabel(group, keys.at(k).toUpper() + QLatin1Char(':'), x, y, 18);
            numberControl(group, RoomPropertyScope::View, keys.at(k) + suffix, k < 2 ? -10000000 : 1, 10000000)->setGeometry(x + 18, y, 36, 19);
        }
    }
    auto *follow = new QGroupBox(tr("Object following"), page); follow->setGeometry(8, 321, 152, 95);
    resourceControl(follow, RoomPropertyScope::View, QStringLiteral("objName"), ResourceType::Object)->move(8, 18);
    const QStringList keys = {QStringLiteral("hborder"), QStringLiteral("vborder"), QStringLiteral("hspeed"), QStringLiteral("vspeed")};
    const QStringList labels = {tr("Hbor:"), tr("Vbor:"), tr("Hsp:"), tr("Vsp:")};
    for (int n = 0; n < 4; ++n) {
        const int x = 6 + (n / 2) * 74, y = 46 + (n % 2) * 25;
        roomLabel(follow, labels.at(n), x, y, 32);
        numberControl(follow, RoomPropertyScope::View, keys.at(n), -10000000, 10000000)->setGeometry(x + 32, y, 32, 19);
    }
    return page;
}
QWidget *RoomPropertiesWindow::createPhysicsPage()
{
    auto *page = roomPropertyPage();
    checkControl(page, tr("Room is Physics World"), RoomPropertyScope::Room, QStringLiteral("PhysicsWorld"))->setGeometry(12, 8, 220, 19);
    auto *group = new QGroupBox(tr("Physics World Properties:"), page); group->setGeometry(12, 32, 194, 79);
    roomLabel(group, tr("Gravity:"), 8, 22, 48);
    roomLabel(group, tr("X:"), 64, 22, 18);
    numberControl(group, RoomPropertyScope::Room, QStringLiteral("PhysicsWorldGravityX"), -100000, 100000, 3)->setGeometry(82, 22, 30, 19);
    roomLabel(group, tr("Y:"), 118, 22, 18);
    numberControl(group, RoomPropertyScope::Room, QStringLiteral("PhysicsWorldGravityY"), -100000, 100000, 3)->setGeometry(136, 22, 30, 19);
    roomLabel(group, tr("Pixels To Meters:"), 8, 49, 90);
    numberControl(group, RoomPropertyScope::Room, QStringLiteral("PhysicsWorldPixToMeters"), 0.000001, 1000, 6)->setGeometry(98, 49, 87, 19);
    return page;
}
void RoomPropertiesWindow::refreshTileLayers()
{
    QSet<int> depths = m_emptyTileLayers;
    depths.insert(m_tileDepth);
    QList<int> sorted = depths.toList(); std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    QSignalBlocker blocker(m_tileLayerChoice); m_tileLayerChoice->clear();
    for (int depth : sorted) m_tileLayerChoice->addItem(tr("Layer %1").arg(depth), depth);
    m_tileLayerChoice->setCurrentIndex(m_tileLayerChoice->findData(m_tileDepth));
}
void RoomPropertiesWindow::editTileLayer(bool add)
{
    bool accepted = false;
    const int depth = EditorInputDialog::getInt(this, add ? tr("Add Tile Layer") : tr("Change Tile Layer"), tr("Depth:"), m_tileDepth, -2147483647, 2147483647, 1, &accepted);
    if (!accepted) return;
    const int previous = m_tileDepth;
    m_emptyTileLayers.insert(depth); m_tileDepth = depth;
    if (!add && depth != previous) {
        m_emptyTileLayers.remove(previous);
        QVector<RoomEntity> records;
        for (const auto &item : m_document->entities()) if (item.tile && item.xml.attribute(QStringLiteral("depth")).toInt() == previous && !item.xml.attribute(QStringLiteral("locked")).toInt()) {
            auto record = item.copy(); record.xml.setAttribute(QStringLiteral("depth"), depth); records.append(record);
        }
        m_document->editEntities(records, tr("Change tile layer depth"));
        for (const auto &record : m_document->entities()) if (record.tile) m_emptyTileLayers.insert(record.xml.attribute(QStringLiteral("depth")).toInt());
    }
    refreshTileLayers(); m_canvas->setTile(m_tileChoice->currentData().toString(), m_tileSource, m_tileDepth);
}
void RoomPropertiesWindow::deleteTileLayer()
{
    QVector<RoomEntity> records;
    for (const auto &item : m_document->entities()) if (item.tile && item.xml.attribute(QStringLiteral("depth")).toInt() == m_tileDepth && !item.xml.attribute(QStringLiteral("locked")).toInt()) {
        auto record = item.copy(); record.xml = QDomElement(); records.append(record);
    }
    if (!records.isEmpty() && EditorMessageBox::question(this, tr("Delete Tile Layer"), tr("Delete all unlocked tiles in this layer?"), EditorMessageBox::Yes | EditorMessageBox::No, EditorMessageBox::No) != EditorMessageBox::Yes) return;
    m_emptyTileLayers.remove(m_tileDepth);
    m_document->editEntities(records, tr("Delete tile layer"));
    QSet<int> remaining = m_emptyTileLayers;
    for (const auto &record : m_document->entities()) if (record.tile) remaining.insert(record.xml.attribute(QStringLiteral("depth")).toInt());
    m_emptyTileLayers = remaining;
    QList<int> sorted = remaining.toList(); std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    m_tileDepth = sorted.isEmpty() ? 1000000 : sorted.first();
    refreshTileLayers(); m_canvas->setTile(m_tileChoice->currentData().toString(), m_tileSource, m_tileDepth);
}
