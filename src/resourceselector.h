#ifndef QTGMS_RESOURCESELECTOR_H
#define QTGMS_RESOURCESELECTOR_H
#include "project.h"
#include <QComboBox>
#include <QMenu>
class QMimeData;

class ResourceSelectionMenu : public QMenu
{
public:
    ResourceSelectionMenu(const QList<ResourceNode> &resources, const QString &current,
                          const QString &emptyLabel, const QString &emptyValue, QWidget *parent = nullptr,
                          const QStringList &excluded = QStringList());
};

// Resource values remain names; the popup follows the project's folder tree.
class ResourceComboBox : public QComboBox
{
public:
    explicit ResourceComboBox(QWidget *parent = nullptr);
    void setResources(const Project &project, ResourceType type, const QString &current,
                      const QString &emptyLabel, const QString &emptyValue = QStringLiteral("<undefined>"),
                      const QStringList &excluded = QStringList());
    void showPopup() override;
    void showPopupAt(const QPoint &globalPosition);
    int droppedResourceIndex(const QMimeData *data) const;
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
private:
    void updateCurrentIcon();
    ResourceType m_resourceType = ResourceType::Sprite;
    QList<ResourceNode> m_resources;
    QString m_emptyLabel, m_emptyValue;
    QStringList m_excluded;
    bool m_selecting = false;
};
#endif
