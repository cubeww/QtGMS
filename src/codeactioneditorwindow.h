#ifndef QTGMS_CODEACTIONEDITORWINDOW_H
#define QTGMS_CODEACTIONEDITORWINDOW_H

#include "codesnippeteditorwindow.h"
#include <QDomDocument>
#include <functional>

class Project;
class CodeEditorPanel;
class QTextDocument;
class QRadioButton;
class ResourceComboBox;
class QCheckBox;

// A private editing transaction. The action host commits the result to its undo stack.
class CodeActionEditorWindow : public CodeSnippetEditorWindow
{
    Q_OBJECT
public:
    CodeActionEditorWindow(QDomElement action, const Project &project, const QString &contextName, QWidget *parent);
    QDomElement action() const;
    void setCommitHandler(const std::function<bool(QDomElement, QDomElement)> &handler);
    bool commitChanges() override;
    void refreshAction(QDomElement action);
    bool hasChanges() const override;
private:
    QDomDocument m_xml;
    QRadioButton *m_self = nullptr;
    QRadioButton *m_other = nullptr;
    QRadioButton *m_objectChoice = nullptr;
    ResourceComboBox *m_object = nullptr;
    QCheckBox *m_relative = nullptr;
    QCheckBox *m_not = nullptr;
    std::function<bool(QDomElement, QDomElement)> m_commitHandler;
};

#endif
