#include "mainwindow.h"
#include "scriptsearchwindow.h"
#include "actionxml.h"
#include "codeeditorpanel.h"
#include "editorstandarddialogs.h"
#include "macrodocument.h"
#include "macroeditorwindow.h"
#include "objectdocument.h"
#include "objecteventdialog.h"
#include "objectpropertieswindow.h"
#include "roomdocument.h"
#include "roompropertieswindow.h"
#include "shaderdocument.h"
#include "shadereditorwindow.h"
#include "scripteditorwindow.h"
#include "textfiledocument.h"
#include "timelinedocument.h"
#include "timelinepropertieswindow.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QTextDocument>
#include <QTimer>

static void appendSearchSource(QVector<ScriptSearchSource> &sources, ScriptSearchSource source,
                               const QString &text, const QString &field)
{
    source.text = text;
    source.text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    source.text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    source.identity += QLatin1Char('/') + field;
    if (!source.text.isEmpty()) sources.append(source);
}

static void appendActionSearchSources(QVector<ScriptSearchSource> &sources, const ScriptSearchSource &owner,
                                      const QDomElement &container)
{
    const auto actions = ActionXml::elements(container, QStringLiteral("action"));
    for (int row = 0; row < actions.size(); ++row) {
        auto source = owner;
        const auto &action = actions.at(row);
        source.actionIndex = row;
        source.identity += QStringLiteral("/action%1").arg(row);
        source.description += QObject::tr(", action %1").arg(row + 1);
        if (ActionXml::text(action, QStringLiteral("useapplyto")).toInt()) {
            source.code = false;
            appendSearchSource(sources, source, ActionXml::text(action, QStringLiteral("whoName")), QStringLiteral("who"));
        }
        const auto arguments = ActionXml::elements(action.firstChildElement(QStringLiteral("arguments")), QStringLiteral("argument"));
        const bool codeAction = ActionXml::text(action, QStringLiteral("kind")).toInt() == 7;
        for (int argument = 0; argument < arguments.size(); ++argument) {
            source.argumentIndex = argument;
            const auto &value = arguments.at(argument);
            const int kind = ActionXml::text(value, QStringLiteral("kind")).toInt();
            source.code = codeAction || kind == 0;
            appendSearchSource(sources, source, ActionXml::text(value, ActionXml::argumentTag(kind)),
                               QStringLiteral("argument%1").arg(argument));
        }
    }
}

// Read each open document's live snapshot. Closed resources use the same loaders
// as their editors, so encodings and shader separators stay consistent.
static QVector<ScriptSearchSource> resourceSearchSources(Project &project, ResourceType type,
    const QString &path, const QString &name, ResourceEditorWindow *editor, QString &error)
{
    QVector<ScriptSearchSource> sources;
    ScriptSearchSource source;
    source.type = type;
    source.path = path;
    source.name = name;
    if (type == ResourceType::Script) {
        TextFileDocument file;
        auto *document = editor ? editor->findChild<TextFileDocument *>() : nullptr;
        if (!document) { if (!file.load(path, error)) return sources; document = &file; }
        source.scope = ScriptSearchScope::Script;
        source.description = QObject::tr("script \"%1\"").arg(name);
        appendSearchSource(sources, source, document->textDocument()->toPlainText(), QStringLiteral("script"));
    } else if (type == ResourceType::Shader) {
        ShaderDocument file;
        auto *document = editor ? editor->findChild<ShaderDocument *>() : nullptr;
        if (!document) { if (!file.load(path, error)) return sources; document = &file; }
        source.scope = ScriptSearchScope::Shader;
        for (int stage = 0; stage < 2; ++stage) {
            source.stage = stage;
            source.description = QObject::tr("shader \"%1\", %2").arg(name, stage == 0 ? QObject::tr("vertex") : QObject::tr("fragment"));
            appendSearchSource(sources, source, document->stageDocument(stage)->toPlainText(), QString::number(stage));
        }
    } else if (type == ResourceType::Object) {
        ObjectDocument file;
        auto *document = editor ? editor->findChild<ObjectDocument *>() : nullptr;
        if (!document) { if (!file.load(path, error)) return sources; document = &file; }
        auto *objectEditor = qobject_cast<ObjectPropertiesWindow *>(editor);
        const auto xml = objectEditor ? objectEditor->editingXml() : document->xml();
        const auto events = ActionXml::elements(xml.documentElement().firstChildElement(QStringLiteral("events")), QStringLiteral("event"));
        source.scope = ScriptSearchScope::Object;
        for (int row = 0; row < events.size(); ++row) {
            const auto &event = events.at(row);
            source.eventIndex = row;
            source.identity = event.attribute(QStringLiteral("eventtype")) + QLatin1Char(':')
                + event.attribute(QStringLiteral("enumb")) + QLatin1Char(':') + event.attribute(QStringLiteral("ename"));
            source.description = QObject::tr("object \"%1\", event \"%2\"").arg(name, ObjectEventDialog::eventName(event));
            appendActionSearchSources(sources, source, event);
        }
    } else if (type == ResourceType::Timeline) {
        TimelineDocument file;
        auto *document = editor ? editor->findChild<TimelineDocument *>() : nullptr;
        if (!document) { if (!file.load(path, error)) return sources; document = &file; }
        auto xml = document->xml();
        source.scope = ScriptSearchScope::Timeline;
        for (const auto &moment : TimelineDocument::moments(xml)) {
            source.step = ActionXml::text(moment, QStringLiteral("step")).toInt();
            source.identity = QString::number(source.step);
            source.description = QObject::tr("time line \"%1\", moment %2").arg(name).arg(source.step);
            appendActionSearchSources(sources, source, moment.firstChildElement(QStringLiteral("event")));
        }
    } else if (type == ResourceType::Room) {
        RoomDocument file;
        auto *document = editor ? editor->findChild<RoomDocument *>() : nullptr;
        if (!document) { if (!file.load(path, error)) return sources; document = &file; }
        source.scope = ScriptSearchScope::Room;
        source.description = QObject::tr("room \"%1\", creation code").arg(name);
        appendSearchSource(sources, source, ActionXml::text(document->settings().documentElement(), QStringLiteral("code")), QStringLiteral("room"));
        source.scope = ScriptSearchScope::Instance;
        for (const auto &entity : document->entities()) {
            if (entity.tile) continue;
            source.instanceId = entity.id;
            source.description = QObject::tr("room \"%1\", instance \"%2\" (%3), creation code")
                .arg(name, entity.id.mid(2), entity.xml.attribute(QStringLiteral("objName")));
            appendSearchSource(sources, source, entity.xml.attribute(QStringLiteral("code")), entity.id);
        }
    } else if (type == ResourceType::Macro) {
        MacroDocument file(&project);
        auto *document = editor ? editor->findChild<MacroDocument *>() : nullptr;
        if (!document) { if (!file.load(path, error)) return sources; document = &file; }
        source.scope = ScriptSearchScope::Macro;
        for (const auto &macro : ActionXml::elements(document->xml().documentElement(), QStringLiteral("constant"))) {
            source.macroName = macro.attribute(QStringLiteral("name"));
            source.identity = source.macroName;
            source.description = QObject::tr("macro \"%1\", %2").arg(source.macroName, document->scopeName());
            source.code = false;
            source.macroColumn = 0;
            appendSearchSource(sources, source, source.macroName, QStringLiteral("name"));
            source.code = true;
            source.macroColumn = 1;
            appendSearchSource(sources, source, macro.text(), QStringLiteral("value"));
        }
    }
    return sources;
}

static ResourceEditorWindow *searchResourceEditor(const QHash<QString, QPointer<ResourceEditorWindow>> &editors,
                                                 ResourceType type, const QString &path)
{
    for (const auto &editor : editors)
        if (editor && editor->property("resourceType").toInt() == int(type)
            && editor->filePath().compare(path, Qt::CaseInsensitive) == 0) return editor;
    return nullptr;
}

void MainWindow::searchScripts()
{
    if (!m_project.isOpen()) return;
    ScriptSearchDialog dialog(m_scriptSearchOptions, this);
    if (dialog.exec() != QDialog::Accepted) return;
    m_scriptSearchOptions = dialog.options();
    const auto options = m_scriptSearchOptions;
    const QString projectPath = m_project.filePath();
    ScriptSearch search(m_project);
    QVector<ScriptSearchSource> sources;
    QStringList warnings;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    // Collect macros even if their scope is excluded: they still classify code tokens.
    QStringList scopes = {m_project.filePath()};
    for (const auto &configuration : m_project.configurations()) scopes.append(configuration.filePath);
    for (const auto &path : scopes) {
        QString error;
        const auto macros = resourceSearchSources(m_project, ResourceType::Macro, path, QString(),
            searchResourceEditor(m_resourceEditors, ResourceType::Macro, path), error);
        for (const auto &source : macros) search.addConstant(source.macroName);
        if (options.scopes & (1u << int(ScriptSearchScope::Macro))) {
            sources += macros;
            if (!error.isEmpty()) warnings.append(path + QStringLiteral(": ") + error);
        }
    }
    const ResourceType types[] = {ResourceType::Shader, ResourceType::Script, ResourceType::Object,
                                  ResourceType::Room, ResourceType::Timeline};
    const ScriptSearchScope sourceScopes[] = {ScriptSearchScope::Shader, ScriptSearchScope::Script,
        ScriptSearchScope::Object, ScriptSearchScope::Room, ScriptSearchScope::Timeline};
    for (int i = 0; i < 5; ++i) {
        quint32 mask = 1u << int(sourceScopes[i]);
        if (types[i] == ResourceType::Room) mask |= 1u << int(ScriptSearchScope::Instance);
        if (!(options.scopes & mask)) continue;
        for (const auto &resource : ActionXml::resourceList(m_project, types[i])) {
            QString error;
            sources += resourceSearchSources(m_project, types[i], resource.filePath, resource.name,
                searchResourceEditor(m_resourceEditors, types[i], resource.filePath), error);
            if (!error.isEmpty()) warnings.append(resource.name + QStringLiteral(": ") + error);
        }
    }
    QApplication::restoreOverrideCursor();
    QVector<ScriptSearchMatch> matches;
    // Yield between batches through a local modal event loop. Editor interaction
    // remains blocked while snapshots are searched; Cancel discards partial results.
    QProgressDialog progress(tr("Searching scripts..."), tr("Cancel"), 0, qMax(1, sources.size()), this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(250);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    int index = 0;
    QTimer timer;
    connect(&timer, &QTimer::timeout, &progress, [&] {
        QElapsedTimer elapsed;
        elapsed.start();
        while (index < sources.size()) {
            search.search(sources.at(index++), options, matches);
            if (elapsed.elapsed() >= 12) break;
        }
        progress.setValue(index);
        if (index == sources.size()) progress.accept();
    });
    timer.start(0);
    const bool completed = progress.exec() == QDialog::Accepted;
    timer.stop();
    if (!completed) return;
    auto *window = new ScriptSearchWindow(options.text, matches, warnings, this);
    connect(window, &ScriptSearchWindow::matchActivated, this, [this, window, projectPath](const ScriptSearchMatch &match) {
        if (projectPath != m_project.filePath()) {
            EditorMessageBox::information(window, tr("Search Results"), tr("The active project has changed. Search again in the current project."));
            return;
        }
        openSearchMatch(match);
    });
    window->show();
}

void MainWindow::openSearchMatch(const ScriptSearchMatch &match)
{
    const auto &source = match.source;
    QString error;
    const auto current = resourceSearchSources(m_project, source.type, source.path, source.name,
        searchResourceEditor(m_resourceEditors, source.type, source.path), error);
    ScriptSearchSource target;
    bool found = false;
    for (const auto &candidate : current) {
        if (candidate.identity == source.identity && candidate.text == source.text) {
            target = candidate;
            found = true;
            break;
        }
    }
    if (!found) {
        EditorMessageBox::information(this, tr("Search Results"), error.isEmpty()
            ? tr("This source has changed or was removed. Search again to update the results.") : error);
        return;
    }
    openResource(source.type, source.path);
    auto *editor = searchResourceEditor(m_resourceEditors, source.type, source.path);
    if (!editor) return;
    if (source.type == ResourceType::Script) {
        if (auto *script = qobject_cast<ScriptEditorWindow *>(editor)) script->selectCode(match.offset, match.length);
    } else if (auto *shader = qobject_cast<ShaderEditorWindow *>(editor)) {
        shader->selectCode(target.stage, match.offset, match.length);
    } else if (auto *object = qobject_cast<ObjectPropertiesWindow *>(editor)) {
        object->showAction(target.eventIndex, target.actionIndex, match.offset, match.length, target.argumentIndex);
    } else if (auto *timeline = qobject_cast<TimelinePropertiesWindow *>(editor)) {
        timeline->showAction(target.step, target.actionIndex, match.offset, match.length, target.argumentIndex);
    } else if (auto *room = qobject_cast<RoomPropertiesWindow *>(editor)) {
        room->showCode(target.instanceId, match.offset, match.length);
    } else if (auto *macro = qobject_cast<MacroEditorWindow *>(editor)) {
        macro->selectMacro(target.macroName, target.macroColumn);
    }
}
