#include "resourceselector.h"
#include "actionxml.h"
#include "objecteventdialog.h"
#include "objecteventtranslations.h"
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include "editortheme.h"
#include <QMenu>
#include <QImage>
#include <QVBoxLayout>

static QJsonObject objectEventCatalog()
{
    static const QJsonObject catalog = [] { QFile file(QStringLiteral(":/object/events.json")); file.open(QIODevice::ReadOnly); return QJsonDocument::fromJson(file.readAll()).object(); }();
    return catalog;
}
static QString objectEventMenu(int type)
{
    switch (type) {
    case 2: return QStringLiteral("AlarmMenu"); case 3: return QStringLiteral("StepMenu"); case 6: return QStringLiteral("MouseMenu");
    case 7: return QStringLiteral("OtherMenu"); case 8: return QStringLiteral("DrawMenu");
    case 5: case 9: case 10: return QStringLiteral("KeyBoardMenu"); default: return QString();
    }
}
static QString eventCaption(const QJsonArray &items, int id)
{
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject(); const auto children = item.value(QStringLiteral("children")).toArray();
        if (children.isEmpty() && item.value(QStringLiteral("id")).toInt() == id) return translatedObjectEventCaption(item.value(QStringLiteral("caption")).toString());
        const QString nested = eventCaption(children, id); if (!nested.isEmpty()) return nested;
    }
    return QString();
}
static QString objectEventTypeName(int type)
{
    const QStringList names = QStringLiteral("Create|Destroy|Alarm|Step|Collision|Keyboard|Mouse|Other|Draw|Key Press|Key Release|Trigger|Clean Up").split(QLatin1Char('|'));
    return type >= 0 && type < names.size() ? translatedObjectEventCaption(names.at(type)) : QObject::tr("Event %1").arg(type);
}
QString ObjectEventDialog::eventName(QDomElement event)
{
    const int type = event.attribute(QStringLiteral("eventtype")).toInt(), id = event.attribute(QStringLiteral("enumb")).toInt();
    if (type == 4) return QObject::tr("Collision with %1").arg(event.attribute(QStringLiteral("ename")));
    if ((type == 0 || type == 1 || type == 12) && id == 0) return objectEventTypeName(type);
    QString caption = eventCaption(objectEventCatalog().value(objectEventMenu(type)).toArray(), id);
    if (type == 7 && id >= 60) { caption = eventCaption(objectEventCatalog().value(QStringLiteral("WebMenu")).toArray(), id); if (!caption.isEmpty()) return QObject::tr("Async: %1").arg(caption); }
    if (caption.isEmpty()) return objectEventTypeName(type) + QStringLiteral(" %1").arg(id);
    if (type == 5 || type == 9 || type == 10) return objectEventTypeName(type) + QStringLiteral(": ") + caption;
    return caption;
}
QIcon ObjectEventDialog::eventIcon(QDomElement event)
{
    const int type = event.attribute(QStringLiteral("eventtype")).toInt();
    const QStringList icons = QStringLiteral("create|destroy|alarm|step|collision|keyboard|mouse|other|draw|keypress|keyrelease").split(QLatin1Char('|'));
    const QString icon = type == 7 && event.attribute(QStringLiteral("enumb")).toInt() >= 60 ? QStringLiteral("async") : icons.value(type, QStringLiteral("other"));
    static QMap<QString, QIcon> iconsByName;
    if (!iconsByName.contains(icon)) {
        QImage image(QStringLiteral(":/object/%1.png").arg(icon));
        // The Create event's light bulb stays yellow, as in the reference.
        if (icon != QStringLiteral("create")) image = EditorTheme::tintActionImage(image);
        iconsByName.insert(icon, QIcon(QPixmap::fromImage(image)));
    }
    return iconsByName.value(icon);
}
static void addEventChoices(QMenu *menu, const QJsonArray &items, int selected)
{
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject(); const auto children = item.value(QStringLiteral("children")).toArray();
        const QString caption = translatedObjectEventCaption(item.value(QStringLiteral("caption")).toString());
        if (caption == QStringLiteral("-")) { menu->addSeparator(); continue; }
        if (!children.isEmpty()) { addEventChoices(menu->addMenu(caption), children, selected); continue; }
        QAction *action = menu->addAction(caption); const int number = item.value(QStringLiteral("id")).toInt(); action->setData(number);
        if (number == selected) { action->setCheckable(true); action->setChecked(true); }
    }
}
ObjectEventDialog::ObjectEventDialog(const Project &project, QWidget *parent)
    : EditorDialog(parent), m_project(project)
{
    setWindowTitle(tr("Choose the Event to Add"));
    setFixedSize(255, 286);
    auto *body = bodyWidget();
    const int categories[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12};
    const QStringList captions = {tr("&Create"), tr("&Destroy"), tr("&Alarm"), tr("&Step"), tr("C&ollision"), tr("&Keyboard"),
        tr("&Mouse"), tr("O&ther"), tr("D&raw"), tr("Key &Press"), tr("Key R&elease"), tr("As&ynchronous")};
    for (int i = 0; i < 12; ++i) {
        const int category = categories[i]; const int type = category == 12 ? 7 : category;
        QDomDocument xml; QDomElement iconEvent = xml.createElement(QStringLiteral("event"));
        iconEvent.setAttribute(QStringLiteral("eventtype"), type); iconEvent.setAttribute(QStringLiteral("enumb"), category == 12 ? 60 : 0);
        auto *button = new QPushButton(eventIcon(iconEvent), captions.at(i), body);
        button->setGeometry(i < 6 ? 10 : 129, 8 + (i % 6) * 32, 112, 23); button->setIconSize(QSize(16, 16));
        button->setProperty("leftAligned", true); button->setAutoDefault(false); m_buttons.insert(category, button);
        connect(button, &QPushButton::clicked, this, [this, category, button] { chooseEvent(category, button); });
    }
    auto *cancel = new QPushButton(QIcon(QStringLiteral(":/object/controls/delete.png")), tr("Cancel"), body);
    cancel->setGeometry(64, 208, 119, 25); cancel->setAutoDefault(false); connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_buttons.value(0)->setFocus();
    if (parent) move(parent->mapToGlobal(parent->rect().center()) - rect().center());
}
void ObjectEventDialog::chooseEvent(int category, QPushButton *button)
{
    if (category == 0 || category == 1) { m_type = category; m_number = 0; m_collisionObject.clear(); accept(); return; }
    const int type = category == 12 ? 7 : category; QMenu menu(this);
    if (category == 4) {
        ResourceSelectionMenu resources(m_project.resources(ResourceType::Object), m_type == 4 ? m_collisionObject : QString(), QString(), QString(), this);
        const QAction *selected = resources.exec(button->mapToGlobal(QPoint(button->width(), 0)));
        if (!selected || !selected->data().isValid()) return;
        m_type = 4; m_number = 0; m_collisionObject = selected->data().toString(); accept(); return;
    }
    const QString catalog = category == 12 ? QStringLiteral("WebMenu") : objectEventMenu(category);
    addEventChoices(&menu, objectEventCatalog().value(catalog).toArray(), m_type == type ? m_number : -1);
    QAction *selected = menu.exec(button->mapToGlobal(QPoint(0, button->height())));
    if (!selected || !selected->data().isValid()) return;
    m_type = type; m_number = selected->data().toInt();
    m_collisionObject.clear(); accept();
}
QDomElement ObjectEventDialog::event(QDomDocument &xml) const
{
    if (m_type < 0) return QDomElement();
    QDomElement event = xml.createElement(QStringLiteral("event")); event.setAttribute(QStringLiteral("eventtype"), m_type);
    if (m_type == 4) event.setAttribute(QStringLiteral("ename"), m_collisionObject); else event.setAttribute(QStringLiteral("enumb"), m_number);
    return event;
}
void ObjectEventDialog::selectEvent(QDomElement event)
{
    setWindowTitle(tr("Choose the Event to Change"));
    m_type = event.attribute(QStringLiteral("eventtype")).toInt(); m_number = event.attribute(QStringLiteral("enumb")).toInt(); m_collisionObject = event.attribute(QStringLiteral("ename"));
    const int category = m_type == 7 && m_number >= 60 ? 12 : m_type;
    if (m_buttons.contains(category)) m_buttons.value(category)->setFocus();
}
