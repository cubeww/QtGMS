#ifndef QTGMS_EXTENSIONPROPERTIESWINDOW_H
#define QTGMS_EXTENSIONPROPERTIESWINDOW_H
#include "resourceeditorwindow.h"
#include "extensiondocument.h"
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QAction;
class ExtensionPropertiesWindow : public ResourceEditorWindow
{
    Q_OBJECT
public:
    ExtensionPropertiesWindow(ExtensionDocument *document, QWidget *parent = nullptr);
    ~ExtensionPropertiesWindow() override;
    bool save() override;
    QString filePath() const override;
    void relocate(const QString &oldDirectory, const QString &newDirectory) override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void refresh();
    void updateActions();
    void addFile(bool placeholder);
    void addMember(bool function);
    void editSelected();
    void removeSelected();
    void editSource();
    QDomElement selectedFile(ExtensionState &state) const;
    ExtensionDocument *m_document;
    QTreeWidget *m_tree;
    QLabel *m_description;
    QAction *m_addFunction, *m_addConstant, *m_edit, *m_delete, *m_editSource;
    bool m_refreshing = false;
};
#endif
