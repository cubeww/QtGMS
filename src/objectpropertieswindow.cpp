#include "resourceselector.h"
#include <QPersistentModelIndex>
#include <QTimer>
#include "editordialog.h"
#include "editorstandarddialogs.h"
#include "actionxml.h"
#include "objectpropertieswindow.h"
#include "objectdocument.h"
#include "objecteventdialog.h"
#include "actionlibrary.h"
#include "actioneditorpanel.h"
#include "actioneditordialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStylePainter>
#include <QStyleOptionComboBox>
#include <QTableWidget>
#include <QToolButton>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include <cmath>

class ObjectSpriteComboBox : public ResourceComboBox
{
protected:
    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this);
        QStyleOptionComboBox option; initStyleOption(&option);
        // The icon is already shown by the separate sprite preview on the left.
        // Keep it in the model for that preview, but draw only the resource name.
        option.currentIcon = QIcon();
        painter.drawComplexControl(QStyle::CC_ComboBox, option);
        painter.drawControl(QStyle::CE_ComboBoxLabel, option);
    }
};

static bool sameObjectEvent(QDomElement a, QDomElement b)
{
    return a.attribute(QStringLiteral("eventtype")) == b.attribute(QStringLiteral("eventtype"))
        && a.attribute(QStringLiteral("enumb")) == b.attribute(QStringLiteral("enumb"))
        && a.attribute(QStringLiteral("ename")) == b.attribute(QStringLiteral("ename"));
}
ObjectPropertiesWindow::ObjectPropertiesWindow(ObjectDocument *document, Project *project, ActionLibraryManager *libraries, QWidget *parent)
    : ResourceEditorWindow(parent), m_document(document), m_project(project), m_libraries(libraries),
      m_events(new QListWidget), m_actionEditor(new ActionEditorPanel(project, libraries, document->undoStack())),
      m_sprite(new ObjectSpriteComboBox), m_parent(new ResourceComboBox), m_mask(new ResourceComboBox), m_depth(new QSpinBox), m_spritePreview(new QLabel), m_children(new QListWidget)
{
    document->setParent(this); setAttribute(Qt::WA_DeleteOnClose); editorMenuBar()->hide(); resize(814, 378); setMinimumSize(720, 376);
    auto *body = new QWidget; auto *layout = new QHBoxLayout(body); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(3);
    auto *properties = new QWidget; properties->setObjectName(QStringLiteral("objectProperties")); properties->setFixedWidth(165); properties->setMinimumHeight(342);
    properties->setStyleSheet(QStringLiteral("QCheckBox::indicator { width: 10px; height: 10px; } QCheckBox { spacing: 3px; } QLineEdit, QSpinBox { border: 1px solid #777773; padding: 0; } QComboBox { border: 1px solid #aaaaa0; padding: 0; background: #383838; } QComboBox::drop-down { width: 0; border: none; } QComboBox::down-arrow { image: none; }"));
    const auto place = [properties](QWidget *widget, int x, int y, int width, int height) { widget->setParent(properties); widget->setGeometry(x, y, width, height); };
    auto *name = new QLineEdit(document->name()); enableResourceRenaming(name, ResourceType::Object); auto *nameLabel = new QLabel(tr("&Name:")); nameLabel->setBuddy(name);
    place(nameLabel, 5, 8, 39, 19); place(name, 46, 8, 113, 19);
    auto *spriteGroup = new QGroupBox(tr("Sprite")); place(spriteGroup, 5, 32, 154, 74);
    m_spritePreview->setParent(spriteGroup); m_spritePreview->setGeometry(6, 20, 17, 17);
    m_sprite->setParent(spriteGroup); m_sprite->setGeometry(30, 19, 89, 20); m_sprite->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto picker = [this](QWidget *parent, QComboBox *combo, int x, int y) {
        auto *button = new QToolButton(parent); button->setGeometry(x, y, 22, 21); button->setAutoRaise(true); button->setIcon(QIcon(QStringLiteral(":/object/controls/selectresource.png"))); button->setIconSize(QSize(16, 16));
        button->setToolTip(tr("Select resource")); connect(button, &QToolButton::clicked, combo, &QComboBox::showPopup);
    };
    picker(spriteGroup, m_sprite, 122, 18);
    auto *newSprite = new QPushButton(tr("New"), spriteGroup); newSprite->setGeometry(13, 46, 58, 20);
    auto *editSprite = new QPushButton(tr("Edit"), spriteGroup); editSprite->setGeometry(84, 46, 57, 20);
    const QStringList flagTags = {QStringLiteral("visible"), QStringLiteral("solid"), QStringLiteral("persistent"), QStringLiteral("PhysicsObject")};
    const QStringList flagNames = {tr("Visible"), tr("Solid"), tr("Persistent"), tr("Uses Physics")};
    for (int i = 0; i < flagTags.size(); ++i) {
        auto *check = new QCheckBox(flagNames.at(i)); m_flags.insert(flagTags.at(i), check); place(check, i % 2 ? 80 : 5, i / 2 ? 135 : 112, i % 2 ? 82 : 72, 17);
        connect(check, &QCheckBox::toggled, this, [this] { applyProperties(); });
    }
    place(new QLabel(tr("Depth:")), 5, 158, 39, 19); place(m_depth, 46, 158, 113, 19);
    m_depth->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max()); m_depth->setKeyboardTracking(false); m_depth->setButtonSymbols(QAbstractSpinBox::NoButtons);
    auto *parentButton = new QPushButton(tr("Parent")); auto *maskButton = new QPushButton(tr("Mask"));
    place(parentButton, 3, 184, 44, 21); place(m_parent, 50, 184, 89, 21); picker(properties, m_parent, 140, 184);
    place(maskButton, 3, 211, 44, 21); place(m_mask, 50, 211, 89, 21); picker(properties, m_mask, 140, 211);
    connect(parentButton, &QPushButton::clicked, this, [this] { for (const ResourceNode &node : ActionXml::resourceList(*m_project, ResourceType::Object)) if (node.name == m_parent->currentData().toString()) emit openResourceRequested(ResourceType::Object, node.filePath); });
    connect(maskButton, &QPushButton::clicked, this, [this] { for (const ResourceNode &node : ActionXml::resourceList(*m_project, ResourceType::Sprite)) if (node.name == m_mask->currentData().toString()) emit openResourceRequested(ResourceType::Sprite, node.filePath); });
    place(new QLabel(tr("Children:")), 3, 239, 44, 17); place(m_children, 46, 238, 113, 43);
    auto *information = new QPushButton(QIcon(QStringLiteral(":/object/controls/information.png")), tr("Show Information")); place(information, 16, 289, 131, 23);
    auto *ok = new QPushButton(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("OK")); place(ok, 42, 316, 75, 23);
    auto *physics = new QPushButton(tr("Physics Properties...")); place(physics, 5, 344, 154, 22);
    auto updatePhysics = [properties, physics](bool enabled) { physics->setVisible(enabled); properties->setMinimumHeight(enabled ? 368 : 342); };
    connect(m_flags.value(QStringLiteral("PhysicsObject")), &QCheckBox::toggled, this, updatePhysics);
    updatePhysics(ActionXml::text(document->xml().documentElement(), QStringLiteral("PhysicsObject")).toInt() != 0);
    layout->addWidget(properties);
    auto *splitter = new QSplitter; splitter->setHandleWidth(3); splitter->setChildrenCollapsible(false);
    auto *events = new QWidget; auto *eventLayout = new QVBoxLayout(events); eventLayout->setContentsMargins(4, 9, 4, 8); eventLayout->setSpacing(5);
    eventLayout->addWidget(new QLabel(tr("Events:"))); eventLayout->addWidget(m_events, 1); m_events->setMinimumWidth(155);
    auto *addEvent = new QPushButton(tr("Add Event")); addEvent->setFixedHeight(23); eventLayout->addWidget(addEvent);
    auto *eventButtons = new QHBoxLayout; auto *removeEvent = new QPushButton(tr("Delete")); auto *changeEvent = new QPushButton(tr("Change")); removeEvent->setFixedSize(71, 23); changeEvent->setFixedSize(73, 23); eventButtons->addWidget(removeEvent); eventButtons->addStretch(); eventButtons->addWidget(changeEvent); eventLayout->addLayout(eventButtons);
    splitter->addWidget(events); splitter->addWidget(m_actionEditor); splitter->setSizes({198, 435}); splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1); layout->addWidget(splitter, 1); setCentralWidget(body);
    connect(newSprite, &QPushButton::clicked, this, &ObjectPropertiesWindow::createSpriteRequested);
    connect(editSprite, &QPushButton::clicked, this, [this] {
        for (const ResourceNode &node : ActionXml::resourceList(*m_project, ResourceType::Sprite)) if (node.name == m_sprite->currentData().toString()) { emit openResourceRequested(ResourceType::Sprite, node.filePath); break; }
    });
    connect(m_sprite, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, editSprite] { editSprite->setVisible(m_sprite->currentData().toString() != QStringLiteral("<undefined>") && m_sprite->currentIndex() >= 0); applyProperties(); });
    for (QComboBox *combo : {m_parent, m_mask}) connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this] { applyProperties(); });
    connect(m_depth, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this] { applyProperties(); });
    connect(m_children, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) { emit openResourceRequested(ResourceType::Object, item->data(Qt::UserRole).toString()); });
    connect(physics, &QPushButton::clicked, this, &ObjectPropertiesWindow::editPhysics); connect(information, &QPushButton::clicked, this, &ObjectPropertiesWindow::showInformation);
    connect(ok, &QPushButton::clicked, this, [this] { if (save()) close(); });
    connect(m_events, &QListWidget::currentRowChanged, this, [this, removeEvent, changeEvent](int row) { removeEvent->setEnabled(row >= 0); changeEvent->setEnabled(row >= 0); if (!m_refreshing) refreshActions(); });
    m_events->setToolTip(tr("Double-click an event to edit its first code action, or add one if none exists."));
    connect(m_events, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const QPersistentModelIndex index(m_events->model()->index(m_events->row(item), 0));
        // Committing code rebuilds the event list; finish the mouse event before
        // entering the editor and avoid keeping a pointer to its list item.
        QTimer::singleShot(0, this, [this, index] {
            if (!index.isValid() || m_refreshing) return;
            m_events->setCurrentIndex(index); refreshActions();
            m_actionEditor->openFirstCodeAction();
        });
    });
    connect(addEvent, &QPushButton::clicked, this, [this] { editEvent(false); }); connect(changeEvent, &QPushButton::clicked, this, [this] { editEvent(true); });
    connect(removeEvent, &QPushButton::clicked, this, &ObjectPropertiesWindow::deleteEvent);
    connect(m_actionEditor, &ActionEditorPanel::actionsEdited, this, [this](const QDomElement &container, const QString &description) {
        QDomDocument xml = m_document->xml(); QDomElement event = selectedEvent(xml); if (event.isNull()) return;
        event.parentNode().replaceChild(xml.importNode(container, true), event); m_document->edit(xml, description);
    });
    m_actionEditor->setModelessCodeEditing(true);
    connect(m_actionEditor, &ActionEditorPanel::codeActionRequested, this, &ObjectPropertiesWindow::openCodeAction);
    connect(document, &ObjectDocument::changed, this, &ObjectPropertiesWindow::refresh);
    connect(document, &ObjectDocument::changed, this, &ObjectPropertiesWindow::refreshCodeEditors);
    connect(document->undoStack(), &QUndoStack::cleanChanged, this, [this] { setWindowTitle(tr("Object Properties: %1%2").arg(m_document->name(), m_document->isModified() ? QStringLiteral(" *") : QString())); });
    connect(document, &ObjectDocument::saved, this, [this] {
        const QString sprite = ActionXml::text(m_document->xml().documentElement(), QStringLiteral("spriteName")); QString thumbnail;
        for (const ResourceNode &node : ActionXml::resourceList(*m_project, ResourceType::Sprite)) if (node.name == sprite) { thumbnail = node.thumbnailPath; break; }
        m_project->updateObjectSprite(filePath(), sprite, thumbnail); emit resourceSaved(ResourceType::Object, filePath(), thumbnail);
    });
    m_events->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_events, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this); menu.addAction(tr("Add Event..."), this, [this] { editEvent(false); });
        auto *change = menu.addAction(tr("Change Event..."), this, [this] { editEvent(true); });
        auto *duplicate = menu.addAction(tr("Duplicate Event..."), this, [this] { editEvent(false, true); });
        auto *remove = menu.addAction(tr("Delete Event"), this, &ObjectPropertiesWindow::deleteEvent);
        for (QAction *action : {change, duplicate, remove}) action->setEnabled(m_events->currentRow() >= 0);
        menu.addSeparator(); menu.addAction(m_document->undoStack()->createUndoAction(&menu)); menu.addAction(m_document->undoStack()->createRedoAction(&menu)); menu.exec(m_events->viewport()->mapToGlobal(pos));
    });
    auto *removeShortcut = new QShortcut(QKeySequence::Delete, m_events); removeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(removeShortcut, &QShortcut::activated, this, &ObjectPropertiesWindow::deleteEvent);
    auto *undo = new QShortcut(QKeySequence::Undo, this); auto *redo = new QShortcut(QKeySequence::Redo, this); auto *save = new QShortcut(QKeySequence::Save, this);
    connect(undo, &QShortcut::activated, document->undoStack(), &QUndoStack::undo); connect(redo, &QShortcut::activated, document->undoStack(), &QUndoStack::redo); connect(save, &QShortcut::activated, this, &ObjectPropertiesWindow::saveProjectRequested);
    refreshResources(); refresh(); editSprite->setVisible(m_sprite->currentData().toString() != QStringLiteral("<undefined>") && m_sprite->currentIndex() >= 0); removeEvent->setEnabled(m_events->currentRow() >= 0); changeEvent->setEnabled(m_events->currentRow() >= 0);
}
void ObjectPropertiesWindow::refreshResources()
{
    m_refreshing = true; const QDomElement root = m_document->xml().documentElement();
    m_sprite->setResources(*m_project, ResourceType::Sprite, ActionXml::text(root, QStringLiteral("spriteName")), tr("<no sprite>"));
    m_mask->setResources(*m_project, ResourceType::Sprite, ActionXml::text(root, QStringLiteral("maskName")), tr("<same as sprite>"));
    m_parent->setResources(*m_project, ResourceType::Object, ActionXml::text(root, QStringLiteral("parentName")), tr("<no parent>"), QStringLiteral("<undefined>"), {m_document->name()});
    m_children->clear();
    for (const ResourceNode &node : ActionXml::resourceList(*m_project, ResourceType::Object)) {
        QFile file(node.filePath); QDomDocument xml;
        if (file.open(QIODevice::ReadOnly) && xml.setContent(&file) && ActionXml::text(xml.documentElement(), QStringLiteral("parentName")) == m_document->name()) {
            auto *item = new QListWidgetItem(node.name, m_children); item->setData(Qt::UserRole, node.filePath);
        }
    }
    m_refreshing = false;
}
void ObjectPropertiesWindow::refresh()
{
    m_refreshing = true; const QDomElement root = m_document->xml().documentElement();
    setWindowTitle(tr("Object Properties: %1%2").arg(m_document->name(), m_document->isModified() ? QStringLiteral(" *") : QString()));
    m_sprite->setCurrentIndex(m_sprite->findData(ActionXml::text(root, QStringLiteral("spriteName"))));
    m_parent->setCurrentIndex(m_parent->findData(ActionXml::text(root, QStringLiteral("parentName"))));
    m_mask->setCurrentIndex(m_mask->findData(ActionXml::text(root, QStringLiteral("maskName")))); m_depth->setValue(ActionXml::text(root, QStringLiteral("depth")).toInt());
    for (auto it = m_flags.constBegin(); it != m_flags.constEnd(); ++it) it.value()->setChecked(ActionXml::text(root, it.key()).toInt() != 0);
    m_spritePreview->setPixmap(m_sprite->itemIcon(m_sprite->currentIndex()).pixmap(17, 17));
    const int row = m_events->currentRow(); m_events->clear();
    for (QDomElement event : ActionXml::elements(root.firstChildElement(QStringLiteral("events")), QStringLiteral("event"))) {
        auto *item = new QListWidgetItem(ObjectEventDialog::eventName(event), m_events); item->setSizeHint(QSize(0, 20));
        item->setIcon(ObjectEventDialog::eventIcon(event));
    }
    if (m_events->count()) m_events->setCurrentRow(qBound(0, row, m_events->count() - 1));
    m_refreshing = false; refreshActions();
}
QDomElement ObjectPropertiesWindow::selectedEvent(QDomDocument &xml) const
{
    const auto events = ActionXml::elements(xml.documentElement().firstChildElement(QStringLiteral("events")), QStringLiteral("event")); const int row = m_events->currentRow();
    return row >= 0 && row < events.size() ? events.at(row) : QDomElement();
}
void ObjectPropertiesWindow::refreshActions()
{
    QDomDocument xml = m_document->xml(); const QDomElement event = selectedEvent(xml);
    m_actionEditor->setActions(event, m_document->name() + QLatin1Char('_') + ObjectEventDialog::eventName(event));
}
void ObjectPropertiesWindow::showAction(int eventIndex, int actionIndex, int offset, int length, int argument)
{
    if (eventIndex < 0 || eventIndex >= m_events->count()) return;
    m_events->setCurrentRow(eventIndex);
    refreshActions();
    m_actionEditor->openAction(actionIndex, offset, length, argument);
}
void ObjectPropertiesWindow::applyProperties()
{
    if (m_refreshing) return; QDomDocument xml = m_document->xml(); QDomElement root = xml.documentElement();
    for (auto it = m_flags.constBegin(); it != m_flags.constEnd(); ++it) ActionXml::setText(root, it.key(), it.value()->isChecked() ? QStringLiteral("-1") : QStringLiteral("0"));
    if (m_sprite->currentIndex() >= 0) ActionXml::setText(root, QStringLiteral("spriteName"), m_sprite->currentData().toString());
    if (m_parent->currentIndex() >= 0) ActionXml::setText(root, QStringLiteral("parentName"), m_parent->currentData().toString());
    if (m_mask->currentIndex() >= 0) ActionXml::setText(root, QStringLiteral("maskName"), m_mask->currentData().toString());
    ActionXml::setText(root, QStringLiteral("depth"), QString::number(m_depth->value())); m_document->edit(xml, tr("Change object properties"));
}
void ObjectPropertiesWindow::editEvent(bool change, bool duplicate)
{
    QDomDocument xml = m_document->xml(); QDomElement previous = selectedEvent(xml);
    if ((change || duplicate) && previous.isNull()) return;
    ObjectEventDialog dialog(*m_project, this); if (change) dialog.selectEvent(previous);
    if (dialog.exec() != QDialog::Accepted) return; QDomElement event = dialog.event(xml); if (event.isNull()) return;
    QDomElement events = ActionXml::child(xml.documentElement(), QStringLiteral("events"));
    for (QDomElement existing : ActionXml::elements(events, QStringLiteral("event"))) {
        if (sameObjectEvent(existing, event) && (!change || existing != previous)) { EditorMessageBox::information(this, tr("Object Event"), tr("This event already exists.")); return; }
    }
    int row = m_events->currentRow();
    if (change || duplicate) {
        QDomElement copy = previous.cloneNode(true).toElement(); copy.setAttribute(QStringLiteral("eventtype"), event.attribute(QStringLiteral("eventtype")));
        copy.removeAttribute(QStringLiteral("enumb")); copy.removeAttribute(QStringLiteral("ename"));
        const QString key = event.hasAttribute(QStringLiteral("ename")) ? QStringLiteral("ename") : QStringLiteral("enumb"); copy.setAttribute(key, event.attribute(key)); event = copy;
    }
    if (change) events.replaceChild(event, previous); else { row = ActionXml::elements(events, QStringLiteral("event")).size(); events.appendChild(event); }
    if (duplicate) ActionXml::clearEditorIds(event);
    m_document->edit(xml, change ? tr("Change event") : duplicate ? tr("Duplicate event") : tr("Add event")); m_events->setCurrentRow(row);
}
void ObjectPropertiesWindow::deleteEvent()
{
    QDomDocument xml = m_document->xml(); QDomElement event = selectedEvent(xml); if (event.isNull()) return;
    if (!event.firstChildElement(QStringLiteral("action")).isNull() && EditorMessageBox::question(this, tr("Delete Event"), tr("Delete this event and its actions?"), EditorMessageBox::Yes | EditorMessageBox::No, EditorMessageBox::No) != EditorMessageBox::Yes) return;
    event.parentNode().removeChild(event); m_document->edit(xml, tr("Delete event"));
}
QString ObjectPropertiesWindow::filePath() const { return m_document->filePath(); }
void ObjectPropertiesWindow::relocate(const QString &oldDirectory, const QString &newDirectory) { m_document->relocate(oldDirectory, newDirectory); }
bool ObjectPropertiesWindow::save()
{
    if (!saveCodeEditors()) return false;
    m_depth->interpretText(); applyProperties(); QString error;
    if (!m_document->save(*m_project, error)) { EditorMessageBox::critical(this, tr("Cannot Save Object"), error); return false; } return true;
}
void ObjectPropertiesWindow::closeEvent(QCloseEvent *event)
{
    if (!closeCodeEditors()) { event->ignore(); return; }
    m_depth->interpretText(); applyProperties();
    if (m_document->isModified()) {
        const auto answer = EditorMessageBox::question(this, tr("Object Properties"), tr("Save changes to %1?").arg(m_document->name()), EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel || (answer == EditorMessageBox::Save && !save())) { event->ignore(); return; }
    }
    event->accept();
}
void ObjectPropertiesWindow::updateResources()
{
    refreshResources(); m_spritePreview->setPixmap(m_sprite->itemIcon(m_sprite->currentIndex()).pixmap(17, 17));
}
void ObjectPropertiesWindow::setSpriteName(const QString &name)
{
    updateResources(); m_sprite->setCurrentIndex(m_sprite->findData(name));
}
void ObjectPropertiesWindow::showInformation()
{
    QDomDocument xml = m_document->xml(); EditorDialog dialog(this); dialog.setWindowTitle(tr("Object Information: %1").arg(m_document->name())); auto *layout = new QVBoxLayout(dialog.bodyWidget()); auto *text = new QTextBrowser;
    QString html = QStringLiteral("<h2>%1</h2>").arg(m_document->name().toHtmlEscaped());
    for (const QString &tag : {QStringLiteral("spriteName"), QStringLiteral("parentName"), QStringLiteral("maskName"), QStringLiteral("depth")}) html += QStringLiteral("<p>%1: %2</p>").arg(tag, ActionXml::text(xml.documentElement(), tag).toHtmlEscaped());
    for (QDomElement event : ActionXml::elements(xml.documentElement().firstChildElement(QStringLiteral("events")), QStringLiteral("event"))) {
        html += QStringLiteral("<h3>%1</h3><ol>").arg(ObjectEventDialog::eventName(event).toHtmlEscaped());
        for (QDomElement action : ActionXml::elements(event, QStringLiteral("action"))) html += QStringLiteral("<li>%1</li>").arg(ActionXml::actionText(action, *m_libraries).toHtmlEscaped()); html += QStringLiteral("</ol>");
    }
    text->setHtml(html); layout->addWidget(text); auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(buttons); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); dialog.resize(600, 500); dialog.exec();
}
void ObjectPropertiesWindow::editPhysics()
{
    QDomDocument xml = m_document->xml(); QDomElement root = xml.documentElement(); EditorDialog dialog(this); dialog.setWindowTitle(tr("Physics Properties"));
    auto *layout = new QVBoxLayout(dialog.bodyWidget()); auto *form = new QFormLayout; auto *shape = new QComboBox;
    shape->addItems({tr("Circle"), tr("Box"), tr("Shape")}); shape->setCurrentIndex(ActionXml::text(root, QStringLiteral("PhysicsObjectShape")).toInt()); form->addRow(tr("Collision Shape:"), shape);
    const QStringList tags = QStringLiteral("Density|Restitution|LinearDamping|AngularDamping|Friction").split(QLatin1Char('|'));
    const QStringList captions = {tr("Density:"), tr("Restitution:"), tr("Linear Damping:"), tr("Angular Damping:"), tr("Friction:")}; QList<QDoubleSpinBox *> values;
    for (int i = 0; i < tags.size(); ++i) {
        auto *spin = new QDoubleSpinBox; spin->setDecimals(6); spin->setRange(0, 1e9); spin->setValue(ActionXml::text(root, QStringLiteral("PhysicsObject") + tags.at(i)).toDouble()); values.append(spin); form->addRow(captions.at(i), spin);
    }
    auto *group = new QSpinBox; group->setRange(-32768, 32767); group->setValue(ActionXml::text(root, QStringLiteral("PhysicsObjectGroup")).toInt()); form->addRow(tr("Collision Group:"), group);
    QList<QCheckBox *> flags; for (const QString &tag : {QStringLiteral("Sensor"), QStringLiteral("Awake"), QStringLiteral("Kinematic")}) {
        auto *check = new QCheckBox(tag); check->setChecked(ActionXml::text(root, QStringLiteral("PhysicsObject") + tag).toInt() != 0); flags.append(check); form->addRow(check);
    }
    layout->addLayout(form); auto *help = new QLabel(tr("Collision shape points:")); layout->addWidget(help);
    auto *points = new QTableWidget(0, 2); points->setHorizontalHeaderLabels({QStringLiteral("X"), QStringLiteral("Y")}); points->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (QDomElement point : ActionXml::elements(root.firstChildElement(QStringLiteral("PhysicsShapePoints")), QStringLiteral("point"))) {
        const QStringList parts = point.text().split(QLatin1Char(',')); const int row = points->rowCount(); points->insertRow(row);
        for (int c = 0; c < 2; ++c) points->setItem(row, c, new QTableWidgetItem(parts.value(c)));
    }
    layout->addWidget(points); auto *pointButtons = new QHBoxLayout; auto *add = new QPushButton(tr("Add Point")); auto *remove = new QPushButton(tr("Remove Point")); pointButtons->addWidget(add); pointButtons->addWidget(remove); layout->addLayout(pointButtons);
    connect(add, &QPushButton::clicked, &dialog, [points] { const int row = points->rowCount(); points->insertRow(row); for (int c = 0; c < 2; ++c) points->setItem(row, c, new QTableWidgetItem(QStringLiteral("0"))); });
    connect(remove, &QPushButton::clicked, &dialog, [points] { if (points->currentRow() >= 0) points->removeRow(points->currentRow()); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, points] {
        for (int row = 0; row < points->rowCount(); ++row) for (int c = 0; c < 2; ++c) {
            bool ok = false; const double value = points->item(row, c) ? points->item(row, c)->text().toDouble(&ok) : 0;
            if (!ok || !std::isfinite(value)) { EditorMessageBox::warning(&dialog, tr("Collision Shape"), tr("Every coordinate must be a finite number.")); return; }
        }
        dialog.accept();
    });
    dialog.resize(420, 640); if (dialog.exec() != QDialog::Accepted) return;
    ActionXml::setText(root, QStringLiteral("PhysicsObjectShape"), QString::number(shape->currentIndex())); ActionXml::setText(root, QStringLiteral("PhysicsObjectGroup"), QString::number(group->value()));
    for (int i = 0; i < tags.size(); ++i) {
        const QString tag = QStringLiteral("PhysicsObject") + tags.at(i); const double old = ActionXml::text(root, tag).toDouble();
        if (std::abs(values.at(i)->value() - old) > 0.0000005) ActionXml::setText(root, tag, QString::number(values.at(i)->value(), 'g', 15));
    }
    const QStringList flagTags = {QStringLiteral("Sensor"), QStringLiteral("Awake"), QStringLiteral("Kinematic")};
    for (int i = 0; i < flags.size(); ++i) ActionXml::setText(root, QStringLiteral("PhysicsObject") + flagTags.at(i), flags.at(i)->isChecked() ? QStringLiteral("-1") : QStringLiteral("0"));
    QDomElement vertices = ActionXml::child(root, QStringLiteral("PhysicsShapePoints"));
    for (QDomElement old : ActionXml::elements(vertices, QStringLiteral("point"))) vertices.removeChild(old);
    for (int row = 0; row < points->rowCount(); ++row) { auto point = xml.createElement(QStringLiteral("point")); point.appendChild(xml.createTextNode(points->item(row, 0)->text() + QLatin1Char(',') + points->item(row, 1)->text())); vertices.appendChild(point); }
    m_document->edit(xml, tr("Change physics properties"));
}
