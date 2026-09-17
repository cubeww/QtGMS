#include "resourceselector.h"
#include <QKeyEvent>
#include <QPointer>
#include <QSet>
#include <QSignalBlocker>
#include <QWheelEvent>
#include <QImageReader>
#include <QFileInfo>
#include <QDateTime>
#include <QPixmapCache>
#include <QPainter>
#include <QStandardItemModel>
#include <functional>

static QIcon selectionIcon(ResourceType type)
{
    const QStringList names = QStringLiteral("sprite|sound|background|path|script|shader|font|timeline|object|room|includedfile|extension").split(QLatin1Char('|'));
    const QString directory = type == ResourceType::Sprite || type == ResourceType::Background || type == ResourceType::Object ? QStringLiteral(":/images/") : QStringLiteral(":/images/tree/");
    return QIcon(directory + names.value(static_cast<int>(type)) + QStringLiteral(".png"));
}

static QString menuLabel(const QString &name)
{ return QString(name).replace(QLatin1Char('&'), QStringLiteral("&&")); }

static QIcon resourceMenuIcon(const ResourceNode &node)
{
    if (node.type != ResourceType::Sprite && node.type != ResourceType::Background && node.type != ResourceType::Object) return QIcon();
    if (node.thumbnailPath.isEmpty()) return selectionIcon(node.type);
    const QFileInfo file(node.thumbnailPath);
    const QString key = QStringLiteral("resource-menu:%1:%2:%3").arg(node.thumbnailPath)
        .arg(file.lastModified().toMSecsSinceEpoch()).arg(file.size());
    QPixmap thumbnail;
    if (QPixmapCache::find(key, &thumbnail)) return QIcon(thumbnail);
    QImageReader reader(node.thumbnailPath);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 64 * 1024 * 1024) return selectionIcon(node.type);
    reader.setScaledSize(size.scaled(16, 16, Qt::KeepAspectRatio));
    const QImage source = reader.read();
    if (source.isNull()) return selectionIcon(node.type);
    const QImage image = source.scaled(16, 16, Qt::KeepAspectRatio, Qt::FastTransformation);
    thumbnail = QPixmap(16, 16); thumbnail.fill(Qt::transparent);
    QPainter painter(&thumbnail);
    painter.drawImage((16 - image.width()) / 2, (16 - image.height()) / 2, image); painter.end();
    QPixmapCache::insert(key, thumbnail);
    return QIcon(thumbnail);
}

static const int ResourceThumbnailRole = Qt::UserRole + 1;

static void configureResourceMenu(QMenu *menu)
{
    // QMenu caches its scrollable style hint. Polish first so changing the
    // stylesheet sends StyleChange and updates that cache before layout.
    menu->ensurePolished();
    menu->setStyleSheet(QStringLiteral("QMenu { menu-scrollable: 1; } QMenu::item { padding: 2px 22px 2px 22px; }"));
}

static void markCurrentResource(QMenu *menu, QAction *action, const QString &current)
{
    if (action->data().toString() != current) return;
    action->setCheckable(true); action->setChecked(true); menu->setActiveAction(action);
}

static void appendResourceChoices(QMenu *menu, const QList<ResourceNode> &nodes,
                                  const QString &current, const QSet<QString> &excluded)
{
    for (const auto &node : nodes) {
        if (node.isGroup) {
            auto *group = menu->addMenu(QIcon(QStringLiteral(":/images/tree/folder.png")), menuLabel(node.name));
            configureResourceMenu(group);
            const auto children = node.children;
            QObject::connect(group, &QMenu::aboutToShow, group, [group, children, current, excluded] {
                if (group->actions().isEmpty()) appendResourceChoices(group, children, current, excluded);
            });
        } else if (!excluded.contains(node.name)) {
            auto *action = menu->addAction(resourceMenuIcon(node), menuLabel(node.name)); action->setData(node.name);
            if (node.isMissing) action->setToolTip(QObject::tr("Missing resource file"));
            markCurrentResource(menu, action, current);
        }
    }
    if (menu->actions().isEmpty()) menu->addAction(QObject::tr("<No resources>"))->setEnabled(false);
}

ResourceSelectionMenu::ResourceSelectionMenu(const QList<ResourceNode> &resources, const QString &current,
        const QString &emptyLabel, const QString &emptyValue, QWidget *parent, const QStringList &excluded)
    : QMenu(parent)
{
    // Qt handles submenus, keyboard navigation, screen edges and long menus.
    configureResourceMenu(this);
    if (!emptyLabel.isEmpty()) {
        auto *empty = addAction(menuLabel(emptyLabel)); empty->setData(emptyValue);
        markCurrentResource(this, empty, current);
    }
    appendResourceChoices(this, resources, current, QSet<QString>::fromList(excluded));
}

ResourceComboBox::ResourceComboBox(QWidget *parent) : QComboBox(parent)
{
    setMinimumContentsLength(8); setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(this, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &ResourceComboBox::updateCurrentIcon);
}

void ResourceComboBox::setResources(const Project &project, ResourceType type, const QString &current,
        const QString &emptyLabel, const QString &emptyValue, const QStringList &excluded)
{
    const QSignalBlocker blocker(this);
    m_resources = project.resources(type); m_emptyLabel = emptyLabel; m_emptyValue = emptyValue; m_excluded = excluded;
    m_resourceType = type;
    // Populate before attaching the model: inserting each row into a live view
    // sends layout/selection notifications for the entire resource list.
    auto *choices = new QStandardItemModel(this);
    const QIcon icon = selectionIcon(type);
    int selectedIndex = -1;
    const auto appendChoice = [&](const QString &text, const QString &name, const QString &thumbnail, bool resource) {
        auto *item = new QStandardItem(text);
        item->setData(name, Qt::UserRole);
        if (resource) {
            item->setIcon(icon);
            item->setData(thumbnail, ResourceThumbnailRole);
        }
        if (selectedIndex < 0 && name == current) selectedIndex = choices->rowCount();
        choices->appendRow(item);
    };
    if (!emptyLabel.isEmpty()) appendChoice(emptyLabel, emptyValue, QString(), false);
    std::function<void(const QList<ResourceNode> &)> append = [&](const QList<ResourceNode> &nodes) {
        for (const auto &node : nodes) {
            if (node.isGroup) append(node.children);
            else if (!excluded.contains(node.name)) appendChoice(node.name, node.name, node.thumbnailPath, true);
        }
    };
    append(m_resources);
    if (selectedIndex < 0 && !current.isEmpty() && !excluded.contains(current)) appendChoice(current, current, QString(), false);
    // QComboBox owns and deletes its previous model.
    setModel(choices);
    setCurrentIndex(selectedIndex);
    updateCurrentIcon();
}

void ResourceComboBox::updateCurrentIcon()
{
    const int index = currentIndex();
    const QVariant path = itemData(index, ResourceThumbnailRole);
    if (!path.isValid()) return;
    // DecorationRole is also queried while sizing hidden Qt popup views. Keep
    // those queries free of image I/O; only the selected resource needs a thumbnail.
    ResourceNode node;
    node.type = m_resourceType;
    node.thumbnailPath = path.toString();
    const QSignalBlocker blocker(this);
    setItemIcon(index, node.thumbnailPath.isEmpty() ? selectionIcon(m_resourceType) : resourceMenuIcon(node));
}

void ResourceComboBox::showPopup()
{ showPopupAt(mapToGlobal(QPoint(0, height()))); }

void ResourceComboBox::showPopupAt(const QPoint &globalPosition)
{
    if (m_selecting || !isEnabled()) return;
    m_selecting = true;
    const QPointer<ResourceComboBox> guard(this);
    QPointer<ResourceSelectionMenu> menu = new ResourceSelectionMenu(m_resources, currentData().toString(), m_emptyLabel, m_emptyValue, this, m_excluded);
    const QAction *selected = menu->exec(globalPosition);
    const QVariant value = menu && selected ? selected->data() : QVariant();
    delete menu.data();
    if (!guard) return;
    m_selecting = false; QComboBox::hidePopup();
    if (!value.isValid()) return;
    const int index = findData(value);
    if (index < 0) return;
    const QString text = itemText(index);
    setCurrentIndex(index);
    if (!guard) return;
    emit activated(index);
    if (guard) emit activated(text);
}

void ResourceComboBox::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
        || event->key() == Qt::Key_Down || event->key() == Qt::Key_Up || event->key() == Qt::Key_F4) {
        showPopup(); event->accept(); return;
    }
    // A focused reference field must not silently change its value by typing.
    if (!event->text().isEmpty() && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        showPopup(); event->accept(); return;
    }
    QComboBox::keyPressEvent(event);
}

void ResourceComboBox::wheelEvent(QWheelEvent *event) { event->ignore(); }
