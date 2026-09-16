#include "editorstandarddialogs.h"
#include "resourcebrowser.h"

#include "resourceitemdelegate.h"

#include <QCheckBox>
#include <QDrag>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPersistentModelIndex>
#include <functional>
#include <QAction>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QPushButton>
#include <QRegExp>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QSet>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>

enum class ResourceItemKind { Category, Group, Resource, Special };
static const int ItemKindRole = Qt::UserRole;
static const int FilePathRole = Qt::UserRole + 1;
static const int ResourceTypeRole = Qt::UserRole + 2;

class ResourceTreeWidget : public QTreeWidget
{
public:
    explicit ResourceTreeWidget(QWidget *parent) : QTreeWidget(parent) {
        setDragEnabled(true); setAcceptDrops(true); setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::DragDrop); setDefaultDropAction(Qt::MoveAction);
    }
    std::function<void(QTreeWidgetItem *, QTreeWidgetItem *, int, bool)> moveRequested;
    bool isLeftClick() const { return m_leftClick; }
protected:
    void mouseReleaseEvent(QMouseEvent *event) override {
        m_leftClick = event->button() == Qt::LeftButton;
        QTreeWidget::mouseReleaseEvent(event);
        m_leftClick = false;
    }
    void startDrag(Qt::DropActions) override {
        auto *item = currentItem();
        if (!item || !(item->flags() & Qt::ItemIsDragEnabled) || item->data(0, ResourceTypeRole).toInt() >= static_cast<int>(ResourceType::Macro)) return;
        m_dragged = indexFromItem(item);
        QDrag drag(this); drag.setMimeData(mimeData({item})); drag.setPixmap(item->icon(0).pixmap(16, 16));
        drag.exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction); m_dragged = QPersistentModelIndex();
    }
    bool destination(const QPoint &position, QTreeWidgetItem *&parent, int &row) {
        auto *source = itemFromIndex(m_dragged); auto *target = itemAt(position);
        if (!source || !target || source == target || source->data(0, ResourceTypeRole) != target->data(0, ResourceTypeRole)) return false;
        const auto kind = static_cast<ResourceItemKind>(target->data(0, ItemKindRole).toInt());
        if (kind == ResourceItemKind::Special || target->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::Macro)) return false;
        if (dropIndicatorPosition() == QAbstractItemView::OnItem && kind != ResourceItemKind::Resource) { parent = target; row = target->childCount(); }
        else {
            parent = target->parent(); if (!parent) return false;
            row = parent->indexOfChild(target) + (dropIndicatorPosition() == QAbstractItemView::AboveItem ? 0 : 1);
        }
        for (auto *ancestor = parent; ancestor; ancestor = ancestor->parent()) if (ancestor == source) return false;
        return true;
    }
    void dragMoveEvent(QDragMoveEvent *event) override {
        if (event->source() != this) { event->ignore(); return; }
        QTreeWidget::dragMoveEvent(event);
        QTreeWidgetItem *parent = nullptr; int row = 0;
        if (!destination(event->pos(), parent, row)) event->ignore();
    }
    void dropEvent(QDropEvent *event) override {
        QTreeWidgetItem *parent = nullptr; int row = 0;
        if (event->source() != this || !destination(event->pos(), parent, row) || !moveRequested) { event->ignore(); return; }
        const bool copy = event->keyboardModifiers() & Qt::ControlModifier;
        moveRequested(itemFromIndex(m_dragged), parent, row, copy);
        event->setDropAction(copy ? Qt::CopyAction : Qt::MoveAction); event->accept();
    }
private:
    QPersistentModelIndex m_dragged;
    bool m_leftClick = false;
};

struct ResourceCategory {
    ResourceType type;
    const char *title;
    const char *singularTitle;
    const char *iconName;
};

static const ResourceCategory ResourceCategories[] = {
    {ResourceType::Sprite, QT_TRANSLATE_NOOP("ResourceBrowser", "Sprites"), QT_TRANSLATE_NOOP("ResourceBrowser", "Sprite"), ""},
    {ResourceType::Sound, QT_TRANSLATE_NOOP("ResourceBrowser", "Sounds"), QT_TRANSLATE_NOOP("ResourceBrowser", "Sound"), "sound"},
    {ResourceType::Background, QT_TRANSLATE_NOOP("ResourceBrowser", "Backgrounds"), QT_TRANSLATE_NOOP("ResourceBrowser", "Background"), ""},
    {ResourceType::Path, QT_TRANSLATE_NOOP("ResourceBrowser", "Paths"), QT_TRANSLATE_NOOP("ResourceBrowser", "Path"), "path"},
    {ResourceType::Script, QT_TRANSLATE_NOOP("ResourceBrowser", "Scripts"), QT_TRANSLATE_NOOP("ResourceBrowser", "Script"), "script"},
    {ResourceType::Shader, QT_TRANSLATE_NOOP("ResourceBrowser", "Shaders"), QT_TRANSLATE_NOOP("ResourceBrowser", "Shader"), "shader"},
    {ResourceType::Font, QT_TRANSLATE_NOOP("ResourceBrowser", "Fonts"), QT_TRANSLATE_NOOP("ResourceBrowser", "Font"), "font"},
    {ResourceType::Timeline, QT_TRANSLATE_NOOP("ResourceBrowser", "Time Lines"), QT_TRANSLATE_NOOP("ResourceBrowser", "Time Line"), "timeline"},
    {ResourceType::Object, QT_TRANSLATE_NOOP("ResourceBrowser", "Objects"), QT_TRANSLATE_NOOP("ResourceBrowser", "Object"), ""},
    {ResourceType::Room, QT_TRANSLATE_NOOP("ResourceBrowser", "Rooms"), QT_TRANSLATE_NOOP("ResourceBrowser", "Room"), "room"},
    {ResourceType::IncludedFile, QT_TRANSLATE_NOOP("ResourceBrowser", "Included Files"), QT_TRANSLATE_NOOP("ResourceBrowser", "Included File"), "includedfile"},
    {ResourceType::Extension, QT_TRANSLATE_NOOP("ResourceBrowser", "Extensions"), QT_TRANSLATE_NOOP("ResourceBrowser", "Extension"), "extension"},
    {ResourceType::Macro, QT_TRANSLATE_NOOP("ResourceBrowser", "Macros"), QT_TRANSLATE_NOOP("ResourceBrowser", "Macro"), "macro"}
};

static QIcon folderIcon()
{
    QIcon icon(QStringLiteral(":/images/tree/folder.png"));
    icon.addFile(QStringLiteral(":/images/tree/folderopen.png"), QSize(), QIcon::Normal, QIcon::On);
    return icon;
}

QIcon ResourceBrowser::resourceIcon(const ResourceNode &resource)
{
    if (resource.isGroup)
        return folderIcon();
    if (!resource.thumbnailPath.isEmpty()) {
        auto cached = m_thumbnailIcons.constFind(resource.thumbnailPath);
        if (cached == m_thumbnailIcons.constEnd()) {
            QImageReader reader(resource.thumbnailPath);
            const QSize size = reader.size();
            if (size.width() > 32 || size.height() > 32)
                reader.setScaledSize(size.scaled(32, 32, Qt::KeepAspectRatio));
            const QImage image = reader.read();
            QIcon thumbnail;
            if (!image.isNull()) {
                for (int extent : {16, 32}) {
                    const QImage scaled = image.scaled(extent, extent, Qt::KeepAspectRatio, Qt::FastTransformation);
                    QPixmap canvas(extent, extent);
                    canvas.fill(Qt::transparent);
                    QPainter painter(&canvas);
                    painter.drawImage((extent - scaled.width()) / 2, (extent - scaled.height()) / 2, scaled);
                    painter.end();
                    thumbnail.addPixmap(canvas);
                }
            }
            m_thumbnailIcons.insert(resource.thumbnailPath, thumbnail);
            cached = m_thumbnailIcons.constFind(resource.thumbnailPath);
        }
        if (!cached.value().isNull())
            return cached.value();
    }
    for (const ResourceCategory &category : ResourceCategories) {
        if (category.type == resource.type && category.iconName[0])
            return QIcon(QStringLiteral(":/images/tree/%1.png").arg(QLatin1String(category.iconName)));
    }
    // Empty sprite/background resources still reserve the image slot. An
    // object with no sprite uses the reference editor's white square.
    QPixmap placeholder(16, 16);
    placeholder.fill(resource.type == ResourceType::Object ? Qt::white : Qt::transparent);
    return QIcon(placeholder);
}

ResourceBrowser::ResourceBrowser(QWidget *parent)
    : QWidget(parent),
      m_resourceTree(new ResourceTreeWidget(this)),
      m_searchEdit(new QLineEdit(this)),
      m_wholeWordCheck(new QCheckBox(tr("&Whole Word Only"), this)),
      m_filterTreeCheck(new QCheckBox(tr("&Filter Tree"), this)),
      m_previousButton(new QPushButton(tr("&Previous"), this)),
      m_nextButton(new QPushButton(tr("&Next"), this)),
      m_macroItem(nullptr),
      m_settingsItem(nullptr)
{
    setObjectName(QStringLiteral("resourceBrowser"));
    setMinimumWidth(240);

    m_resourceTree->setObjectName(QStringLiteral("resourceTree"));
    m_resourceTree->setItemDelegate(new ResourceItemDelegate(m_resourceTree));
    QPalette treePalette = m_resourceTree->palette();
    // Qt's Windows style uses the Dark role for dotted branch connectors.
    treePalette.setColor(QPalette::Dark, QColor(109, 109, 109));
    treePalette.setColor(QPalette::Highlight, QColor(51, 153, 255));
    treePalette.setColor(QPalette::HighlightedText, Qt::white);
    m_resourceTree->setPalette(treePalette);
    m_resourceTree->setHeaderHidden(true);
    m_resourceTree->setIndentation(20);
    m_resourceTree->setIconSize(QSize(16, 16));
    m_resourceTree->setUniformRowHeights(true);
    m_resourceTree->setMouseTracking(true);
    m_resourceTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resourceTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resourceTree->header()->setStretchLastSection(false);
    m_resourceTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    m_searchEdit->setPlaceholderText(tr("Search For Resources..."));
    m_searchEdit->setAcceptDrops(false);
    m_searchEdit->setAccessibleName(tr("Search for resources"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(21);
    m_previousButton->setFixedHeight(21);
    m_nextButton->setFixedHeight(21);

    auto *searchPanel = new QWidget(this);
    searchPanel->setObjectName(QStringLiteral("resourceSearchPanel"));
    auto *searchLayout = new QVBoxLayout(searchPanel);
    searchLayout->setContentsMargins(2, 2, 2, 2);
    searchLayout->setSpacing(1);
    searchLayout->addWidget(m_searchEdit);
    auto *optionsLayout = new QHBoxLayout;
    optionsLayout->setSpacing(4);
    optionsLayout->addWidget(m_wholeWordCheck);
    optionsLayout->addStretch();
    optionsLayout->addWidget(m_filterTreeCheck);
    searchLayout->addLayout(optionsLayout);
    auto *buttonsLayout = new QHBoxLayout;
    buttonsLayout->setSpacing(2);
    buttonsLayout->addWidget(m_previousButton);
    buttonsLayout->addWidget(m_nextButton);
    searchLayout->addLayout(buttonsLayout);

    auto *logoLabel = new QLabel(this);
    logoLabel->setObjectName(QStringLiteral("brandLogo"));
    logoLabel->setPixmap(QPixmap(QStringLiteral(":/images/logo.png")));
    logoLabel->setAlignment(Qt::AlignCenter);
    logoLabel->setFixedHeight(39);
    logoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    logoLabel->setAccessibleName(tr("YoYo Games reference artwork"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_resourceTree, 1);
    layout->addWidget(searchPanel);
    layout->addWidget(logoLabel);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &ResourceBrowser::updateSearch);
    connect(m_wholeWordCheck, &QCheckBox::toggled, this, &ResourceBrowser::updateSearch);
    connect(m_filterTreeCheck, &QCheckBox::toggled, this, &ResourceBrowser::updateSearch);
    connect(m_previousButton, &QPushButton::clicked, this, [this] { findMatch(-1); });
    connect(m_nextButton, &QPushButton::clicked, this, [this] { findMatch(1); });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this] { findMatch(1); });
    connect(m_resourceTree, &QTreeWidget::itemActivated, this, [this] { activateCurrentResource(); });
    connect(m_resourceTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        if (!static_cast<ResourceTreeWidget *>(m_resourceTree)->isLeftClick()
            || item->data(0, ItemKindRole).toInt() != static_cast<int>(ResourceItemKind::Resource)) return;
        emit resourceClicked(static_cast<ResourceType>(item->data(0, ResourceTypeRole).toInt()),
            item->data(0, FilePathRole).toString());
    });
    createActions();
    static_cast<ResourceTreeWidget *>(m_resourceTree)->moveRequested = [this](QTreeWidgetItem *source, QTreeWidgetItem *parent, int row, bool copy) {
        ResourceTreeRequest request; request.command = copy ? ResourceTreeCommand::Duplicate : ResourceTreeCommand::Move;
        request.type = static_cast<ResourceType>(source->data(0, ResourceTypeRole).toInt()); request.source = itemPath(source); request.destination = itemPath(parent); request.position = row;
        emit treeCommandRequested(request);
    };
    connect(m_resourceTree, &QTreeWidget::currentItemChanged, this, &ResourceBrowser::updateActions);
    m_resourceTree->installEventFilter(this);
    m_resourceTree->viewport()->installEventFilter(this);
    setProject(Project());
}

void ResourceBrowser::createActions()
{
    const auto action = [this](const QString &text, const QString &icon, const QKeySequence &shortcut) {
        auto *result = new QAction(icon.isEmpty() ? QIcon() : QIcon(QStringLiteral(":/images/resourcecommands/%1.png").arg(icon)), text, this);
        result->setShortcut(shortcut);
        result->setEnabled(false);
        return result;
    };
    m_addExistingAction = action(tr("Add Existing"), QString(), QKeySequence());
    m_sortAction = action(tr("Sort by Name"), QString(), QKeySequence());
    m_referencesAction = action(tr("Check &References..."), QStringLiteral("properties"), QKeySequence());
    connect(m_addExistingAction, &QAction::triggered, this, [this] { requestTreeCommand(ResourceTreeCommand::AddExisting); });
    connect(m_sortAction, &QAction::triggered, this, [this] { requestTreeCommand(ResourceTreeCommand::Sort); });
    connect(m_referencesAction, &QAction::triggered, this, [this] { requestTreeCommand(ResourceTreeCommand::References); });
    m_createAction = action(tr("&Create"), QStringLiteral("create"), QKeySequence());
    m_duplicateAction = action(tr("D&uplicate"), QStringLiteral("duplicate"), QKeySequence(Qt::ALT | Qt::Key_Insert));
    m_createGroupAction = action(tr("Create &Group"), QStringLiteral("group"), QKeySequence(Qt::SHIFT | Qt::Key_Insert));
    m_deleteAction = action(tr("&Delete"), QStringLiteral("delete"), QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    m_renameAction = action(tr("&Rename"), QStringLiteral("rename"), QKeySequence(Qt::Key_F2));
    m_propertiesAction = action(tr("&Properties..."), QStringLiteral("properties"), QKeySequence(Qt::ALT | Qt::Key_Return));
    m_openLocationAction = new QAction(tr("Open in Explorer"), this);
    connect(m_createAction, &QAction::triggered, this, [this] {
        if (m_createAction->isEnabled() && m_resourceTree->currentItem())
            emit createResourceRequested(static_cast<ResourceType>(m_resourceTree->currentItem()->data(0, ResourceTypeRole).toInt()));
    });
    connect(m_createGroupAction, &QAction::triggered, this, [this] { requestTreeCommand(ResourceTreeCommand::CreateGroup); });
    connect(m_duplicateAction, &QAction::triggered, this, [this] { requestTreeCommand(ResourceTreeCommand::Duplicate); });
    connect(m_deleteAction, &QAction::triggered, this, [this] {
        auto *item = m_resourceTree->currentItem();
        if (item && item->data(0, ItemKindRole).toInt() == static_cast<int>(ResourceItemKind::Group)) { requestTreeCommand(ResourceTreeCommand::DeleteGroup); return; }
        if (item && m_deleteAction->isEnabled()) emit removeResourceRequested(static_cast<ResourceType>(item->data(0, ResourceTypeRole).toInt()), item->data(0, FilePathRole).toString());
    });
    connect(m_renameAction, &QAction::triggered, this, [this] {
        auto *item = m_resourceTree->currentItem();
        if (item && item->data(0, ItemKindRole).toInt() == static_cast<int>(ResourceItemKind::Group)) { requestTreeCommand(ResourceTreeCommand::RenameGroup); return; }
        if (item && m_renameAction->isEnabled()) emit renameResourceRequested(static_cast<ResourceType>(item->data(0, ResourceTypeRole).toInt()), item->data(0, FilePathRole).toString(), QString());
    });
    connect(m_propertiesAction, &QAction::triggered, this, &ResourceBrowser::activateCurrentResource);
    connect(m_openLocationAction, &QAction::triggered, this, &ResourceBrowser::openResourceLocation);
}

void ResourceBrowser::addEditActions(QMenu *menu)
{
    menu->addActions({m_createAction, m_addExistingAction, m_duplicateAction});
    menu->addSeparator();
    menu->addActions({m_createGroupAction, m_deleteAction, m_renameAction, m_propertiesAction});
}

void ResourceBrowser::updateActions()
{
    const QTreeWidgetItem *item = m_resourceTree->currentItem();
    const bool open = !m_projectFilePath.isEmpty();
    const bool resource = item && item->data(0, ItemKindRole).toInt() == static_cast<int>(ResourceItemKind::Resource);
    const bool editable = item && item->data(0, ResourceTypeRole).isValid()
        && (item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::Sprite)
            || item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::Path)
            || item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::Sound)
            || item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::Script)
            || item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::Shader)
            || item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::Font)
            || item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::Object)
            || item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::Timeline)
            || item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::Extension)
            || item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::Room)
            || item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::Background));
    const bool folder = item && item->data(0, ItemKindRole).toInt() == static_cast<int>(ResourceItemKind::Group);
    const bool regular = item && item->data(0, ResourceTypeRole).isValid() && (editable || item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::IncludedFile));
    m_createGroupAction->setEnabled(open && regular);
    m_duplicateAction->setEnabled(open && regular && (folder || resource));
    m_addExistingAction->setEnabled(open && regular); m_sortAction->setEnabled(open && regular);
    QString resourceName;
    if (item && item->data(0, ResourceTypeRole).isValid()) {
        const auto type = static_cast<ResourceType>(item->data(0, ResourceTypeRole).toInt());
        for (const auto &category : ResourceCategories) {
            if (category.type == type) {
                resourceName = tr(category.singularTitle);
                break;
            }
        }
    }
    m_createAction->setText(resourceName.isEmpty() ? tr("&Create") : tr("&Create %1").arg(resourceName));
    m_addExistingAction->setText(item && item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::Extension)
        ? tr("Import Extension...") : resourceName.isEmpty() ? tr("Add Existing") : tr("Add Existing %1").arg(resourceName));
    m_referencesAction->setEnabled(open && regular && resource);
    m_createAction->setEnabled(open && editable);
    m_deleteAction->setEnabled(open && regular && (folder || resource));
    m_renameAction->setEnabled(open && regular && (folder || (resource && QFileInfo(item->data(0, FilePathRole).toString()).isFile())));
    const bool macroScope = resource && item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::Macro);
    const bool information = item && item->data(0, ResourceTypeRole).isValid() && item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::GameInformation);
    const bool settings = item == m_settingsItem && item && QFileInfo(item->data(0, FilePathRole).toString()).isFile();
    m_propertiesAction->setEnabled(open && (settings || information || ((editable || macroScope) && resource && QFileInfo(item->data(0, FilePathRole).toString()).isFile())));
    const bool group = item && (item->data(0, ItemKindRole).toInt() == static_cast<int>(ResourceItemKind::Category)
        || item->data(0, ItemKindRole).toInt() == static_cast<int>(ResourceItemKind::Group));
    const QString path = item ? item->data(0, FilePathRole).toString() : QString();
    m_openLocationAction->setEnabled(open && (group || (!path.isEmpty() && QFileInfo::exists(path))));
}

bool ResourceBrowser::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_resourceTree || watched == m_resourceTree->viewport()) && event->type() == QEvent::ContextMenu) {
        auto *context = static_cast<QContextMenuEvent *>(event);
        QTreeWidgetItem *item;
        QPoint position;
        if (context->reason() == QContextMenuEvent::Keyboard) {
            item = m_resourceTree->currentItem();
            if (item) m_resourceTree->scrollToItem(item);
            position = item ? m_resourceTree->visualItemRect(item).center() : QPoint(8, 8);
        } else {
            position = m_resourceTree->viewport()->mapFromGlobal(context->globalPos());
            item = m_resourceTree->itemAt(position);
        }
        showContextMenu(item, m_resourceTree->viewport()->mapToGlobal(position));
        event->accept();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void ResourceBrowser::showContextMenu(QTreeWidgetItem *item, const QPoint &globalPosition)
{
    QMenu menu(this);
    if (!item) {
        menu.addAction(tr("&Find Resource..."), this, SLOT(focusSearch()));
        menu.addSeparator();
        menu.addAction(tr("&Expand Resource Tree"), this, SLOT(expandAll()));
        menu.addAction(tr("C&ollapse Resource Tree"), this, SLOT(collapseAll()));
    } else {
        m_resourceTree->setCurrentItem(item);
        m_resourceTree->setFocus(Qt::MouseFocusReason);
        updateActions();
        const auto kind = static_cast<ResourceItemKind>(item->data(0, ItemKindRole).toInt());
        const bool regular = kind != ResourceItemKind::Special
            && item->data(0, ResourceTypeRole).toInt() != static_cast<int>(ResourceType::Macro);
        if (regular) {
            menu.addAction(m_createAction);
            menu.addAction(m_addExistingAction);
            menu.addAction(m_duplicateAction);
            menu.addSeparator();
            menu.addAction(m_createGroupAction);
            menu.addSeparator();
            menu.addAction(m_sortAction);
            menu.addSeparator();
            menu.addAction(m_deleteAction);
            menu.addSeparator();
            menu.addAction(m_renameAction);
            menu.addSeparator();
        }
        if (regular && kind == ResourceItemKind::Resource)
            menu.addAction(m_referencesAction);
        menu.addAction(m_propertiesAction);
        menu.addAction(m_openLocationAction);
        if (m_propertiesAction->isEnabled()) menu.setDefaultAction(m_propertiesAction);
    }
    menu.exec(globalPosition);
}

void ResourceBrowser::openResourceLocation()
{
    const QTreeWidgetItem *item = m_resourceTree->currentItem();
    if (!item || !m_openLocationAction->isEnabled()) return;
    QString path = item->data(0, FilePathRole).toString();
    if (path.isEmpty()) path = m_projectFilePath;
    const QFileInfo info(path);
    const QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory)))
        EditorMessageBox::warning(this, tr("Open in Explorer"), tr("Cannot open the resource directory: %1").arg(directory));
}

void ResourceBrowser::activateCurrentResource()
{
    const QTreeWidgetItem *item = m_resourceTree->currentItem();
    if (!item) return;
    if (item->data(0, ResourceTypeRole).isValid() && item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::GameInformation)) {
        if (!m_projectFilePath.isEmpty()) emit resourceActivated(ResourceType::GameInformation, item->data(0, FilePathRole).toString());
        return;
    }
    if (item == m_settingsItem) {
        const QString path = item->data(0, FilePathRole).toString();
        if (QFileInfo(path).isFile()) emit resourceActivated(ResourceType::GameSettings, path);
        return;
    }
    if (item->data(0, ItemKindRole).toInt() != static_cast<int>(ResourceItemKind::Resource)) return;
    const QString path = item->data(0, FilePathRole).toString();
    if (!path.isEmpty()) emit resourceActivated(static_cast<ResourceType>(item->data(0, ResourceTypeRole).toInt()), path);
}

QList<int> ResourceBrowser::selectedGroupPath(ResourceType type) const
{
    QTreeWidgetItem *item = m_resourceTree->currentItem();
    QList<int> path;
    if (!item || item->data(0, ResourceTypeRole).toInt() != static_cast<int>(type)) return path;
    if (item->data(0, ItemKindRole).toInt() == static_cast<int>(ResourceItemKind::Resource)) item = item->parent();
    while (item && item->parent()) {
        path.prepend(item->parent()->indexOfChild(item));
        item = item->parent();
    }
    return path;
}

void ResourceBrowser::addResource(const ResourceNode &resource, const QList<int> &groupPath)
{
    QTreeWidgetItem *parent = nullptr;
    for (int i = 0; i < m_resourceTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_resourceTree->topLevelItem(i);
        if (item->data(0, ItemKindRole).toInt() == static_cast<int>(ResourceItemKind::Category)
            && item->data(0, ResourceTypeRole).toInt() == static_cast<int>(resource.type)) { parent = item; break; }
    }
    for (int index : groupPath) parent = parent ? parent->child(index) : nullptr;
    if (!parent) return;
    appendResources(parent, {resource});
    QTreeWidgetItem *item = parent->child(parent->childCount() - 1);
    m_searchEdit->clear();
    updateSearch();
    for (; parent; parent = parent->parent()) parent->setExpanded(true);
    m_resourceTree->setCurrentItem(item);
    m_resourceTree->scrollToItem(item);
}

static void indexResourceNodes(const QList<ResourceNode> &nodes, QHash<QString, const ResourceNode *> &byPath)
{
    for (const ResourceNode &node : nodes) {
        if (node.isGroup) indexResourceNodes(node.children, byPath);
        else if (!node.filePath.isEmpty()) byPath.insert(node.filePath, &node);
    }
}
void ResourceBrowser::refreshIcons(const Project &project)
{
    m_projectFilePath = project.filePath();
    QHash<QString, const ResourceNode *> byPath;
    indexResourceNodes(project.resources(ResourceType::Sprite), byPath);
    indexResourceNodes(project.resources(ResourceType::Background), byPath);
    indexResourceNodes(project.resources(ResourceType::Object), byPath);
    m_thumbnailIcons.clear();
    for (QTreeWidgetItemIterator it(m_resourceTree); *it; ++it) {
        const ResourceNode *node = byPath.value((*it)->data(0, FilePathRole).toString(), nullptr);
        if (node) (*it)->setIcon(0, resourceIcon(*node));
    }
    updateActions();
}

void ResourceBrowser::relocatePaths(const QString &oldDirectory, const QString &newDirectory)
{
    for (QTreeWidgetItemIterator it(m_resourceTree); *it; ++it) {
        const QString path = (*it)->data(0, FilePathRole).toString();
        if (path.isEmpty()) continue;
        const QString relocated = QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(path));
        (*it)->setData(0, FilePathRole, relocated);
        (*it)->setToolTip(0, relocated);
    }
    m_thumbnailIcons.clear();
}

void ResourceBrowser::appendResources(QTreeWidgetItem *parent, const QList<ResourceNode> &resources)
{
    for (const ResourceNode &resource : resources) {
        auto *item = new QTreeWidgetItem(parent, QStringList(resource.name));
        item->setData(0, ItemKindRole, static_cast<int>(resource.isGroup
            ? ResourceItemKind::Group : ResourceItemKind::Resource));
        item->setData(0, FilePathRole, resource.filePath);
        item->setData(0, ResourceTypeRole, static_cast<int>(resource.type));
        item->setFlags((item->flags() | Qt::ItemIsDragEnabled) & ~Qt::ItemIsEditable);
        if (!resource.isGroup) item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
        item->setIcon(0, resourceIcon(resource));
        item->setToolTip(0, resource.filePath);
        if (resource.isMissing) {
            item->setForeground(0, QColor(220, 130, 100));
            item->setToolTip(0, tr("Missing file:\n%1").arg(resource.filePath));
        }
        appendResources(item, resource.children);
    }
}

static QString resourceTreeItemKey(const QTreeWidgetItem *item)
{
    if (!item) return QString();
    const auto kind = static_cast<ResourceItemKind>(item->data(0, ItemKindRole).toInt());
    const QString prefix = QString::number(static_cast<int>(kind)) + QLatin1Char(':')
        + QString::number(item->data(0, ResourceTypeRole).toInt()) + QLatin1Char(':');
    if (kind == ResourceItemKind::Resource) return prefix + item->data(0, FilePathRole).toString();
    QStringList folders;
    for (auto *parent = item; parent && parent->parent(); parent = parent->parent()) folders.prepend(parent->text(0));
    return prefix + folders.join(QLatin1Char('/'));
}

void ResourceBrowser::setProject(const Project &project)
{
    const bool sameProject = !m_projectFilePath.isEmpty() && m_projectFilePath == project.filePath();
    QSet<QString> expanded;
    QStringList selectionCandidates;
    const int verticalPosition = m_resourceTree->verticalScrollBar()->value();
    const int horizontalPosition = m_resourceTree->horizontalScrollBar()->value();
    if (sameProject) {
        for (QTreeWidgetItemIterator it(m_resourceTree); *it; ++it)
            if ((*it)->isExpanded()) expanded.insert(resourceTreeItemKey(*it));
        auto *current = m_resourceTree->currentItem();
        selectionCandidates.append(resourceTreeItemKey(current));
        if (current && current->parent()) {
            auto *parent = current->parent(); const int row = parent->indexOfChild(current);
            // After deletion prefer the next sibling, then the previous one,
            // then the closest surviving parent. Keys survive index shifts.
            if (row + 1 < parent->childCount()) selectionCandidates.append(resourceTreeItemKey(parent->child(row + 1)));
            if (row > 0) selectionCandidates.append(resourceTreeItemKey(parent->child(row - 1)));
            for (; parent; parent = parent->parent()) selectionCandidates.append(resourceTreeItemKey(parent));
        }
    }
    m_projectFilePath = project.filePath();
    const QSignalBlocker treeBlocker(m_resourceTree);
    const QSignalBlocker searchBlocker(m_searchEdit);
    m_resourceTree->setUpdatesEnabled(false);
    m_resourceTree->clear();
    m_thumbnailIcons.clear();
    m_resourceTree->viewport()->unsetCursor();
    if (!sameProject) m_searchEdit->clear();
    QFont categoryFont = m_resourceTree->font();
    categoryFont.setBold(true);
    for (const ResourceCategory &category : ResourceCategories) {
        auto *item = new QTreeWidgetItem(m_resourceTree, QStringList(tr(category.title)));
        item->setData(0, ItemKindRole, static_cast<int>(ResourceItemKind::Category));
        item->setData(0, ResourceTypeRole, static_cast<int>(category.type));
        item->setIcon(0, folderIcon());
        item->setFont(0, categoryFont);
        item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
        if (category.type == ResourceType::Macro) item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
        if (category.type != ResourceType::Macro) appendResources(item, project.resources(category.type));
        if (category.type == ResourceType::Macro)
            m_macroItem = item;
    }
    auto *informationItem = new QTreeWidgetItem(m_resourceTree, QStringList(tr("Game Information")));
    informationItem->setData(0, ItemKindRole, static_cast<int>(ResourceItemKind::Special));
    informationItem->setData(0, ResourceTypeRole, static_cast<int>(ResourceType::GameInformation));
    informationItem->setData(0, FilePathRole, project.informationFilePath());
    informationItem->setToolTip(0, project.informationFilePath());
    informationItem->setIcon(0, QIcon(QStringLiteral(":/images/information.png")));
    m_settingsItem = new QTreeWidgetItem(m_resourceTree, QStringList(tr("Global Game Settings")));
    m_settingsItem->setData(0, ItemKindRole, static_cast<int>(ResourceItemKind::Special));
    m_settingsItem->setData(0, ResourceTypeRole, static_cast<int>(ResourceType::GameSettings));
    m_settingsItem->setIcon(0, QIcon(QStringLiteral(":/images/gamesettings.png")));
    refreshMacros(project);
    updateSearch();
    QTreeWidgetItem *selection = nullptr;
    if (sameProject) {
        QHash<QString, QTreeWidgetItem *> items;
        for (QTreeWidgetItemIterator it(m_resourceTree); *it; ++it) {
            const QString key = resourceTreeItemKey(*it);
            items.insert(key, *it); (*it)->setExpanded(expanded.contains(key));
        }
        for (const auto &key : selectionCandidates) {
            auto *candidate = items.value(key);
            if (candidate && !candidate->isHidden()) { selection = candidate; break; }
        }
    }
    m_resourceTree->setCurrentItem(selection ? selection : m_resourceTree->topLevelItem(0));
    if (sameProject) {
        m_resourceTree->doItemsLayout();
        m_resourceTree->verticalScrollBar()->setValue(verticalPosition);
        m_resourceTree->horizontalScrollBar()->setValue(horizontalPosition);
    } else m_resourceTree->scrollToTop();
    m_resourceTree->setUpdatesEnabled(true);
    updateActions();
}

void ResourceBrowser::refreshMacros(const Project &project)
{
    m_projectFilePath = project.filePath();
    const QSignalBlocker blocker(m_resourceTree);
    const auto *current = m_resourceTree->currentItem();
    const QString selectedPath = current && current->parent() == m_macroItem ? current->data(0, FilePathRole).toString() : QString();
    qDeleteAll(m_macroItem->takeChildren());
    if (project.isOpen()) {
        auto addScope = [this, &selectedPath](const QString &name, const QString &path) {
            auto *item = new QTreeWidgetItem(m_macroItem, QStringList(name));
            item->setData(0, ItemKindRole, static_cast<int>(ResourceItemKind::Resource));
            item->setData(0, ResourceTypeRole, static_cast<int>(ResourceType::Macro));
            item->setData(0, FilePathRole, path); item->setToolTip(0, path);
            item->setIcon(0, QIcon(QStringLiteral(":/images/tree/macro.png")));
            if (path == selectedPath) m_resourceTree->setCurrentItem(item);
        };
        addScope(tr("All Configurations"), project.filePath());
        for (const auto &configuration : project.configurations()) addScope(configuration.name, configuration.filePath);
    }
    updateSearch(); updateActions();
}

void ResourceBrowser::setConfiguration(const ProjectConfiguration *configuration)
{
    const QString path = configuration ? configuration->filePath : QString();
    m_settingsItem->setData(0, FilePathRole, path);
    m_settingsItem->setToolTip(0, path);
    updateSearch();
}

void ResourceBrowser::focusSearch()
{
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void ResourceBrowser::expandAll()
{
    m_resourceTree->expandAll();
}

void ResourceBrowser::collapseAll()
{
    m_resourceTree->collapseAll();
}

bool ResourceBrowser::matchesSearch(const QTreeWidgetItem *item) const
{
    if (item->data(0, ItemKindRole).toInt() != static_cast<int>(ResourceItemKind::Resource))
        return false;
    const QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty())
        return true;
    if (m_wholeWordCheck->isChecked()) {
        const QRegExp expression(QStringLiteral("\\b%1\\b").arg(QRegExp::escape(query)),
                                 Qt::CaseInsensitive);
        return expression.indexIn(item->text(0)) >= 0;
    }
    return item->text(0).contains(query, Qt::CaseInsensitive);
}

bool ResourceBrowser::filterItem(QTreeWidgetItem *item)
{
    bool hasMatch = matchesSearch(item);
    bool childMatches = false;
    for (int index = 0; index < item->childCount(); ++index)
        childMatches = filterItem(item->child(index)) || childMatches;
    hasMatch = hasMatch || childMatches;
    const bool filtering = m_filterTreeCheck->isChecked() && !m_searchEdit->text().trimmed().isEmpty();
    item->setHidden(filtering && !hasMatch);
    if (filtering && childMatches)
        item->setExpanded(true);
    return hasMatch;
}

void ResourceBrowser::updateSearch()
{
    bool hasMatch = false;
    for (int index = 0; index < m_resourceTree->topLevelItemCount(); ++index) {
        QTreeWidgetItem *item = m_resourceTree->topLevelItem(index);
        const bool matches = filterItem(item);
        hasMatch = hasMatch || matches;
    }
    const bool canNavigate = hasMatch && !m_searchEdit->text().trimmed().isEmpty();
    m_previousButton->setEnabled(canNavigate);
    m_nextButton->setEnabled(canNavigate);
}

void ResourceBrowser::findMatch(int direction)
{
    QList<QTreeWidgetItem *> items;
    for (QTreeWidgetItemIterator it(m_resourceTree); *it; ++it)
        items.append(*it);
    const int count = items.size();
    int currentIndex = items.indexOf(m_resourceTree->currentItem());
    if (currentIndex < 0)
        currentIndex = direction > 0 ? -1 : 0;
    for (int step = 1; step <= count; ++step) {
        const int index = (currentIndex + direction * step + count * 2) % count;
        QTreeWidgetItem *item = items.at(index);
        if (!item->isHidden() && matchesSearch(item)) {
            for (QTreeWidgetItem *parent = item->parent(); parent; parent = parent->parent())
                parent->setExpanded(true);
            m_resourceTree->setCurrentItem(item);
            m_resourceTree->scrollToItem(item);
            return;
        }
    }
}

void ResourceBrowser::refreshInformation(const Project &project)
{
    for (int i = 0; i < m_resourceTree->topLevelItemCount(); ++i) {
        auto *item = m_resourceTree->topLevelItem(i);
        if (item->data(0, ResourceTypeRole).isValid() && item->data(0, ResourceTypeRole).toInt() == static_cast<int>(ResourceType::GameInformation)) {
            item->setData(0, FilePathRole, project.informationFilePath()); item->setToolTip(0, project.informationFilePath()); break;
        }
    }
    updateActions();
}

void ResourceBrowser::selectResource(const QString &filePath)
{
    for (QTreeWidgetItemIterator it(m_resourceTree); *it; ++it) {
        if ((*it)->data(0, ItemKindRole).toInt() != static_cast<int>(ResourceItemKind::Resource) || (*it)->data(0, FilePathRole).toString() != filePath) continue;
        for (auto *parent = (*it)->parent(); parent; parent = parent->parent()) parent->setExpanded(true);
        m_resourceTree->setCurrentItem(*it); m_resourceTree->scrollToItem(*it); break;
    }
}

QList<int> ResourceBrowser::itemPath(QTreeWidgetItem *item) const
{
    QList<int> path;
    while (item && item->parent()) { path.prepend(item->parent()->indexOfChild(item)); item = item->parent(); }
    return path;
}
void ResourceBrowser::selectTreeNode(ResourceType type, const QList<int> &path)
{
    for (int i = 0; i < m_resourceTree->topLevelItemCount(); ++i) {
        auto *item = m_resourceTree->topLevelItem(i);
        if (item->data(0, ResourceTypeRole).toInt() != static_cast<int>(type)) continue;
        for (int index : path) { if (!item || index < 0 || index >= item->childCount()) return; item->setExpanded(true); item = item->child(index); }
        if (item) { m_resourceTree->setCurrentItem(item); m_resourceTree->scrollToItem(item); } return;
    }
}
void ResourceBrowser::requestTreeCommand(ResourceTreeCommand command)
{
    auto *item = m_resourceTree->currentItem(); if (!item) return;
    ResourceTreeRequest request; request.command = command; request.type = static_cast<ResourceType>(item->data(0, ResourceTypeRole).toInt()); request.source = itemPath(item);
    request.destination = command == ResourceTreeCommand::Duplicate ? (item->parent() ? itemPath(item->parent()) : QList<int>()) : selectedGroupPath(request.type);
    emit treeCommandRequested(request);
}
