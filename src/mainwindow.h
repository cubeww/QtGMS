#ifndef QTGMS_MAINWINDOW_H
#define QTGMS_MAINWINDOW_H

#include "editorwindow.h"
#include "project.h"
#include "resourcetreerequest.h"
#include "scriptsearch.h"
#include <QPointer>
#include <QHash>

class QAction;
class QComboBox;
class QMenu;
class CompilePanel;
class ResourceBrowser;
class ResourceEditorWindow;
class RoomPropertiesWindow;
class ActionLibraryManager;
class GameRunner;

class MainWindow : public EditorWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &projectPath = QString(), QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void createWorkspace();
    void createMenusAndToolbar();
    void openProject();
    void importProject();
    void importProjectFile(const QString &filePath);
    void rememberProject();
    void refreshRecentProjects();
    void newProject();
    bool saveProject();
    void exportProject();
    bool prepareToCloseProject();
    void loadProject(const QString &filePath);
    void closeProject();
    void updateProject();
    void updateProjectActions();
    void updateConfiguration(int index);
    void manageConfigurations();
    void processTreeCommand(const ResourceTreeRequest &request);
    bool compileProject();
    void runProject();
    bool stopGame();
    void cleanBuild();
    void updateBuildActions();
    void importDroppedFiles(const QStringList &paths, QWidget *parent);
    void removeResource(ResourceType type, const QString &filePath);
    void renameResource(ResourceType type, const QString &filePath, const QString &name);
    void openResource(ResourceType type, const QString &filePath);
    ResourceNode createResource(ResourceType type);
    bool closeResourceEditors();
    bool saveResources();
    void searchScripts();
    bool collectFontCharacters(QString &characters, QString &error);
    void openSearchMatch(const ScriptSearchMatch &match);

    Project m_project;
    QPointer<RoomPropertiesWindow> m_lastRoomEditor;
    ResourceBrowser *m_resourceBrowser;
    CompilePanel *m_compilePanel;
    QComboBox *m_configurationCombo;
    QAction *m_closeProjectAction;
    QAction *m_saveAction;
    QAction *m_saveAllAction;
    QAction *m_saveAsAction;
    QAction *m_exportProjectAction = nullptr;
    QMenu *m_recentProjectsMenu = nullptr;
    QAction *m_createPathAction;
    QAction *m_createSpriteAction;
    QAction *m_createBackgroundAction;
    QAction *m_createSoundAction;
    QAction *m_createScriptAction;
    QAction *m_createShaderAction;
    QAction *m_createFontAction;
    QAction *m_createObjectAction;
    QAction *m_createTimelineAction;
    QAction *m_createRoomAction;
    QAction *m_createExtensionAction;
    QAction *m_defineMacrosAction;
    QAction *m_gameSettingsAction;
    QAction *m_manageConfigurationsAction;
    QString m_activeConfigurationPath;
    ActionLibraryManager *m_actionLibraries;
    bool m_treeBusy = false;
    bool m_building = false;
    GameRunner *m_gameRunner;
    QAction *m_buildAction = nullptr;
    QAction *m_runAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_cleanAction = nullptr;
    bool m_fileDropPending = false;
    bool m_informationEdited = false;
    bool m_settingsEdited = false;
    QHash<QString, QPointer<ResourceEditorWindow>> m_resourceEditors;
    QAction *m_searchScriptsAction = nullptr;
    ScriptSearchOptions m_scriptSearchOptions;
};

#endif
