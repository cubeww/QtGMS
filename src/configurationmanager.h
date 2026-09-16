#ifndef QTGMS_CONFIGURATIONMANAGER_H
#define QTGMS_CONFIGURATIONMANAGER_H
#include "editordialog.h"
#include "project.h"
#include <functional>
class QListWidget;
class QPushButton;
class ConfigurationManager : public EditorDialog
{
    Q_OBJECT
public:
    ConfigurationManager(const QList<ProjectConfiguration> &configurations, int currentIndex,
        const std::function<bool(const QList<ConfigurationEdit> &)> &apply, QWidget *parent = nullptr);
    QString selectedName() const;
private:
    void refresh(int selected);
    void updateButtons();
    void add();
    void rename();
    void remove();
    void move(int delta);
    bool validName(const QString &name, int except = -1);
    QList<ConfigurationEdit> m_edits;
    QListWidget *m_list;
    QPushButton *m_deleteButton;
    QPushButton *m_renameButton;
    QPushButton *m_upButton;
    QPushButton *m_downButton;
};
#endif
