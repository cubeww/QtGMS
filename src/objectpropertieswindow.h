#ifndef QTGMS_OBJECTPROPERTIESWINDOW_H
#define QTGMS_OBJECTPROPERTIESWINDOW_H
#include "resourceeditorwindow.h"
#include <QDomDocument>
#include <QMap>
#include <QPointer>
class CodeActionEditorWindow;
class ObjectDocument;
class ActionLibraryManager;
class ActionEditorPanel;
class QListWidget;
class QComboBox;
class ResourceComboBox;
class QSpinBox;
class QLabel;
class QCheckBox;
class ObjectPropertiesWindow : public ResourceEditorWindow
{
    Q_OBJECT
public:
    ObjectPropertiesWindow(ObjectDocument *document, Project *project, ActionLibraryManager *libraries, QWidget *parent = nullptr);
    bool save() override;
    QString filePath() const override;
    void relocate(const QString &oldDirectory, const QString &newDirectory) override;
    void updateResources();
    void setSpriteName(const QString &name);
    void showAction(int eventIndex, int actionIndex, int offset, int length, int argument);
    QDomDocument editingXml() const;
signals:
    void openResourceRequested(ResourceType type, const QString &path);
    void createSpriteRequested();
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void refresh();
    void refreshResources();
    void refreshActions();
    void applyProperties();
    void editEvent(bool change, bool duplicate = false);
    void deleteEvent();
    void editPhysics();
    void showInformation();
    void openCodeAction(int row, int offset, int length);
    void refreshCodeEditors();
    bool saveCodeEditors();
    bool closeCodeEditors();
    QDomElement selectedEvent(QDomDocument &xml) const;
    ObjectDocument *m_document;
    Project *m_project;
    ActionLibraryManager *m_libraries;
    QListWidget *m_events;
    ActionEditorPanel *m_actionEditor;
    ResourceComboBox *m_sprite, *m_parent, *m_mask;
    QSpinBox *m_depth;
    QLabel *m_spritePreview;
    QListWidget *m_children;
    QMap<QString, QCheckBox *> m_flags;
    bool m_refreshing = false;
    QMap<QString, QPointer<CodeActionEditorWindow>> m_codeEditors;
};
#endif
