#ifndef QTGMS_ACTIONEDITORDIALOG_H
#define QTGMS_ACTIONEDITORDIALOG_H
#include "editordialog.h"
#include <QDomDocument>
#include <functional>
#include "project.h"
struct LibraryAction;
class ResourceComboBox;
class QCheckBox;
class QRadioButton;
class ActionEditorDialog : public EditorDialog
{
    Q_OBJECT
public:
    ActionEditorDialog(QDomElement action, const LibraryAction *definition, const Project &project, QWidget *parent = nullptr);
    QDomElement action() const { return m_xml.documentElement(); }
    void selectArgument(int argument, int offset, int length);
protected:
    void accept() override;
private:
    QDomDocument m_xml;
    ResourceComboBox *m_appliesTo = nullptr;
    QRadioButton *m_self = nullptr, *m_other = nullptr;
    QCheckBox *m_relative = nullptr, *m_not = nullptr;
    QList<std::function<QString()>> m_values;
    QList<QWidget *> m_argumentWidgets;
};
#endif
