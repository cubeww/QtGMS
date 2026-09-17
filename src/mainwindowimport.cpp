#include "mainwindow.h"
#include "projectimportjob.h"

#include "backgrounddocument.h"
#include "editordialog.h"
#include "editorstandarddialogs.h"
#include "objectpropertieswindow.h"
#include "pathpropertieswindow.h"
#include "resourcebrowser.h"
#include "resourcereferences.h"
#include "roompropertieswindow.h"
#include "sounddocument.h"
#include "spritedocument.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMap>
#include <QMimeData>
#include <QPushButton>
#include <QRegExp>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

static bool isDroppedProject(const QString &path)
{
    return path.endsWith(QStringLiteral(".project.gmx"), Qt::CaseInsensitive) || ProjectImportJob::supportsFile(path);
}

static ResourceType droppedResourceType(const QString &path)
{
    if (path.endsWith(QStringLiteral(".gmez"), Qt::CaseInsensitive)) return ResourceType::Extension;
    for (ResourceType type : {ResourceType::Sprite, ResourceType::Sound, ResourceType::Background,
             ResourceType::Path, ResourceType::Font, ResourceType::Timeline, ResourceType::Object,
             ResourceType::Room, ResourceType::Extension}) {
        if (path.endsWith(ResourceReferences::suffix(type), Qt::CaseInsensitive)) return type;
    }
    return ResourceType::IncludedFile;
}

static bool chooseDroppedMediaType(QWidget *parent, bool image, int count, ResourceType &type)
{
    EditorDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("What resource to create?"));
    dialog.setFixedSize(356, 140);
    auto *layout = new QVBoxLayout(dialog.bodyWidget());
    layout->setContentsMargins(18, 10, 18, 10);
    auto *label = new QLabel(QObject::tr("Please indicate the type of resource you want to create."), dialog.bodyWidget());
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label, 1);
    if (count > 1) {
        auto *countLabel = new QLabel(QObject::tr("Apply to %1 files").arg(count), dialog.bodyWidget());
        countLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(countLabel);
    }
    auto *buttons = new QHBoxLayout;
    const auto addButton = [&](const QString &text, ResourceType choice) {
        if (buttons->count()) buttons->addStretch();
        auto *button = new QPushButton(text, dialog.bodyWidget());
        button->setFixedSize(74, 23);
        buttons->addWidget(button);
        QObject::connect(button, &QPushButton::clicked, &dialog, [&dialog, &type, choice]() {
            type = choice;
            dialog.accept();
        });
        return button;
    };
    QPushButton *first = image ? addButton(QObject::tr("Sprite"), ResourceType::Sprite)
                              : addButton(QObject::tr("Sound"), ResourceType::Sound);
    if (image) addButton(QObject::tr("Background"), ResourceType::Background);
    addButton(QObject::tr("Included File"), ResourceType::IncludedFile);
    layout->addLayout(buttons);
    first->setDefault(true);
    first->setFocus();
    return dialog.exec() == QDialog::Accepted;
}

// Build a complete resource before the project transaction copies it and its assets.
static bool stageDroppedMedia(ResourceType type, const QString &source, const QString &destination, QString &error)
{
    if (type == ResourceType::Sprite) {
        QList<SpriteFrame> frames;
        if (!SpriteDocument::importImages(QStringList(source), frames, error)) return false;
        if (!SpriteDocument::createEmpty(destination, error)) return false;
        SpriteDocument document;
        if (!document.load(destination, 0, error)) return false;
        SpriteState state = document.state();
        state.frames = frames;
        state.size = frames.first().image.size();
        document.edit(state, QObject::tr("Load sprite"));
        return document.save(error);
    }
    if (type == ResourceType::Background) {
        QImageReader reader(source);
        const QImage image = reader.read();
        if (image.isNull()) { error = reader.errorString(); return false; }
        if (!BackgroundDocument::createEmpty(destination, error)) return false;
        BackgroundDocument document;
        if (!document.load(destination, 0, error)) return false;
        document.replaceImage(image);
        return document.save(error);
    }
    if (!SoundDocument::createEmpty(destination, error)) return false;
    SoundDocument document;
    return document.load(destination, 0, error) && document.importAudio(source, error) && document.save(error);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::WindowActivate) {
        auto *room = qobject_cast<RoomPropertiesWindow *>(watched);
        if (room && room->parentWidget() == this) m_lastRoomEditor = room;
    }
    if (event->type() != QEvent::DragEnter && event->type() != QEvent::DragMove && event->type() != QEvent::Drop)
        return EditorWindow::eventFilter(watched, event);
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || !qobject_cast<EditorWindow *>(widget->window())) return EditorWindow::eventFilter(watched, event);
    QWidget *ancestor = widget;
    while (ancestor && ancestor != this) ancestor = ancestor->parentWidget();
    if (!ancestor) return EditorWindow::eventFilter(watched, event);
    auto *drop = static_cast<QDropEvent *>(event);
    if (!drop->mimeData()->hasUrls()) return EditorWindow::eventFilter(watched, event);

    // Consume external URLs before child text/graphics views or the resource tree
    // interpret them. Internal resource/action drags retain their existing handlers.
    QStringList paths;
    for (const QUrl &url : drop->mimeData()->urls()) {
        if (!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isFile()) { drop->ignore(); return true; }
        const QString path = QFileInfo(url.toLocalFile()).absoluteFilePath();
        if (!paths.contains(path, Qt::CaseInsensitive)) paths.append(path);
    }
    if (paths.isEmpty() || m_treeBusy || m_fileDropPending || !(drop->possibleActions() & Qt::CopyAction)) {
        drop->ignore();
        return true;
    }
    drop->setDropAction(Qt::CopyAction);
    drop->accept();
    if (event->type() == QEvent::Drop) {
        const QPointer<QWidget> parent = widget->window();
        m_fileDropPending = true;
        // Leave the native drag loop before opening modal dialogs or editors.
        QTimer::singleShot(0, this, [this, paths, parent]() {
            m_fileDropPending = false;
            if (paths.size() == 1 && isDroppedProject(paths.first())) {
                if (ProjectImportJob::supportsFile(paths.first())) importProjectFile(paths.first());
                else loadProject(paths.first());
            } else importDroppedFiles(paths, parent ? parent.data() : this);
        });
    }
    return true;
}

void MainWindow::importDroppedFiles(const QStringList &paths, QWidget *parent)
{
    if (m_treeBusy) return;
    QScopedValueRollback<bool> busy(m_treeBusy, true);
    if (QApplication::activeModalWidget()) parent = QApplication::activeModalWidget();

    QStringList images, sounds;
    QMap<QString, ResourceType> types;
    const QList<QByteArray> imageFormats = QImageReader::supportedImageFormats();
    for (const QString &path : paths) {
        const ResourceType type = droppedResourceType(path);
        types.insert(path, type);
        if (type != ResourceType::IncludedFile || isDroppedProject(path)) continue;
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix == QStringLiteral("wav") || suffix == QStringLiteral("mp3") || suffix == QStringLiteral("ogg"))
            sounds.append(path);
        else if (imageFormats.contains(suffix.toLatin1()) || !QImageReader::imageFormat(path).isEmpty()) images.append(path);
    }
    // Choose before writing so closing either dialog cancels the whole drop.
    ResourceType imageType = ResourceType::Sprite, soundType = ResourceType::Sound;
    if (!images.isEmpty() && !chooseDroppedMediaType(parent, true, images.size(), imageType)) return;
    if (!sounds.isEmpty() && !chooseDroppedMediaType(parent, false, sounds.size(), soundType)) return;
    for (const QString &path : images) types[path] = imageType;
    for (const QString &path : sounds) types[path] = soundType;
    if (!m_project.isOpen()) newProject();
    if (!m_project.isOpen()) return;

    QMap<ResourceType, QList<int>> groups;
    for (auto type : types) groups.insert(type, m_resourceBrowser->selectedGroupPath(type));
    ResourceNode lastCreated;
    bool imported = false;
    QStringList errors;
    for (const QString &path : paths) {
        QString error;
        if (isDroppedProject(path)) {
            errors.append(tr("%1: Drop a project on its own to open it.").arg(QFileInfo(path).fileName()));
            continue;
        }
        const ResourceType type = types.value(path);
        const bool media = type != ResourceType::IncludedFile && (images.contains(path) || sounds.contains(path));
        QTemporaryDir staging;
        QString source = path;
        if (media) {
            if (!staging.isValid()) {
                errors.append(tr("%1: Cannot create a temporary import directory.").arg(QFileInfo(path).fileName()));
                continue;
            }
            QString name = QFileInfo(path).completeBaseName();
            name.replace(QRegExp(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
            if (name.isEmpty() || name.at(0).isDigit()) name.prepend(QLatin1Char('_'));
            source = QDir(staging.path()).filePath(name.left(96) + ResourceReferences::suffix(type));
            if (!stageDroppedMedia(type, path, source, error)) {
                errors.append(tr("%1: %2").arg(QFileInfo(path).fileName(), error));
                continue;
            }
        }
        ResourceNode created;
        if (m_project.copyResource(type, source, groups.value(type), created, error)) {
            lastCreated = created;
            imported = true;
        } else errors.append(tr("%1: %2").arg(QFileInfo(path).fileName(), error));
    }
    if (imported) {
        m_roomAssets->reload();
        m_settingsEdited = true;
        m_resourceBrowser->setProject(m_project);
        updateConfiguration(m_configurationCombo->currentIndex());
        for (const auto &editor : m_resourceEditors) {
            if (auto *object = qobject_cast<ObjectPropertiesWindow *>(editor.data())) object->updateResources();
            if (auto *room = qobject_cast<RoomPropertiesWindow *>(editor.data())) room->updateResources();
            if (auto *path = qobject_cast<PathPropertiesWindow *>(editor.data())) path->updateResources();
        }
        m_resourceBrowser->selectResource(lastCreated.filePath);
    }
    if (!errors.isEmpty()) EditorMessageBox::warning(parent, tr("Cannot Import Files"), errors.join(QStringLiteral("\n\n")));
    if (imported) openResource(lastCreated.type, lastCreated.filePath);
}
