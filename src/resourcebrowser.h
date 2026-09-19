#ifndef QTGMS_RESOURCEBROWSER_H
#define QTGMS_RESOURCEBROWSER_H

#include "project.h"
#include "resourcetreerequest.h"

#include <QWidget>
#include <QHash>
#include <QIcon>

class QCheckBox;
class QAction;
class QMenu;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class ResourceBrowser : public QWidget
{
    Q_OBJECT

public:
    explicit ResourceBrowser(QWidget *parent = nullptr);
    void setProject(const Project &project);
    void setConfiguration(const ProjectConfiguration *configuration);
    void refreshIcons(const Project &project);
    void refreshResourceIcons(const Project &project, ResourceType type, const QString &filePath);
    QList<int> selectedGroupPath(ResourceType type) const;
    void addResource(const ResourceNode &resource, const QList<int> &groupPath);
    void refreshInformation(const Project &project);
    void refreshMacros(const Project &project);
    void relocatePaths(const QString &oldDirectory, const QString &newDirectory);
    void addEditActions(QMenu *menu);
    void selectTreeNode(ResourceType type, const QList<int> &path);
    void selectResource(const QString &filePath);

signals:
    void treeCommandRequested(const ResourceTreeRequest &request);
    void resourceActivated(ResourceType type, const QString &filePath);
    void resourceClicked(ResourceType type, const QString &filePath);
    void createResourceRequested(ResourceType type);
    void removeResourceRequested(ResourceType type, const QString &filePath);
    void renameResourceRequested(ResourceType type, const QString &filePath, const QString &name);

public slots:
    void focusSearch();
    void expandAll();
    void collapseAll();
    void activateCurrentResource();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void createActions();
    QList<int> itemPath(QTreeWidgetItem *item) const;
    void requestTreeCommand(ResourceTreeCommand command);
    void updateActions();
    void showContextMenu(QTreeWidgetItem *item, const QPoint &globalPosition);
    void openResourceLocation();
    bool matchesSearch(const QTreeWidgetItem *item) const;
    bool filterItem(QTreeWidgetItem *item);
    void appendResources(QTreeWidgetItem *parent, const QList<ResourceNode> &resources);
    QIcon resourceIcon(const ResourceNode &resource);
    void updateSearch();
    void findMatch(int direction);

    QTreeWidget *m_resourceTree;
    QLineEdit *m_searchEdit;
    QCheckBox *m_wholeWordCheck;
    QCheckBox *m_filterTreeCheck;
    QPushButton *m_previousButton;
    QPushButton *m_nextButton;
    QTreeWidgetItem *m_macroItem;
    QTreeWidgetItem *m_settingsItem;
    QHash<QString, QIcon> m_thumbnailIcons;
    QString m_projectFilePath;
    QAction *m_addExistingAction;
    QAction *m_sortAction;
    QAction *m_referencesAction;
    QAction *m_createAction;
    QAction *m_duplicateAction;
    QAction *m_createGroupAction;
    QAction *m_deleteAction;
    QAction *m_renameAction;
    QAction *m_propertiesAction;
    QAction *m_openLocationAction;
};

#endif
