#include "resourceselector.h"
#include "pathpropertieswindow.h"
#include "pathdocument.h"
#include "pathcanvas.h"
#include "roomdocument.h"
#include "roomassets.h"
#include "roomcanvas.h"
#include "project.h"
#include "actionxml.h"
#include "editorstandarddialogs.h"
#include "editordialog.h"
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QRadioButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

class PathNumberEdit : public QDoubleSpinBox
{
public:
    explicit PathNumberEdit(QWidget *parent) : QDoubleSpinBox(parent) {}
protected:
    QString textFromValue(double value) const override { return locale().toString(value, 'g', 12); }
};
static QDoubleSpinBox *pathNumber(QWidget *parent, double minimum, double maximum)
{
    auto *edit = new PathNumberEdit(parent); edit->setRange(minimum, maximum); edit->setDecimals(6); edit->setKeyboardTracking(false);
    edit->setButtonSymbols(QAbstractSpinBox::NoButtons); edit->setFixedHeight(20); edit->setMinimumWidth(48); return edit;
}
static QSpinBox *pathInteger(int minimum, int maximum, int width)
{
    auto *edit = new QSpinBox; edit->setRange(minimum, maximum); edit->setKeyboardTracking(false); edit->setButtonSymbols(QAbstractSpinBox::NoButtons); edit->setFixedSize(width, 20); return edit;
}
PathPropertiesWindow::PathPropertiesWindow(PathDocument *document, const Project *project, const QSharedPointer<RoomAssets> &assets, QWidget *parent)
    : ResourceEditorWindow(parent), m_document(document), m_project(project), m_canvas(new PathCanvas(document)), m_points(new QListWidget), m_assets(assets)
{
    setObjectName(QStringLiteral("pathProperties")); document->setParent(this); setAttribute(Qt::WA_DeleteOnClose); editorMenuBar()->hide(); resize(850, 485); setMinimumSize(700, 420);
    auto *body = new QWidget; auto *layout = new QVBoxLayout(body); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0); setCentralWidget(body);
    auto *toolbar = new QToolBar; toolbar->setIconSize(QSize(16, 16)); toolbar->setFixedHeight(28); toolbar->setMovable(false); layout->addWidget(toolbar);
    auto action = [toolbar](const QString &icon, const QString &text) { return toolbar->addAction(QIcon(icon), text); };
    connect(action(QStringLiteral(":/images/editor/ok.png"), tr("Save and close")), &QAction::triggered, this, [this] { if (save()) close(); });
    toolbar->addSeparator();
    auto *undo = action(QStringLiteral(":/images/editor/undo.png"), tr("Undo"));
    connect(undo, &QAction::triggered, this, [this] { m_canvas->finishInteraction(); m_document->undoStack()->undo(); });
    connect(document->undoStack(), &QUndoStack::canUndoChanged, undo, &QAction::setEnabled); undo->setEnabled(false);
    toolbar->addSeparator();
    const QStringList operations = {QStringLiteral("clear"), QStringLiteral("reverse"), QStringLiteral("shift"), QStringLiteral("mirror"), QStringLiteral("flip"), QStringLiteral("rotate"), QStringLiteral("scale")};
    const QStringList titles = {tr("Clear path"), tr("Reverse path"), tr("Shift path"), tr("Mirror path horizontally"), tr("Flip path vertically"), tr("Rotate path"), tr("Scale path")};
    for (int i = 0; i < operations.size(); ++i) { const QString operation = operations.at(i); connect(action(QStringLiteral(":/images/path/%1.png").arg(operation), titles.at(i)), &QAction::triggered, this, [this, operation] { transformPath(operation); }); }
    toolbar->addSeparator();
    const QStringList directions = {QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("up"), QStringLiteral("down")};
    const QVector<QPointF> offsets = {QPointF(-32, 0), QPointF(32, 0), QPointF(0, -32), QPointF(0, 32)};
    const QStringList viewTitles = {tr("Move view left"), tr("Move view right"), tr("Move view up"), tr("Move view down")};
    for (int i = 0; i < directions.size(); ++i) { const QPointF offset = offsets.at(i); connect(action(QStringLiteral(":/images/path/%1.png").arg(directions.at(i)), viewTitles.at(i)), &QAction::triggered, this, [this, offset] { m_canvas->panBy(offset); }); }
    connect(action(QStringLiteral(":/images/path/center.png"), tr("Center view around path")), &QAction::triggered, m_canvas, &PathCanvas::centerPath); toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr(" Snap X: "))); m_snapX = pathInteger(1, 999, 30); toolbar->addWidget(m_snapX);
    toolbar->addWidget(new QLabel(tr(" Snap Y: "))); m_snapY = pathInteger(1, 999, 30); toolbar->addWidget(m_snapY); toolbar->addSeparator();
    auto *grid = action(QStringLiteral(":/images/room/grid.png"), tr("Show grid")); grid->setCheckable(true); grid->setChecked(true); connect(grid, &QAction::toggled, m_canvas, &PathCanvas::setGridVisible);
    m_roomButton = new QToolButton; m_roomButton->setObjectName(QStringLiteral("pathRoomButton")); m_roomButton->setText(tr("< Select Room Background >")); m_roomButton->setFixedHeight(23);
    toolbar->addWidget(m_roomButton);
    connect(m_roomButton, &QToolButton::clicked, this, [this] {
        const auto resources = ActionXml::resourceList(*m_project, ResourceType::Room);
        const int current = m_document->state().backgroundRoom;
        const QString selectedName = current >= 0 && current < resources.size() ? resources.at(current).name : QString();
        ResourceSelectionMenu menu(m_project->resources(ResourceType::Room), selectedName, tr("< No Room Background >"), QString(), this);
        const QAction *selected = menu.exec(m_roomButton->mapToGlobal(QPoint(0, m_roomButton->height())));
        if (!selected || !selected->data().isValid()) return;
        int roomIndex = -1;
        for (int i = 0; i < resources.size(); ++i) if (resources.at(i).name == selected->data().toString()) roomIndex = i;
        QString error;
        if (!loadRoomPreview(roomIndex, error)) { EditorMessageBox::warning(this, tr("Cannot Load Room Background"), error); return; }
        auto state = m_document->state(); state.backgroundRoom = roomIndex;
        m_document->edit(state, tr("Change path room background"), m_document->selectedPoint());
    });
    auto *splitter = new QSplitter(Qt::Horizontal); splitter->setHandleWidth(4); splitter->setChildrenCollapsible(false); layout->addWidget(splitter, 1);
    auto *sidebar = new QWidget; auto *left = new QVBoxLayout(sidebar); left->setSizeConstraint(QLayout::SetMinimumSize); left->setContentsMargins(8, 8, 3, 8); left->setSpacing(7);
    auto *nameRow = new QHBoxLayout; nameRow->setSpacing(6); auto *name = new QLineEdit(document->name()); enableResourceRenaming(name, ResourceType::Path); name->setFixedHeight(20); nameRow->addWidget(new QLabel(tr("Name:"))); nameRow->addWidget(name); left->addLayout(nameRow);
    m_points->setMinimumHeight(70); m_points->setUniformItemSizes(true); left->addWidget(m_points, 1);
    auto *pointControls = new QGridLayout; pointControls->setHorizontalSpacing(6); pointControls->setVerticalSpacing(8); left->addLayout(pointControls);
    m_x = pathNumber(sidebar, -1e9, 1e9); m_y = pathNumber(sidebar, -1e9, 1e9); m_speed = pathNumber(sidebar, 0, 1e9); m_speed->setValue(100);
    const QStringList labels = {tr("X:"), tr("Y:"), tr("sp:")}; const QList<QDoubleSpinBox *> edits = {m_x, m_y, m_speed}; const QStringList buttonTitles = {tr("Add"), tr("Insert"), tr("Delete")};
    for (int i = 0; i < 3; ++i) {
        pointControls->addWidget(new QLabel(labels.at(i)), i, 0); pointControls->addWidget(edits.at(i), i, 1);
        auto *button = new QPushButton(buttonTitles.at(i)); button->setAutoDefault(false); button->setFixedSize(63, 23); pointControls->addWidget(button, i, 3);
        if (i == 2) { m_deleteButton = button; connect(button, &QPushButton::clicked, m_canvas, &PathCanvas::deleteSelected); }
        else connect(button, &QPushButton::clicked, this, [this, i] { addPoint(i == 1); });
        connect(edits.at(i), &QDoubleSpinBox::editingFinished, this, &PathPropertiesWindow::editPoint);
    }
    pointControls->setColumnStretch(2, 1);
    auto *kindRow = new QHBoxLayout; kindRow->setSpacing(7); left->addLayout(kindRow); auto *kind = new QGroupBox(tr("Connection Kind"));
    auto *kindLayout = new QVBoxLayout(kind); kindLayout->setContentsMargins(5, 9, 5, 5); kindLayout->setSpacing(2);
    m_straight = new QRadioButton(tr("Straight lines")); m_smooth = new QRadioButton(tr("Smooth curve")); kindLayout->addWidget(m_straight); kindLayout->addWidget(m_smooth);
    m_closed = new QCheckBox(tr("Closed")); kindRow->addWidget(kind); kindRow->addWidget(m_closed, 0, Qt::AlignVCenter); kindRow->addStretch();
    // The splitter must respect the complete controls, including the Closed label.
    kind->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_closed->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    auto *precisionRow = new QHBoxLayout; precisionRow->setSpacing(7); precisionRow->addWidget(new QLabel(tr("Precision:"))); m_precision = pathInteger(1, 8, 40); precisionRow->addWidget(m_precision); precisionRow->addStretch(); left->addLayout(precisionRow);
    auto updateSettings = [this] { if (m_refreshing) return; m_canvas->finishInteraction(); auto state = m_document->state(); state.smooth = m_smooth->isChecked(); state.closed = m_closed->isChecked(); state.precision = m_precision->value(); state.snapX = m_snapX->value(); state.snapY = m_snapY->value(); m_document->edit(state, tr("Change path settings"), m_document->selectedPoint()); };
    connect(m_smooth, &QRadioButton::toggled, this, updateSettings); connect(m_closed, &QCheckBox::toggled, this, updateSettings);
    for (auto *edit : {m_precision, m_snapX, m_snapY}) connect(edit, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, updateSettings);
    auto *right = new QWidget; auto *rightLayout = new QVBoxLayout(right); rightLayout->setContentsMargins(0, 0, 0, 0); rightLayout->setSpacing(0); rightLayout->addWidget(m_canvas, 1);
    m_status = new QLabel; m_status->setFixedHeight(19); m_status->setStyleSheet(QStringLiteral("background: #131313; color: white;")); rightLayout->addWidget(m_status);
    splitter->addWidget(sidebar); splitter->addWidget(right); splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1); splitter->setSizes({174, 670});
    connect(m_points, &QListWidget::currentRowChanged, this, [this](int row) { if (!m_refreshing) m_document->selectPoint(row); });
    connect(document, &PathDocument::changed, this, &PathPropertiesWindow::refresh); connect(document, &PathDocument::selectionChanged, this, &PathPropertiesWindow::refreshSelection);
    connect(document->undoStack(), &QUndoStack::cleanChanged, this, &PathPropertiesWindow::updateTitle);
    connect(document, &PathDocument::saved, this, [this] { emit resourceSaved(ResourceType::Path, filePath(), QString()); });
    connect(m_canvas, &PathCanvas::viewChanged, this, &PathPropertiesWindow::updateStatus);
    connect(m_canvas, &PathCanvas::cursorMoved, this, [this](const QPointF &position) { m_cursor = position; updateStatus(); });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this); connect(saveShortcut, &QShortcut::activated, this, &PathPropertiesWindow::saveProjectRequested);
    auto *undoShortcut = new QShortcut(QKeySequence::Undo, this), *redoShortcut = new QShortcut(QKeySequence::Redo, this);
    connect(undoShortcut, &QShortcut::activated, this, [this] { m_canvas->finishInteraction(); m_canvas->setFocus(); m_document->undoStack()->undo(); });
    connect(redoShortcut, &QShortcut::activated, this, [this] { m_canvas->finishInteraction(); m_canvas->setFocus(); m_document->undoStack()->redo(); });
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, m_points); deleteShortcut->setContext(Qt::WidgetShortcut); connect(deleteShortcut, &QShortcut::activated, m_canvas, &PathCanvas::deleteSelected);
    m_canvas->setToolTip(tr("Left click: add/select/drag point\nRight click: delete point\nAlt: no snap\nMiddle mouse or Space + drag: pan\nWheel: zoom"));
    refresh();
}
PathPropertiesWindow::~PathPropertiesWindow()
{
    for (QObject *child : findChildren<QObject *>()) QObject::disconnect(child, nullptr, this, nullptr);
    delete takeCentralWidget(); delete m_roomPreview; delete m_roomDocument;
}
void PathPropertiesWindow::updateTitle() { setWindowTitle(tr("Path Properties: %1%2").arg(m_document->name(), m_document->isModified() ? QStringLiteral(" *") : QString())); }
void PathPropertiesWindow::refresh()
{
    m_refreshing = true; const auto &state = m_document->state(); const QSignalBlocker blocker(m_points);
    while (m_points->count() > state.points.size()) delete m_points->takeItem(m_points->count() - 1);
    for (int i = 0; i < state.points.size(); ++i) {
        const auto &point = state.points.at(i); const QString text = QStringLiteral("%1, %2, %3").arg(QString::number(point.position.x(), 'g', 12), QString::number(point.position.y(), 'g', 12), QString::number(point.speed, 'g', 12));
        if (i == m_points->count()) m_points->addItem(text); else m_points->item(i)->setText(text);
    }
    m_straight->setChecked(!state.smooth); m_smooth->setChecked(state.smooth); m_closed->setChecked(state.closed); m_precision->setValue(state.precision); m_snapX->setValue(state.snapX); m_snapY->setValue(state.snapY);
    const auto rooms = ActionXml::resourceList(*m_project, ResourceType::Room);
    m_roomButton->setText(state.backgroundRoom < 0 ? tr("< Select Room Background >") : state.backgroundRoom < rooms.size() ? rooms.at(state.backgroundRoom).name : tr("< Missing room %1 >").arg(state.backgroundRoom));
    QString error;
    if (!loadRoomPreview(state.backgroundRoom, error)) {
        m_canvas->setRoomPreview(nullptr); delete m_roomPreview; m_roomPreview = nullptr; delete m_roomDocument; m_roomDocument = nullptr; m_previewIndex = -2;
    }
    m_roomButton->setToolTip(error);
    m_refreshing = false; refreshSelection(); updateTitle(); updateStatus();
}
void PathPropertiesWindow::refreshSelection()
{
    const int selected = m_document->selectedPoint(); const QSignalBlocker blocker(m_points); m_points->setCurrentRow(selected); m_deleteButton->setEnabled(selected >= 0);
    if (selected < 0) return; const auto &point = m_document->state().points.at(selected);
    const QSignalBlocker x(m_x), y(m_y), speed(m_speed); m_x->setValue(point.position.x()); m_y->setValue(point.position.y()); m_speed->setValue(point.speed);
}
void PathPropertiesWindow::updateStatus()
{
    const auto area = m_canvas->visibleArea(); m_status->setText(tr(" x: %1   y: %2    Area: (%3,%4) -> (%5,%6)")
        .arg(m_cursor.x(), 0, 'f', 0).arg(m_cursor.y(), 0, 'f', 0).arg(area.left(), 0, 'f', 0).arg(area.top(), 0, 'f', 0).arg(area.right(), 0, 'f', 0).arg(area.bottom(), 0, 'f', 0));
}
void PathPropertiesWindow::addPoint(bool insert)
{
    m_canvas->finishInteraction(); auto state = m_document->state(); const int selected = m_document->selectedPoint();
    PathPoint point; point.position = QPointF(m_x->value(), m_y->value()); point.speed = m_speed->value();
    const int index = insert && selected >= 0 ? selected : state.points.size(); state.points.insert(index, point); m_document->edit(state, tr("Add path point"), index);
}
void PathPropertiesWindow::editPoint()
{
    if (m_refreshing || m_document->selectedPoint() < 0) return;
    auto state = m_document->state(); auto &point = state.points[m_document->selectedPoint()];
    // Preserve unedited coordinates at their original precision.
    if (sender() == m_x && m_x->value() != QString::number(point.position.x(), 'f', m_x->decimals()).toDouble()) point.position.setX(m_x->value());
    else if (sender() == m_y && m_y->value() != QString::number(point.position.y(), 'f', m_y->decimals()).toDouble()) point.position.setY(m_y->value());
    else if (sender() == m_speed && m_speed->value() != QString::number(point.speed, 'f', m_speed->decimals()).toDouble()) point.speed = m_speed->value();
    m_document->edit(state, tr("Edit path point"), m_document->selectedPoint());
}

void PathPropertiesWindow::transformPath(const QString &operation)
{
    m_canvas->finishInteraction(); auto state = m_document->state(); if (state.points.isEmpty()) return;
    int selected = m_document->selectedPoint(); QString description;
    if (operation == QStringLiteral("clear")) {
        if (EditorMessageBox::question(this, tr("Clear Path"), tr("Delete all points in this path?"), EditorMessageBox::Yes | EditorMessageBox::No, EditorMessageBox::No) != EditorMessageBox::Yes) return;
        state.points.clear(); selected = -1; description = tr("Clear path");
    } else if (operation == QStringLiteral("reverse")) {
        std::reverse(state.points.begin(), state.points.end()); if (selected >= 0) selected = state.points.size() - 1 - selected; description = tr("Reverse path");
    } else {
        double x = 0, y = 0;
        if (operation == QStringLiteral("shift") || operation == QStringLiteral("rotate") || operation == QStringLiteral("scale")) {
            const bool scale = operation == QStringLiteral("scale"), rotate = operation == QStringLiteral("rotate");
            EditorDialog dialog(this); dialog.setWindowTitle(scale ? tr("Scale Path") : rotate ? tr("Rotate Path") : tr("Shift Path"));
            auto *form = new QFormLayout(dialog.bodyWidget()); auto *first = pathNumber(dialog.bodyWidget(), -1e9, 1e9), *second = pathNumber(dialog.bodyWidget(), -1e9, 1e9);
            first->setMinimumWidth(120); second->setMinimumWidth(120); first->setValue(scale ? 100 : 0); second->setValue(scale ? 100 : 0);
            form->addRow(rotate ? tr("Angle (counter-clockwise):") : scale ? tr("Horizontal (%):") : tr("Horizontal offset:"), first);
            if (!rotate) form->addRow(scale ? tr("Vertical (%):") : tr("Vertical offset:"), second); else second->hide();
            auto *buttons = new QHBoxLayout; auto *ok = new QPushButton(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("OK")); auto *cancel = new QPushButton(QIcon(QStringLiteral(":/object/controls/delete.png")), tr("Cancel"));
            buttons->addWidget(ok); buttons->addStretch(); buttons->addWidget(cancel); form->addRow(buttons);
            connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept); connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted) return; first->interpretText(); second->interpretText(); x = first->value(); y = second->value();
        }
        qreal left = state.points.first().position.x(), right = left, top = state.points.first().position.y(), bottom = top;
        for (const auto &point : state.points) { left = qMin(left, point.position.x()); right = qMax(right, point.position.x()); top = qMin(top, point.position.y()); bottom = qMax(bottom, point.position.y()); }
        const QPointF center((left + right) / 2, (top + bottom) / 2); const double angle = x * 3.14159265358979323846 / 180;
        for (auto &point : state.points) {
            QPointF p = point.position - center;
            if (operation == QStringLiteral("shift")) { point.position += QPointF(x, y); description = tr("Shift path"); continue; }
            if (operation == QStringLiteral("mirror")) { p.setX(-p.x()); description = tr("Mirror path"); }
            else if (operation == QStringLiteral("flip")) { p.setY(-p.y()); description = tr("Flip path"); }
            else if (operation == QStringLiteral("scale")) { p = QPointF(p.x() * x / 100, p.y() * y / 100); description = tr("Scale path"); }
            else if (operation == QStringLiteral("rotate")) { p = QPointF(p.x() * std::cos(angle) + p.y() * std::sin(angle), -p.x() * std::sin(angle) + p.y() * std::cos(angle)); description = tr("Rotate path"); }
            point.position = center + p;
        }
    }
    QString error; if (!PathDocument::validate(state, error)) { EditorMessageBox::warning(this, tr("Cannot Transform Path"), error); return; }
    m_document->edit(state, description, selected);
}
bool PathPropertiesWindow::loadRoomPreview(int roomIndex, QString &error)
{
    if (m_previewIndex == roomIndex) return true;
    if (roomIndex < 0) {
        m_canvas->setRoomPreview(nullptr); delete m_roomPreview; m_roomPreview = nullptr; delete m_roomDocument; m_roomDocument = nullptr; m_previewIndex = roomIndex; return true;
    }
    const auto rooms = ActionXml::resourceList(*m_project, ResourceType::Room);
    if (roomIndex >= rooms.size()) { error = tr("Room background %1 is missing.").arg(roomIndex); return false; }
    const QString path = rooms.at(roomIndex).filePath;
    auto *document = new RoomDocument(this);
    if (!document->load(path, error)) { delete document; return false; }
    auto *preview = new RoomCanvas(document, m_assets.data(), this); preview->hide();
    m_canvas->setRoomPreview(preview); delete m_roomPreview; delete m_roomDocument; m_roomPreview = preview; m_roomDocument = document; m_previewIndex = roomIndex; return true;
}
void PathPropertiesWindow::updateResources()
{
    m_canvas->setRoomPreview(nullptr); delete m_roomPreview; m_roomPreview = nullptr; delete m_roomDocument; m_roomDocument = nullptr;
    m_previewIndex = -2; refresh();
}
bool PathPropertiesWindow::save()
{
    m_canvas->finishInteraction();
    for (auto *edit : {m_x, m_y, m_speed}) if (edit->hasFocus()) {
        edit->interpretText(); const int selected = m_document->selectedPoint(); if (selected < 0) break;
        auto state = m_document->state(); auto &point = state.points[selected];
        double previous = edit == m_x ? point.position.x() : edit == m_y ? point.position.y() : point.speed;
        if (edit->value() != QString::number(previous, 'f', edit->decimals()).toDouble()) {
            if (edit == m_x) point.position.setX(edit->value()); else if (edit == m_y) point.position.setY(edit->value()); else point.speed = edit->value();
            m_document->edit(state, tr("Edit path point"), selected);
        }
    }
    for (auto *edit : {m_precision, m_snapX, m_snapY}) edit->interpretText();
    QString error; if (m_document->save(error)) return true;
    EditorMessageBox::warning(this, tr("Cannot Save Path"), error); return false;
}
QString PathPropertiesWindow::filePath() const { return m_document->filePath(); }
void PathPropertiesWindow::relocate(const QString &oldDirectory, const QString &newDirectory) { m_document->relocate(oldDirectory, newDirectory); updateResources(); }
void PathPropertiesWindow::closeEvent(QCloseEvent *event)
{
    m_canvas->finishInteraction(); m_canvas->setFocus();
    if (m_document->isModified()) {
        const auto answer = EditorMessageBox::question(this, tr("Path Properties"), tr("Save changes to %1?").arg(m_document->name()), EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel || (answer == EditorMessageBox::Save && !save())) { event->ignore(); return; }
    }
    event->accept();
}
