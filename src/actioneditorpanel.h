#ifndef QTGMS_ACTIONEDITORPANEL_H
#define QTGMS_ACTIONEDITORPANEL_H
#include <QWidget>
#include <QDomDocument>
class Project;
class ActionLibraryManager;
struct LibraryAction;
class ActionPalette;
class ActionList;
class QUndoStack;
class QVBoxLayout;
// Edits one action container; the host owns resource identity, storage and undo.
class ActionEditorPanel : public QWidget
{
    Q_OBJECT
public:
    ActionEditorPanel(Project *project, ActionLibraryManager *libraries, QUndoStack *undoStack, QWidget *parent = nullptr);
    void setActions(QDomElement container, const QString &contextName);
    void setListMargins(int left, int top, int right, int bottom);
    void openFirstCodeAction();
    void openAction(int row, int offset = -1, int length = 0, int argument = -1);
    void setModelessCodeEditing(bool enabled) { m_modelessCodeEditing = enabled; }
signals:
    void actionsEdited(const QDomElement &container, const QString &description);
    void codeActionRequested(int row, int offset, int length);
private:
    void refreshActions();
    void commit(const QDomDocument &xml, const QString &description);
    void addAction(int libraryId, int actionId, int before = -1);
    void editAction();
    QDomElement editActionValues(QDomElement action, const LibraryAction *definition, int row,
                                int offset = -1, int length = 0, int argument = -1);
    void deleteActions();
    void copyActions();
    void pasteActions();
    void moveActions(const QList<int> &rows, int before);
    Project *m_project;
    ActionLibraryManager *m_libraries;
    ActionList *m_actions;
    ActionPalette *m_palette;
    QVBoxLayout *m_listLayout;
    QDomDocument m_xml;
    QString m_contextName;
    bool m_modelessCodeEditing = false;
};
#endif
