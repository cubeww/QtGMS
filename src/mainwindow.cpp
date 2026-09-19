#include "actionxml.h"
#include "roomassets.h"
#include "preferencesdialog.h"
#include "resourcereferences.h"
#include <QShortcut>
#include "configurationmanager.h"
#include <QPushButton>
#include "gamesettingsdocument.h"
#include "gamesettingswindow.h"
#include "editorstandarddialogs.h"
#include "gameinformationwindow.h"
#include "pathdocument.h"
#include "pathpropertieswindow.h"
#include "macrodocument.h"
#include "macroeditorwindow.h"
#include "extensiondocument.h"
#include "extensionpropertieswindow.h"
#include "roomdocument.h"
#include "roompropertieswindow.h"
#include "mainwindow.h"

#include "compilepanel.h"
#include "gamerunner.h"
#include "projectloader.h"
#include "projectimportjob.h"
#include "resourcebrowser.h"
#include "spritedocument.h"
#include "spritepropertieswindow.h"
#include "backgrounddocument.h"
#include "backgroundpropertieswindow.h"
#include "sounddocument.h"
#include "soundpropertieswindow.h"
#include "textfiledocument.h"
#include "scripteditorwindow.h"
#include "gmlsymbols.h"
#include "codesnippeteditorwindow.h"
#include "shaderdocument.h"
#include "shadereditorwindow.h"
#include "fontdocument.h"
#include "fontpropertieswindow.h"
#include "objectdocument.h"
#include "objectpropertieswindow.h"
#include "timelinedocument.h"
#include "timelinepropertieswindow.h"
#include "actionlibrary.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QSignalBlocker>
#include <QSettings>
#include <QStandardPaths>
#include <QToolBar>
#include <QTimer>

static QAction *addUnavailableAction(QMenu *menu, const QString &text,
                                     const QString &iconName = QString(),
                                     const QKeySequence &shortcut = QKeySequence())
{
    auto *action = menu->addAction(text);
    if (!iconName.isEmpty())
        action->setIcon(QIcon(QStringLiteral(":/images/%1.png").arg(iconName)));
    action->setShortcut(shortcut);
    action->setEnabled(false);
    return action;
}

static QString resourceEditorKey(ResourceType type, const QString &path)
{ return QString::number(static_cast<int>(type)) + QLatin1Char('|') + path; }

static void showResourceEditor(ResourceEditorWindow *window)
{
    QWidget *modal = QApplication::activeModalWidget();
    if (modal && modal != window && !window->isAncestorOf(modal) && window->parentWidget() != modal) {
        // Keep a resource opened from a code transaction inside its modal branch.
        // Window modality also prevents closing that transaction beneath the resource.
        const QPoint position = window->pos();
        window->setParent(modal, window->windowFlags());
        window->setWindowModality(Qt::WindowModal);
        window->move(position);
    }
    window->showNormal(); window->raise(); window->activateWindow();
}

MainWindow::MainWindow(const QString &projectPath, QWidget *parent)
    : EditorWindow(parent),
      m_roomAssets(new RoomAssets(&m_project)),
      m_resourceBrowser(new ResourceBrowser(this)),
      m_compilePanel(new CompilePanel(this)),
      m_configurationCombo(new QComboBox(this)),
      m_closeProjectAction(nullptr), m_saveAction(nullptr), m_saveAllAction(nullptr), m_saveAsAction(nullptr),
      m_createPathAction(nullptr), m_createSpriteAction(nullptr), m_createBackgroundAction(nullptr), m_createSoundAction(nullptr), m_createScriptAction(nullptr), m_createShaderAction(nullptr), m_createFontAction(nullptr),
      m_createObjectAction(nullptr), m_createTimelineAction(nullptr), m_createRoomAction(nullptr), m_createExtensionAction(nullptr), m_defineMacrosAction(nullptr), m_actionLibraries(new ActionLibraryManager(this)), m_gameRunner(new GameRunner(this))
{
    qRegisterMetaType<ResourceType>("ResourceType");
    qRegisterMetaType<ResourceTreeRequest>("ResourceTreeRequest");
    setObjectName(QStringLiteral("mainWindow"));
    qApp->installEventFilter(this);
    setWindowTitle(QStringLiteral("QtGMS"));
    resize(1362, 821);
    setMinimumSize(900, 540);
    setDockOptions(QMainWindow::AnimatedDocks);
    createWorkspace();
    createMenusAndToolbar();
    connect(m_gameRunner, &GameRunner::runningChanged, this, &MainWindow::updateBuildActions);
    connect(m_gameRunner, &GameRunner::message, m_compilePanel, &CompilePanel::appendMessage);
    updateProject();
    connect(m_resourceBrowser, &ResourceBrowser::resourceActivated, this, &MainWindow::openResource);
    connect(m_resourceBrowser, &ResourceBrowser::resourceClicked, this, [this](ResourceType type, const QString &path) {
        if (type != ResourceType::Object || !m_lastRoomEditor || !m_lastRoomEditor->isVisible()
            || m_lastRoomEditor->isMinimized()
            || !QSettings().value(QStringLiteral("roomEditor/selectObjectFromTree"), true).toBool()) return;
        for (const auto &resource : ActionXml::resourceList(m_project, ResourceType::Object)) {
            if (resource.filePath.compare(path, Qt::CaseInsensitive) != 0) continue;
            m_lastRoomEditor->selectPlacementObject(resource.name);
            break;
        }
    });
    connect(this, &EditorWindow::resourceNameActivated, this, [this](const QString &name) {
        for (ResourceType type : {ResourceType::Sprite, ResourceType::Sound, ResourceType::Background,
                                 ResourceType::Path, ResourceType::Script, ResourceType::Shader, ResourceType::Font,
                                 ResourceType::Timeline, ResourceType::Object, ResourceType::Room, ResourceType::Macro}) {
            for (const auto &resource : ActionXml::resourceList(m_project, type)) {
                if (type == ResourceType::Script) {
                    for (const auto &script : GmlSymbols::instance().scriptItems(resource.name, resource.filePath)) {
                        if (script.name != name) continue;
                        openResource(type, resource.filePath);
                        if (auto *window = qobject_cast<ScriptEditorWindow *>(m_resourceEditors.value(resourceEditorKey(type, resource.filePath)).data()))
                            window->selectScript(name);
                        return;
                    }
                }
                if (resource.name == name) {
                    openResource(type, type == ResourceType::Macro ? m_project.filePath() : resource.filePath);
                    return;
                }
            }
        }
    });
    connect(m_resourceBrowser, &ResourceBrowser::treeCommandRequested, this, &MainWindow::processTreeCommand, Qt::QueuedConnection);
    connect(m_resourceBrowser, &ResourceBrowser::removeResourceRequested, this, &MainWindow::removeResource);
    connect(m_resourceBrowser, &ResourceBrowser::renameResourceRequested, this, &MainWindow::renameResource);
    connect(m_resourceBrowser, &ResourceBrowser::createResourceRequested, this, &MainWindow::createResource);
    // Windows file associations pass the selected file as a startup argument.
    // Wait until the main window is shown before opening projects or dialogs.
    QTimer::singleShot(0, this, [this, projectPath] {
        if (projectPath.isEmpty())
            newProject();
        else if (ProjectImportJob::supportsFile(projectPath))
            importProjectFile(projectPath);
        else
            loadProject(projectPath);
    });
}

MainWindow::~MainWindow()
{
    m_gameRunner->disconnect(this);
    m_gameRunner->stop();
    qApp->removeEventFilter(this);
    // Destroy editors while their registry and the shared actions still exist.
    const auto windows = m_resourceEditors.values();
    for (const auto &window : windows) delete window.data();
}

void MainWindow::openProject()
{
    const QString directory = m_project.isOpen() && !m_project.isTemporary()
        ? QFileInfo(m_project.filePath()).absolutePath() : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString filePath = QFileDialog::getOpenFileName(this, tr("Open Project"),
        directory, tr("GameMaker Studio Projects (*.project.gmx)"));
    if (!filePath.isEmpty())
        loadProject(filePath);
}

void MainWindow::newProject()
{
    Project created;
    QString error;
    if (!Project::createTemporary(created, error)) {
        EditorMessageBox::critical(this, tr("Cannot Create Project"), error); return;
    }
    if (!prepareToCloseProject()) return;
    m_project = created;
    updateProject();
    m_compilePanel->setMessages(QStringList());
}

bool MainWindow::saveProject()
{
    if (!m_project.isOpen()) return false;
    if (!m_project.isTemporary()) {
        if (!saveResources()) return false;
        rememberProject();
        return true;
    }
    QFileDialog dialog(this, tr("Save As"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(QFileInfo(m_project.filePath()).fileName()),
        tr("GameMaker Studio Projects (*.project.gmx)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setDefaultSuffix(QStringLiteral("project.gmx"));
    while (dialog.exec() == QDialog::Accepted) {
        if (!saveResources() || !stopGame()) return false;
        // Keep temporary storage alive while open editors change their paths.
        const Project previous = m_project;
        const QString oldDirectory = QFileInfo(previous.filePath()).absolutePath();
        QString error;
        if (!m_project.saveTemporaryAs(dialog.selectedFiles().first(), error)) {
            EditorMessageBox::critical(this, tr("Cannot Save Project"), error); continue;
        }
        const QString newDirectory = QFileInfo(m_project.filePath()).absolutePath();
        m_roomAssets->reload();
        const auto windows = m_resourceEditors.values();
        m_resourceEditors.clear();
        for (const auto &window : windows) {
            if (!window) continue;
            window->relocate(oldDirectory, newDirectory);
            const QString configPath = window->property("configurationPath").toString();
            if (!configPath.isEmpty()) window->setProperty("configurationPath", QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(configPath)));
            m_resourceEditors.insert(resourceEditorKey(static_cast<ResourceType>(window->property("resourceType").toInt()), window->filePath()), window);
        }
        m_resourceBrowser->relocatePaths(oldDirectory, newDirectory);
        m_resourceBrowser->refreshMacros(m_project);
        m_resourceBrowser->refreshIcons(m_project);
        if (!m_activeConfigurationPath.isEmpty()) m_activeConfigurationPath = QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(m_activeConfigurationPath));
        updateConfiguration(m_configurationCombo->currentIndex());
        updateProjectActions();
        rememberProject();
        return true;
    }
    return false;
}

bool MainWindow::prepareToCloseProject()
{
    if (m_building) return false;
    bool hasUnsavedMacros = false;
    for (const auto &window : m_resourceEditors)
        if (auto *macros = qobject_cast<MacroEditorWindow *>(window.data())) hasUnsavedMacros = hasUnsavedMacros || macros->isModified();
    bool hasInformationChanges = m_informationEdited;
    for (const auto &window : m_resourceEditors)
        if (auto *information = qobject_cast<GameInformationWindow *>(window.data())) hasInformationChanges = hasInformationChanges || information->isModified();
    bool hasSettingsChanges = m_settingsEdited;
    for (const auto &window : m_resourceEditors)
        if (auto *settings = qobject_cast<GameSettingsWindow *>(window.data())) hasSettingsChanges = hasSettingsChanges || settings->isModified();
    if (m_project.isTemporary() && (m_project.isImported() || m_project.resourceCount() > 0 || hasUnsavedMacros || hasInformationChanges || hasSettingsChanges)) {
        const auto answer = EditorMessageBox::question(this, tr("Save Project"), tr("Save this project before closing it?"),
            EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel) return false;
        if (answer == EditorMessageBox::Save && !saveProject()) return false;
        if (answer == EditorMessageBox::Discard) {
            if (!stopGame()) return false;
            const auto windows = m_resourceEditors.values();
            for (const auto &window : windows) delete window.data();
            return true;
        }
    }
    return closeResourceEditors() && stopGame();
}

void MainWindow::loadProject(const QString &filePath)
{
    Project project;
    ProjectLoader loader;
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool loaded = loader.load(filePath, project, error);
    if (!loaded) {
        QApplication::restoreOverrideCursor();
        EditorMessageBox::critical(this, tr("Cannot Open Project"), error);
        return;
    }

    QApplication::restoreOverrideCursor();
    if (!prepareToCloseProject()) return;
    m_project = project;
    updateProject();
    rememberProject();
    QStringList messages;
    messages.append(tr("Opened project: %1").arg(QDir::toNativeSeparators(m_project.filePath())));
    messages.append(tr("Indexed %1 resources and %2 configurations.")
        .arg(m_project.resourceCount()).arg(m_project.configurations().size()));
    if (!m_project.warnings().isEmpty()) {
        messages.append(tr("Loaded with %1 warning(s):").arg(m_project.warnings().size()));
        messages.append(m_project.warnings());
        m_compilePanel->show();
    }
    m_compilePanel->setMessages(messages);
}

void MainWindow::closeProject()
{
    if (!prepareToCloseProject()) return;
    m_project = Project();
    updateProject();
    m_compilePanel->setMessages(QStringList());
}

ResourceNode MainWindow::createResource(ResourceType type)
{
    if (!m_project.isOpen()) return ResourceNode();
    const QList<int> groupPath = m_resourceBrowser->selectedGroupPath(type);
    ResourceNode resource;
    QString error;
    if (!m_project.createResource(type, groupPath, resource, error)) {
        EditorMessageBox::critical(this, tr("Cannot Create Resource"), error);
        return ResourceNode();
    }
    m_resourceBrowser->addResource(resource, groupPath);
    m_roomAssets->invalidate(type, resource.filePath);
    for (const auto &editor : m_resourceEditors) {
        if (auto *objectWindow = qobject_cast<ObjectPropertiesWindow *>(editor.data())) objectWindow->updateResources();
        if (type == ResourceType::Object || type == ResourceType::Sprite || type == ResourceType::Background)
            if (auto *room = qobject_cast<RoomPropertiesWindow *>(editor.data())) room->updateResources();
    }
    openResource(type, resource.filePath);
    return resource;
}

void MainWindow::openResource(ResourceType type, const QString &filePath)
{
    if (type != ResourceType::Sprite && type != ResourceType::Background && type != ResourceType::Sound
        && type != ResourceType::Script && type != ResourceType::Shader && type != ResourceType::Font && type != ResourceType::Object && type != ResourceType::Timeline && type != ResourceType::Room && type != ResourceType::Extension && type != ResourceType::Macro && type != ResourceType::Path && type != ResourceType::GameInformation && type != ResourceType::GameSettings) return;
    if (type == ResourceType::GameInformation && filePath.isEmpty()) {
        QString error;
        if (!m_project.ensureInformationFile(error)) { EditorMessageBox::warning(this, tr("Cannot Open Game Information"), error); return; }
        m_resourceBrowser->refreshInformation(m_project);
        openResource(type, m_project.informationFilePath()); return;
    }
    if (m_resourceEditors.value(resourceEditorKey(type, filePath))) {
        auto *window = m_resourceEditors.value(resourceEditorKey(type, filePath)).data();
        showResourceEditor(window); return;
    }
    QStringList textureGroups;
    const int configIndex = m_configurationCombo->currentIndex();
    if (configIndex >= 0 && configIndex < m_project.configurations().size()) {
        const QMap<QString, QString> &options = m_project.configurations().at(configIndex).options;
        const int count = options.value(QStringLiteral("option_textureGroup_count")).toInt();
        for (int i = 0; i < count; ++i) textureGroups.append(options.value(QStringLiteral("option_textureGroups%1").arg(i), QString::number(i)));
    }
    if (textureGroups.isEmpty()) textureGroups.append(QStringLiteral("Default"));
    ResourceEditorWindow *window = nullptr;
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (type == ResourceType::Sprite) {
        auto *document = new SpriteDocument;
        if (document->load(filePath, configIndex, error)) window = new SpritePropertiesWindow(document, textureGroups, this);
        else delete document;
    } else if (type == ResourceType::GameSettings) {
        auto *document = new GameSettingsDocument(&m_project);
        if (document->load(filePath, error)) window = new GameSettingsWindow(document, this); else delete document;
    } else if (type == ResourceType::GameInformation) {
        auto *information = new GameInformationWindow(this);
        if (information->load(filePath, error)) window = information; else delete information;
    } else if (type == ResourceType::Path) {
        auto *document = new PathDocument;
        if (document->load(filePath, error)) window = new PathPropertiesWindow(document, &m_project, m_roomAssets, this);
        else delete document;
    } else if (type == ResourceType::Macro) {
        auto *document = new MacroDocument(&m_project);
        if (document->load(filePath, error)) window = new MacroEditorWindow(document, this);
        else delete document;
    } else if (type == ResourceType::Extension) {
        auto *document = new ExtensionDocument;
        if (document->load(filePath, error)) window = new ExtensionPropertiesWindow(document, this);
        else delete document;
    } else if (type == ResourceType::Room) {
        auto *document = new RoomDocument;
        if (document->load(filePath, error)) {
            auto *room = new RoomPropertiesWindow(document, &m_project, m_roomAssets, this);
            connect(room, &RoomPropertiesWindow::openResourceRequested, this, &MainWindow::openResource); window = room;
        } else delete document;
    } else if (type == ResourceType::Timeline) {
        auto *document = new TimelineDocument;
        if (document->load(filePath, error)) window = new TimelinePropertiesWindow(document, &m_project, m_actionLibraries, this);
        else delete document;
    } else if (type == ResourceType::Object) {
        auto *document = new ObjectDocument;
        if (document->load(filePath, error)) {
            auto *objectWindow = new ObjectPropertiesWindow(document, &m_project, m_actionLibraries, this);
            connect(objectWindow, &ObjectPropertiesWindow::openResourceRequested, this, &MainWindow::openResource);
            connect(objectWindow, &ObjectPropertiesWindow::createSpriteRequested, this, [this, objectWindow] {
                const ResourceNode sprite = createResource(ResourceType::Sprite);
                if (!sprite.name.isEmpty()) objectWindow->setSpriteName(sprite.name);
            });
            window = objectWindow;
        } else delete document;
    } else if (type == ResourceType::Font) {
        auto *document = new FontDocument;
        if (document->load(filePath, configIndex, error))
            window = new FontPropertiesWindow(document, textureGroups,
                [this](QString &characters, QString &scanError) { return collectFontCharacters(characters, scanError); }, this);
        else delete document;
    } else if (type == ResourceType::Shader) {
        auto *document = new ShaderDocument;
        if (document->load(filePath, error)) window = new ShaderEditorWindow(document, &m_project, this);
        else delete document;
    } else if (type == ResourceType::Script) {
        auto *document = new TextFileDocument;
        if (document->load(filePath, error)) window = new ScriptEditorWindow(document, m_project, this);
        else delete document;
    } else if (type == ResourceType::Sound) {
        auto *document = new SoundDocument;
        if (document->load(filePath, configIndex, error)) window = new SoundPropertiesWindow(document, m_project.audioGroups(), this);
        else delete document;
    } else {
        auto *document = new BackgroundDocument;
        if (document->load(filePath, configIndex, error)) window = new BackgroundPropertiesWindow(document, textureGroups, this);
        else delete document;
    }
    QApplication::restoreOverrideCursor();
    if (!window) { EditorMessageBox::critical(this, tr("Cannot Open Resource"), error); return; }
    window->setProperty("resourceType", static_cast<int>(type));
    window->setProperty("configurationPath", configIndex >= 0 && configIndex < m_project.configurations().size() ? m_project.configurations().at(configIndex).filePath : QString());
    m_resourceEditors.insert(resourceEditorKey(type, filePath), window);
    connect(window, &ResourceEditorWindow::renameResourceRequested, this, &MainWindow::renameResource, Qt::QueuedConnection);
    if (!ResourceReferences::tag(type).isEmpty()) {
        auto *renameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), window);
        const QPointer<ResourceEditorWindow> editor = window;
        connect(renameShortcut, &QShortcut::activated, this, [this, type, editor] { if (editor) renameResource(type, editor->filePath(), QString()); }, Qt::QueuedConnection);
    }
    connect(window, &ResourceEditorWindow::saveProjectRequested, this, [this] { saveProject(); });
    connect(window, &QObject::destroyed, this, [this] {
        m_roomAssets->trimCache();
        for (auto it = m_resourceEditors.begin(); it != m_resourceEditors.end();) {
            if (it.value().isNull()) it = m_resourceEditors.erase(it);
            else ++it;
        }
    });
    connect(window, &ResourceEditorWindow::resourceSaved, this, [this](ResourceType savedType, const QString &path, const QString &thumbnail) {
        if (savedType == ResourceType::GameInformation) { m_informationEdited = true; return; }
        if (savedType == ResourceType::GameSettings) {
            m_settingsEdited = true;
            QStringList groups;
            for (const auto &configuration : m_project.configurations()) if (configuration.filePath == path) {
                const int count = qBound(1, configuration.options.value(QStringLiteral("option_textureGroup_count"), QStringLiteral("1")).toInt(), 4096);
                for (int i = 0; i < count; ++i) groups.append(configuration.options.value(QStringLiteral("option_textureGroups%1").arg(i), i == 0 ? QStringLiteral("Default") : QString::number(i)));
            }
            for (const auto &editor : m_resourceEditors) {
                if (auto *sound = qobject_cast<SoundPropertiesWindow *>(editor.data())) sound->setAudioGroups(m_project.audioGroups());
                if (!editor || editor->property("configurationPath").toString() != path) continue;
                if (auto *sprite = qobject_cast<SpritePropertiesWindow *>(editor.data())) sprite->setTextureGroups(groups);
                if (auto *background = qobject_cast<BackgroundPropertiesWindow *>(editor.data())) background->setTextureGroups(groups);
                if (auto *font = qobject_cast<FontPropertiesWindow *>(editor.data())) font->setTextureGroups(groups);
            }
            return;
        }
        if (savedType == ResourceType::Macro) { m_resourceBrowser->refreshMacros(m_project); return; }
        const bool visualResource = savedType == ResourceType::Object || savedType == ResourceType::Sprite || savedType == ResourceType::Background;
        if (visualResource) {
            m_project.updateThumbnail(savedType, path, thumbnail);
            m_roomAssets->invalidate(savedType, path);
            m_resourceBrowser->refreshResourceIcons(m_project, savedType, path);
        }
        for (const auto &editor : m_resourceEditors) {
            if (savedType == ResourceType::Room || visualResource)
                if (auto *pathWindow = qobject_cast<PathPropertiesWindow *>(editor.data())) pathWindow->updateResources();
            if (savedType == ResourceType::Object || savedType == ResourceType::Sprite)
                if (auto *objectWindow = qobject_cast<ObjectPropertiesWindow *>(editor.data())) objectWindow->updateResources();
            if (visualResource)
                if (auto *room = qobject_cast<RoomPropertiesWindow *>(editor.data())) room->updateResources();
        }
    });
    window->move(geometry().center() - window->rect().center());
    showResourceEditor(window);
}
void MainWindow::removeResource(ResourceType type, const QString &filePath)
{
    QString name;
    for (const auto &resource : ActionXml::resourceList(m_project, type)) if (resource.filePath == filePath) name = resource.name;
    if (name.isEmpty() || ResourceReferences::tag(type).isEmpty()) return;
    if (EditorMessageBox::question(this, tr("Delete Resource"),
            tr("Delete %1 and its resource files?\n\nReferences will be cleared and matching room instances or tiles removed. Open editors will be saved; this resource's editor will be closed.").arg(name),
            EditorMessageBox::Yes | EditorMessageBox::No, EditorMessageBox::No) != EditorMessageBox::Yes) return;
    if (!saveResources()) return;
    struct OpenResource { ResourceType type; QString path; QRect geometry; bool maximized; };
    QList<OpenResource> opened;
    const auto windows = m_resourceEditors.values();
    for (const auto &window : windows) if (window) {
        OpenResource item; item.type = static_cast<ResourceType>(window->property("resourceType").toInt()); item.path = window->filePath(); item.geometry = window->normalGeometry(); item.maximized = window->isMaximized(); opened.append(item);
    }
    if (!closeResourceEditors()) return;
    // Destroy children and release asset handles before touching files. QPointer also
    // prevents deferred close events from leaving stale editors in the registry.
    for (const auto &window : windows) delete window.data();
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool removed = m_project.removeResource(type, filePath, error);
    QApplication::restoreOverrideCursor();
    if (removed) {
        m_roomAssets->reload();
        m_settingsEdited = true;
        m_resourceBrowser->setProject(m_project);
        updateConfiguration(m_configurationCombo->currentIndex());
    }
    for (const auto &item : opened) {
        if (removed && item.type == type && item.path == filePath) continue;
        openResource(item.type, item.path);
        if (auto *editor = m_resourceEditors.value(resourceEditorKey(item.type, item.path)).data()) {
            if (item.geometry.isValid()) editor->setGeometry(item.geometry);
            if (item.maximized) editor->showMaximized();
        }
    }
    if (!error.isEmpty()) EditorMessageBox::warning(this, removed ? tr("Resource Deleted") : tr("Cannot Delete Resource"), error);
}

void MainWindow::renameResource(ResourceType type, const QString &filePath, const QString &requestedName)
{
    QString oldName;
    for (const auto &resource : ActionXml::resourceList(m_project, type)) if (resource.filePath == filePath) oldName = resource.name;
    if (oldName.isEmpty()) return;
    QString name = requestedName;
    if (name.isNull()) {
        EditorInputDialog dialog(this); dialog.setWindowTitle(tr("Rename Resource")); dialog.setLabelText(tr("Name:")); dialog.setTextValue(oldName);
        if (dialog.exec() != QDialog::Accepted) return; name = dialog.textValue().trimmed();
    }
    if (name == oldName) return;
    QString error;
    if (!m_project.validateResourceName(type, filePath, name, error)) { EditorMessageBox::warning(this, tr("Cannot Rename Resource"), error); return; }
    if (!saveResources()) return;
    struct OpenResource { ResourceType type; QString path; QRect geometry; bool maximized; };
    QList<OpenResource> opened;
    const auto windows = m_resourceEditors.values();
    for (const auto &window : windows) if (window) {
        OpenResource item; item.type = static_cast<ResourceType>(window->property("resourceType").toInt()); item.path = window->filePath(); item.geometry = window->normalGeometry(); item.maximized = window->isMaximized(); opened.append(item);
    }
    if (!closeResourceEditors()) return;
    for (const auto &window : windows) delete window.data();
    QString newPath;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool renamed = m_project.renameResource(type, filePath, name, newPath, error);
    QApplication::restoreOverrideCursor();
    if (renamed) {
        m_roomAssets->reload();
        m_settingsEdited = true; m_resourceBrowser->setProject(m_project); updateConfiguration(m_configurationCombo->currentIndex()); m_resourceBrowser->selectResource(newPath);
    }
    for (const auto &item : opened) {
        const QString path = renamed && item.path == filePath ? newPath : item.path;
        openResource(item.type, path);
        if (auto *editor = m_resourceEditors.value(resourceEditorKey(item.type, path)).data()) { if (item.geometry.isValid()) editor->setGeometry(item.geometry); if (item.maximized) editor->showMaximized(); }
    }
    if (auto *editor = m_resourceEditors.value(resourceEditorKey(type, renamed ? newPath : filePath)).data()) { editor->raise(); editor->activateWindow(); }
    if (!renamed) EditorMessageBox::warning(this, tr("Cannot Rename Resource"), error);
}

bool MainWindow::closeResourceEditors()
{
    // A code transaction runs a nested event loop and commits to its host on
    // return. Resource-wide operations must not delete that host underneath it.
    for (auto *code : findChildren<CodeSnippetEditorWindow *>()) {
        if (code->isVisible() && code->windowModality() != Qt::NonModal) {
            EditorMessageBox::information(QApplication::activeModalWidget(), tr("Code Editor"),
                tr("Close the open code editor before performing an operation that closes resource editors."));
            return false;
        }
    }
    const auto windows = m_resourceEditors.values();
    for (const auto &window : windows)
        if (window && !window->close()) return false;
    return true;
}
bool MainWindow::saveResources()
{
    for (const auto &window : m_resourceEditors)
        if (window && !window->save()) return false;
    return true;
}
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!prepareToCloseProject()) { event->ignore(); return; }
    event->accept();
}

void MainWindow::updateProjectActions()
{
    setWindowTitle(m_project.isOpen()
        ? QFileInfo(m_project.filePath()).fileName() + (m_project.isTemporary() ? QStringLiteral(" * - QtGMS") : QStringLiteral(" - QtGMS"))
        : QStringLiteral("QtGMS"));
    m_closeProjectAction->setEnabled(m_project.isOpen());
    m_createPathAction->setEnabled(m_project.isOpen());
    m_createSpriteAction->setEnabled(m_project.isOpen());
    m_createBackgroundAction->setEnabled(m_project.isOpen());
    m_createSoundAction->setEnabled(m_project.isOpen());
    m_createScriptAction->setEnabled(m_project.isOpen());
    m_createShaderAction->setEnabled(m_project.isOpen());
    m_createFontAction->setEnabled(m_project.isOpen());
    m_createObjectAction->setEnabled(m_project.isOpen());
    m_createTimelineAction->setEnabled(m_project.isOpen());
    m_createRoomAction->setEnabled(m_project.isOpen());
    m_createExtensionAction->setEnabled(m_project.isOpen());
    m_defineMacrosAction->setEnabled(m_project.isOpen());
    m_searchScriptsAction->setEnabled(m_project.isOpen());
    m_gameSettingsAction->setEnabled(!m_project.configurations().isEmpty());
    m_manageConfigurationsAction->setEnabled(!m_project.configurations().isEmpty());
    m_saveAction->setEnabled(m_project.isOpen());
    m_saveAllAction->setEnabled(m_project.isOpen());
    m_saveAsAction->setEnabled(m_project.isTemporary());
    m_exportProjectAction->setEnabled(m_project.isOpen());
    updateBuildActions();
}

void MainWindow::updateProject()
{
    m_roomAssets->reload();
    m_activeConfigurationPath.clear();
    m_informationEdited = false;
    m_settingsEdited = false;
    updateProjectActions();
    m_resourceBrowser->setProject(m_project);

    const QSignalBlocker blocker(m_configurationCombo);
    m_configurationCombo->clear();
    for (const ProjectConfiguration &configuration : m_project.configurations())
        m_configurationCombo->addItem(configuration.name);
    if (!m_project.isOpen())
        m_configurationCombo->addItem(QStringLiteral("Default"));
    m_configurationCombo->setEnabled(!m_project.configurations().isEmpty());
    const int defaultIndex = m_configurationCombo->findText(QStringLiteral("Default"));
    if (defaultIndex >= 0)
        m_configurationCombo->setCurrentIndex(defaultIndex);
    updateConfiguration(m_configurationCombo->currentIndex());
}

void MainWindow::updateConfiguration(int index)
{
    const auto &configurations = m_project.configurations();
    const QString path = index >= 0 && index < configurations.size() ? configurations.at(index).filePath : QString();
    if (!m_activeConfigurationPath.isEmpty() && path != m_activeConfigurationPath) {
        const auto windows = m_resourceEditors.values();
        for (const auto &window : windows) {
            if (!window) continue;
            const ResourceType type = static_cast<ResourceType>(window->property("resourceType").toInt());
            if (type != ResourceType::Sprite && type != ResourceType::Background && type != ResourceType::Font && type != ResourceType::Sound) continue;
            if (!window->save() || !window->close()) {
                const QSignalBlocker blocker(m_configurationCombo);
                for (int i = 0; i < configurations.size(); ++i) if (configurations.at(i).filePath == m_activeConfigurationPath) m_configurationCombo->setCurrentIndex(i);
                return;
            }
            // close() defers deletion; remove the old document before a resource can reopen.
            delete window.data();
        }
    }
    m_activeConfigurationPath = path;
    m_resourceBrowser->setConfiguration(index >= 0 && index < configurations.size() ? &configurations.at(index) : nullptr);
}

void MainWindow::manageConfigurations()
{
    if (m_project.configurations().isEmpty()) return;
    ConfigurationManager dialog(m_project.configurations(), m_configurationCombo->currentIndex(), [this](const QList<ConfigurationEdit> &edits) {
        if (!saveResources() || !closeResourceEditors()) return false;
        const auto windows = m_resourceEditors.values(); for (const auto &window : windows) delete window.data();
        QString error;
        if (!m_project.saveConfigurations(edits, error)) { EditorMessageBox::warning(this, tr("Cannot Save Configurations"), error); return false; }
        m_settingsEdited = true; m_activeConfigurationPath.clear();
        m_resourceBrowser->setProject(m_project);
        const QSignalBlocker blocker(m_configurationCombo); m_configurationCombo->clear();
        for (const auto &config : m_project.configurations()) m_configurationCombo->addItem(config.name);
        updateProjectActions(); return true;
    }, this);
    if (dialog.exec() == QDialog::Accepted) {
        const int index = m_configurationCombo->findText(dialog.selectedName());
        m_configurationCombo->setCurrentIndex(qMax(0, index)); updateConfiguration(m_configurationCombo->currentIndex());
    }
}

void MainWindow::createWorkspace()
{
    // Editors will be owned, non-modal top-level windows (Qt::Window), not
    // children clipped to this workspace. Only utility panels are docked.
    auto *workspace = new QFrame(this);
    workspace->setObjectName(QStringLiteral("workspace"));
    workspace->setFrameShape(QFrame::StyledPanel);
    workspace->setMinimumSize(200, 120);
    setCentralWidget(workspace);

    auto *resourceDock = new QDockWidget(tr("Resource Tree"), this);
    resourceDock->setObjectName(QStringLiteral("resourceDock"));
    resourceDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    resourceDock->setAllowedAreas(Qt::LeftDockWidgetArea);
    auto *hiddenTitle = new QWidget(resourceDock);
    hiddenTitle->setFixedHeight(0);
    resourceDock->setTitleBarWidget(hiddenTitle);
    resourceDock->setWidget(m_resourceBrowser);
    addDockWidget(Qt::LeftDockWidgetArea, resourceDock);

    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, m_compilePanel);
    resizeDocks({resourceDock}, {320}, Qt::Horizontal);
    resizeDocks({m_compilePanel}, {200}, Qt::Vertical);
}

void MainWindow::createMenusAndToolbar()
{
    auto *fileMenu = editorMenuBar()->addMenu(tr("&File"));
    auto *editMenu = editorMenuBar()->addMenu(tr("&Edit"));
    auto *windowMenu = editorMenuBar()->addMenu(tr("&Window"));
    auto *resourcesMenu = editorMenuBar()->addMenu(tr("&Resources"));
    auto *scriptsMenu = editorMenuBar()->addMenu(tr("&Scripts"));
    auto *runMenu = editorMenuBar()->addMenu(tr("R&un"));
    auto *helpMenu = editorMenuBar()->addMenu(tr("&Help"));

    QAction *newAction = addUnavailableAction(fileMenu, tr("&New Project"),
        QStringLiteral("new"), QKeySequence(Qt::CTRL | Qt::Key_N));
    newAction->setEnabled(true);
    connect(newAction, &QAction::triggered, this, &MainWindow::newProject);
    QAction *openAction = addUnavailableAction(fileMenu, tr("&Open Project..."),
        QStringLiteral("open"), QKeySequence(Qt::CTRL | Qt::Key_O));
    openAction->setEnabled(true);
    connect(openAction, &QAction::triggered, this, &MainWindow::openProject);
    QAction *importAction = fileMenu->addAction(tr("&Import Project..."));
    importAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    connect(importAction, &QAction::triggered, this, &MainWindow::importProject);
    m_recentProjectsMenu = fileMenu->addMenu(tr("&Recent Projects"));
    m_recentProjectsMenu->setToolTipsVisible(true);
    connect(m_recentProjectsMenu, &QMenu::aboutToShow, this, &MainWindow::refreshRecentProjects);
    refreshRecentProjects();
    m_closeProjectAction = fileMenu->addAction(tr("&Close Project"));
    connect(m_closeProjectAction, &QAction::triggered, this, &MainWindow::closeProject);
    fileMenu->addSeparator();
    QAction *saveAction = addUnavailableAction(fileMenu, tr("&Save"),
        QStringLiteral("save"), QKeySequence(Qt::CTRL | Qt::Key_S));
    m_saveAction = saveAction;
    connect(saveAction, &QAction::triggered, this, [this] { saveProject(); });
    m_saveAsAction = addUnavailableAction(fileMenu, tr("Save &As..."));
    connect(m_saveAsAction, &QAction::triggered, this, [this] { saveProject(); });
    m_saveAllAction = addUnavailableAction(fileMenu, tr("Sa&ve All"));
    connect(m_saveAllAction, &QAction::triggered, this, [this] { saveProject(); });
    m_exportProjectAction = fileMenu->addAction(tr("Export Project..."));
    connect(m_exportProjectAction, &QAction::triggered, this, &MainWindow::exportProject);
    fileMenu->addSeparator();
    m_buildAction = fileMenu->addAction(QIcon(QStringLiteral(":/images/build.png")), tr("&Build"));
    m_buildAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    connect(m_buildAction, &QAction::triggered, this, &MainWindow::compileProject);
    fileMenu->addSeparator();
    QAction *preferencesAction = fileMenu->addAction(tr("&Preferences..."));
    connect(preferencesAction, &QAction::triggered, this, [this] {
        PreferencesDialog dialog(this);
        dialog.exec();
    });
    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F4));
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    m_resourceBrowser->addEditActions(editMenu);
    editMenu->addSeparator();
    QAction *findAction = editMenu->addAction(tr("&Find Resource..."));
    findAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(findAction, &QAction::triggered, m_resourceBrowser, &ResourceBrowser::focusSearch);
    QAction *expandAction = editMenu->addAction(tr("&Expand Resource Tree"));
    connect(expandAction, &QAction::triggered, m_resourceBrowser, &ResourceBrowser::expandAll);
    QAction *collapseAction = editMenu->addAction(tr("C&ollapse Resource Tree"));
    connect(collapseAction, &QAction::triggered, m_resourceBrowser, &ResourceBrowser::collapseAll);
    editMenu->addSeparator();
    addUnavailableAction(editMenu, tr("Show &Object Information"));
    addUnavailableAction(editMenu, tr("Transparent Background Settings"));

    addUnavailableAction(windowMenu, tr("C&ascade"));
    windowMenu->addSeparator();
    QAction *compileAction = m_compilePanel->toggleViewAction();
    compileAction->setText(tr("&Show Compile Form"));
    windowMenu->addAction(compileAction);
    windowMenu->addSeparator();
    addUnavailableAction(windowMenu, tr("&Close All"));

    struct ResourceCommand {
        const char *caption;
        const char *iconName;
        Qt::Key key;
    };
    const ResourceCommand resourceCommands[] = {
        {QT_TR_NOOP("Create &Sprite"), "sprite", Qt::Key_S},
        {QT_TR_NOOP("Create So&und"), "sound", Qt::Key_U},
        {QT_TR_NOOP("Create &Background"), "background", Qt::Key_B},
        {QT_TR_NOOP("Create &Path"), "path", Qt::Key_P},
        {QT_TR_NOOP("Create S&cript"), "script", Qt::Key_C},
        {QT_TR_NOOP("Create Sh&ader"), "shader", Qt::Key_A},
        {QT_TR_NOOP("Create &Font"), "font", Qt::Key_F},
        {QT_TR_NOOP("Create &Time Line"), "timeline", Qt::Key_T},
        {QT_TR_NOOP("Create &Object"), "object", Qt::Key_O},
        {QT_TR_NOOP("Create &Room"), "room", Qt::Key_R}
    };
    QList<QAction *> resourceActions;
    for (const ResourceCommand &command : resourceCommands) {
        resourceActions.append(addUnavailableAction(resourcesMenu, tr(command.caption),
            QString::fromLatin1(command.iconName),
            QKeySequence(Qt::CTRL | Qt::SHIFT | command.key)));
    }
    m_createSpriteAction = resourceActions.first();
    connect(m_createSpriteAction, &QAction::triggered, this, [this] { createResource(ResourceType::Sprite); });
    m_createBackgroundAction = resourceActions.at(2);
    connect(m_createBackgroundAction, &QAction::triggered, this, [this] { createResource(ResourceType::Background); });
    m_createSoundAction = resourceActions.at(1);
    connect(m_createSoundAction, &QAction::triggered, this, [this] { createResource(ResourceType::Sound); });
    m_createPathAction = resourceActions.at(3);
    connect(m_createPathAction, &QAction::triggered, this, [this] { createResource(ResourceType::Path); });
    m_createScriptAction = resourceActions.at(4);
    connect(m_createScriptAction, &QAction::triggered, this, [this] { createResource(ResourceType::Script); });
    m_createShaderAction = resourceActions.at(5);
    connect(m_createShaderAction, &QAction::triggered, this, [this] { createResource(ResourceType::Shader); });
    m_createFontAction = resourceActions.at(6);
    connect(m_createFontAction, &QAction::triggered, this, [this] { createResource(ResourceType::Font); });
    m_createObjectAction = resourceActions.at(8);
    connect(m_createObjectAction, &QAction::triggered, this, [this] { createResource(ResourceType::Object); });
    m_createTimelineAction = resourceActions.at(7);
    connect(m_createTimelineAction, &QAction::triggered, this, [this] { createResource(ResourceType::Timeline); });
    m_createRoomAction = resourceActions.at(9);
    connect(m_createRoomAction, &QAction::triggered, this, [this] { createResource(ResourceType::Room); });
    m_createExtensionAction = resourcesMenu->addAction(QIcon(QStringLiteral(":/images/tree/extension.png")), tr("Create Extension"));
    connect(m_createExtensionAction, &QAction::triggered, this, [this] { createResource(ResourceType::Extension); });
    resourcesMenu->addSeparator();
    m_gameSettingsAction = resourcesMenu->addAction(QIcon(QStringLiteral(":/images/gamesettings.png")), tr("Change &Global Game Settings"));
    m_gameSettingsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
    connect(m_gameSettingsAction, &QAction::triggered, this, [this] {
        const int index = m_configurationCombo->currentIndex();
        if (index >= 0 && index < m_project.configurations().size()) openResource(ResourceType::GameSettings, m_project.configurations().at(index).filePath);
    });
    resourcesMenu->addSeparator();
    m_defineMacrosAction = resourcesMenu->addAction(QIcon(QStringLiteral(":/images/tree/macro.png")), tr("Define Macros..."));
    m_defineMacrosAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    connect(m_defineMacrosAction, &QAction::triggered, this, [this] { openResource(ResourceType::Macro, m_project.filePath()); });

    const QStringList scriptCommands = {
        tr("Show Built-in &Variables"), tr("Show Built-in &Functions"),
        tr("Show &Extension Functions"), tr("Show &Constants"), QString(),
        tr("&Show Resource Names"), tr("S&earch in Scripts..."), QString(),
        tr("Check &Resource Names"), tr("Check &All Scripts"), QString(),
        tr("Clear All Breakpoints"), tr("Clear All Bookmarks")
    };
    for (const QString &command : scriptCommands) {
        if (command.isEmpty())
            scriptsMenu->addSeparator();
        else if (command == tr("S&earch in Scripts...")) {
            m_searchScriptsAction = scriptsMenu->addAction(QIcon(QStringLiteral(":/images/code/find.png")), command);
            m_searchScriptsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
            connect(m_searchScriptsAction, &QAction::triggered, this, &MainWindow::searchScripts);
        }
        else
            addUnavailableAction(scriptsMenu, command);
    }

    m_runAction = runMenu->addAction(QIcon(QStringLiteral(":/images/run.png")), tr("&Run normally"));
    m_runAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_runAction, &QAction::triggered, this, &MainWindow::runProject);
    QAction *debugAction = addUnavailableAction(runMenu, tr("Run in &Debug mode"),
        QStringLiteral("debug"), QKeySequence(Qt::Key_F6));
    m_stopAction = runMenu->addAction(QIcon(QStringLiteral(":/images/stop.png")), tr("&Stop"));
    m_stopAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::stopGame);
    m_cleanAction = runMenu->addAction(QIcon(QStringLiteral(":/images/clean.png")), tr("&Clean build"));
    m_cleanAction->setShortcut(QKeySequence(Qt::Key_F7));
    connect(m_cleanAction, &QAction::triggered, this, &MainWindow::cleanBuild);
    runMenu->addSeparator();
    m_manageConfigurationsAction = runMenu->addAction(tr("Configuration Manager"));
    connect(m_manageConfigurationsAction, &QAction::triggered, this, &MainWindow::manageConfigurations);
    auto *configurationsMenu = runMenu->addMenu(tr("Configurations"));
    connect(configurationsMenu, &QMenu::aboutToShow, this, [this, configurationsMenu] {
        configurationsMenu->clear();
        for (int i = 0; i < m_project.configurations().size(); ++i) {
            auto *action = configurationsMenu->addAction(m_project.configurations().at(i).name); action->setCheckable(true); action->setChecked(i == m_configurationCombo->currentIndex());
            connect(action, &QAction::triggered, this, [this, i] { m_configurationCombo->setCurrentIndex(i); });
        }
    });

    QAction *manualAction = addUnavailableAction(helpMenu, tr("Open the &Manual"),
        QStringLiteral("help"), QKeySequence(Qt::Key_F1));
    helpMenu->addSeparator();
    addUnavailableAction(helpMenu, tr("Key Bindings"));
    addUnavailableAction(helpMenu, tr("&Knowledge Base"));
    addUnavailableAction(helpMenu, tr("&Release Notes"));
    helpMenu->addSeparator();
    QAction *aboutAction = helpMenu->addAction(tr("&About QtGMS..."));
    connect(aboutAction, &QAction::triggered, this, [this] {
        EditorMessageBox::about(this, tr("About QtGMS"),
            QStringLiteral("<p>%1</p><p><a href=\"https://github.com/cubeww/QtGMS\">https://github.com/cubeww/QtGMS</a></p>")
                .arg(tr("QtGMS 0.2.3\nA GameMaker Studio-style editor.").toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"))));
    });

    auto *toolbar = new QToolBar(tr("Main Toolbar"), this);
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setFixedHeight(30);
    addToolBar(Qt::TopToolBarArea, toolbar);
    toolbar->addActions({newAction, openAction, saveAction});
    toolbar->addSeparator();
    toolbar->addAction(m_buildAction);
    toolbar->addSeparator();
    toolbar->addActions({m_runAction, debugAction, m_stopAction, m_cleanAction});
    toolbar->addSeparator();
    toolbar->addActions(resourceActions);
    toolbar->addSeparator();
    toolbar->addAction(m_gameSettingsAction);
    toolbar->addSeparator();
    toolbar->addAction(manualAction);
    toolbar->addSeparator();

    auto *targetLabel = new QLabel(tr(" Target: "), toolbar);
    auto *targetCombo = new QComboBox(toolbar);
    targetCombo->setAccessibleName(tr("Build target"));
    targetCombo->addItem(QStringLiteral("Windows"));
    targetCombo->setFixedWidth(157);
    targetLabel->setBuddy(targetCombo);
    toolbar->addWidget(targetLabel);
    toolbar->addWidget(targetCombo);
    toolbar->addSeparator();

    auto *configurationLabel = new QLabel(tr(" Configuration: "), toolbar);
    m_configurationCombo->setAccessibleName(tr("Build configuration"));
    m_configurationCombo->setFixedWidth(145);
    configurationLabel->setBuddy(m_configurationCombo);
    toolbar->addWidget(configurationLabel);
    toolbar->addWidget(m_configurationCombo);
    auto *manageButton = new QPushButton(tr("Manage"), toolbar); manageButton->setFixedSize(52, 21); toolbar->addWidget(manageButton);
    connect(manageButton, &QPushButton::clicked, m_manageConfigurationsAction, &QAction::trigger);
    connect(m_manageConfigurationsAction, &QAction::changed, manageButton, [this, manageButton] { manageButton->setEnabled(m_manageConfigurationsAction->isEnabled()); });
    connect(m_configurationCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        this, &MainWindow::updateConfiguration);
    toolbar->addSeparator();

    auto *helpLabel = new QLabel(tr(" Knowledge Base: "), toolbar);
    auto *helpSearch = new QLineEdit(toolbar);
    helpSearch->setAccessibleName(tr("Search knowledge base"));
    helpSearch->setEnabled(false);
    helpSearch->setMinimumWidth(80);
    helpSearch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    helpLabel->setBuddy(helpSearch);
    toolbar->addWidget(helpLabel);
    toolbar->addWidget(helpSearch);
}
