#ifndef QTGMS_OBJECTEVENTDIALOG_H
#define QTGMS_OBJECTEVENTDIALOG_H
#include "editordialog.h"
#include <QDomElement>
#include <QIcon>
#include <QMap>
class Project;
class QPushButton;
class ObjectEventDialog : public EditorDialog
{
    Q_OBJECT
public:
    ObjectEventDialog(const Project &project, QWidget *parent = nullptr);
    QDomElement event(QDomDocument &xml) const;
    void selectEvent(QDomElement event);
    static QString eventName(QDomElement event);
    static QIcon eventIcon(QDomElement event);
private:
    void chooseEvent(int category, QPushButton *button);
    const Project &m_project;
    QMap<int, QPushButton *> m_buttons;
    int m_type = -1;
    int m_number = 0;
    QString m_collisionObject;
};
#endif
