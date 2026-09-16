#include "compilerbuild.h"
#include "builddirectory.h"
#include "actionxml.h"
#include "scriptsource.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

CompilerBuild::CompilerBuild(const CompileRequest &input)
    : request(input)
{
}
QByteArray CompilerBuild::read(const QString &path)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly))
        throw CompileError(QStringLiteral("%1: %2").arg(path, input.errorString()));
    QByteArray bytes = input.readAll();
    if (input.error() != QFile::NoError)
        throw CompileError(input.errorString());
    return bytes;
}
QDomDocument CompilerBuild::xml(const QString &path, QByteArray *sourceBytes)
{
    const QByteArray bytes = read(path);
    QDomDocument document;
    QString error;
    int line, column;
    if (!document.setContent(bytes, false, &error, &line, &column))
        throw CompileError(QStringLiteral("%1:%2:%3: %4").arg(path).arg(line).arg(column).arg(error));
    if (sourceBytes)
        *sourceBytes = bytes;
    return document;
}
QString CompilerBuild::text(const QDomElement &element, const char *key, const QString &fallback)
{
    auto child = element.firstChildElement(QLatin1String(key));
    return child.isNull() ? fallback : child.text();
}
double CompilerBuild::number(const QDomElement &element, const char *key, double fallback)
{
    QString value = text(element, key);
    if (value.isEmpty())
        return fallback;
    if (value.compare("true", Qt::CaseInsensitive) == 0)
        return 1;
    if (value.compare("false", Qt::CaseInsensitive) == 0)
        return 0;
    bool valid;
    double result = value.toDouble(&valid);
    if (!valid)
        throw CompileError(QStringLiteral("Invalid value for %1: %2").arg(QLatin1String(key), value));
    return result;
}
double CompilerBuild::attribute(const QDomElement &element, const char *key, double fallback)
{
    const QString value = element.attribute(QLatin1String(key));
    if (value.isEmpty())
        return fallback;
    bool valid;
    double result = value.toDouble(&valid);
    if (!valid)
        throw CompileError(QStringLiteral("Invalid attribute %1: %2").arg(QLatin1String(key), value));
    return result;
}
QVector<QDomElement> CompilerBuild::children(const QDomElement &element, const char *name)
{
    QVector<QDomElement> result;
    for (auto child = element.firstChildElement(QLatin1String(name)); !child.isNull();
        child = child.nextSiblingElement(QLatin1String(name)))
        result.append(child);
    return result;
}
QString CompilerBuild::assetPath(const CompilerResource &resource, const QString &relative)
{
    QString path = relative;
    path.replace('\\', '/');
    return QFileInfo(resource.node.filePath).absoluteDir().absoluteFilePath(path);
}
int CompilerBuild::option(const char *key, int fallback) const
{
    QString value = options.value(QStringLiteral("option_") + QLatin1String(key));
    if (value.isEmpty())
        return fallback;
    if (value.compare("true", Qt::CaseInsensitive) == 0)
        return 1;
    if (value.compare("false", Qt::CaseInsensitive) == 0)
        return 0;
    return int(value.toLongLong());
}
int CompilerBuild::resourceId(ResourceType type, const QString &name, int absent) const
{
    if (name.isEmpty() || name == "<undefined>" || name == "<none>")
        return absent;
    // GMX object references use these scope sentinels, including parentName.
    if (type == ResourceType::Object) {
        if (name == QStringLiteral("self"))
            return -1;
        if (name == QStringLiteral("other"))
            return -2;
    }
    const auto &entries = resources[type];
    for (int i = 0; i < entries.size(); ++i)
        if (entries.at(i).node.name == name)
            return i;
    throw CompileError(QStringLiteral("Unresolved resource: %1").arg(name));
}
int CompilerBuild::code(const QString &name, const QString &source)
{
    if (source.trimmed().isEmpty())
        return -1;
    VmCode entry;
    entry.name = name;
    entry.source = source;
    codes.append(entry);
    return codes.size() - 1;
}
void CompilerBuild::external(const QString &name, const QByteArray &bytes)
{
    QString normalized = name;
    normalized.replace('\\', '/');
    normalized = QDir::cleanPath(normalized);
    if (QDir::isAbsolutePath(normalized) || normalized == ".." || normalized.startsWith("../")
        || normalized.contains(':') || normalized.compare("data.win", Qt::CaseInsensitive) == 0)
        throw CompileError(QStringLiteral("Invalid output file path: %1").arg(name));
    for (auto it = externalFiles.cbegin(); it != externalFiles.cend(); ++it)
        if (it.key().compare(normalized, Qt::CaseInsensitive) == 0) {
            if (it.value() != bytes)
                throw CompileError(QStringLiteral("Conflicting output file: %1").arg(normalized));
            return;
        }
    externalFiles.insert(normalized, bytes);
}
int CompilerBuild::addAudio(int group, const QByteArray &bytes)
{
    // Keep converted audio on disk until AUDO is assembled. Retaining every
    // decoded song here duplicates the final data.win buffer in a 32-bit process.
    if (!audioData.isOpen() && !audioData.open())
        throw CompileError(QStringLiteral("Cannot create temporary audio storage: %1").arg(audioData.errorString()));
    CompilerAudioEntry entry;
    entry.offset = audioData.pos();
    entry.size = bytes.size();
    if (audioData.write(bytes) != bytes.size())
        throw CompileError(QStringLiteral("Cannot store compiled audio: %1").arg(audioData.errorString()));
    auto &entries = audio[group];
    const int index = entries.size();
    entries.append(entry);
    return index;
}
void CompilerBuild::load()
{
    if (!request.project.isOpen() || request.configuration < 0
        || request.configuration >= request.project.configurations().size())
        throw CompileError(QStringLiteral("Open a project and select a configuration first."));
    const auto &config = request.project.configurations().at(request.configuration);
    options = config.options;
    textures.configure(options, QDir(BuildDirectory::path(request.project)).filePath(QStringLiteral(".cache")));
    environment.shortCircuit = option("shortcircuit", 1) != 0;
    const auto constants = QJsonDocument::fromJson(read(QStringLiteral(":/gmlconstants.json"))).object();
    for (auto it = constants.begin(); it != constants.end(); ++it)
        environment.constants.insert(it.key(), it.value().toDouble());
    const auto functions = QJsonDocument::fromJson(read(QStringLiteral(":/gmlfunctions.json"))).object();
    for (auto it = functions.begin(); it != functions.end(); ++it) {
        const auto info = it.value().toObject();
        environment.functions.insert(it.key());
        environment.functionArguments.insert(it.key(), info.value("arguments").toInt());
        environment.functionClassifications.insert(it.key(), info.value("classification").toString().toULongLong());
    }
    const auto variables = QJsonDocument::fromJson(read(QStringLiteral(":/gmlvariables.json"))).object();
    for (auto it = variables.begin(); it != variables.end(); ++it) {
        const auto info = it.value().toObject();
        environment.builtInVariables.insert(it.key());
        if (info.value("global").toBool())
            environment.builtInGlobalVariables.insert(it.key());
        if (!info.value("writable").toBool())
            environment.readOnlyVariables.insert(it.key());
    }
    for (const auto &macro : request.project.resources(ResourceType::Macro))
        environment.macros.insert(macro.name, macro.value);
    for (const auto &macro : config.macros)
        environment.macros.insert(macro.name, macro.value);
    QSet<QString> scriptNames;
    for (ResourceType type : { ResourceType::Sprite, ResourceType::Sound, ResourceType::Background, ResourceType::Path,
             ResourceType::Script, ResourceType::Shader, ResourceType::Font, ResourceType::Timeline,
             ResourceType::Object, ResourceType::Room, ResourceType::Extension, ResourceType::IncludedFile }) {
        auto nodes = ActionXml::resourceList(request.project, type);
        for (const auto &node : nodes) {
            CompilerResource entry;
            entry.node = node;
            if (type != ResourceType::IncludedFile && type != ResourceType::Script && type != ResourceType::Shader) {
                entry.document = xml(node.filePath, &entry.sourceBytes);
                entry.xml = entry.document.documentElement();
            }
            if (type != ResourceType::Script)
                environment.constants.insert(node.name, resources[type].size());
            if (type == ResourceType::Script) {
                const auto sections = ScriptSource::sections(QString::fromUtf8(read(node.filePath)), node.name);
                // Only the #define names are callable for a multi-script file.
                for (const auto &section : sections) {
                    if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$")).match(section.name).hasMatch()
                        || scriptNames.contains(section.name))
                        throw CompileError(node.name + ": invalid or duplicate script name: " + section.name);
                    CompilerResource script;
                    script.node = node;
                    script.node.name = section.name;
                    script.embeddedSource = section.code.trimmed().isEmpty() ? QStringLiteral("exit;") : section.code;
                    environment.constants.insert(section.name, resources[type].size());
                    environment.functions.insert(section.name);
                    scriptNames.insert(section.name);
                    resources[type].append(script);
                }
                continue;
            }
            resources[type].append(entry);
        }
    }
    if (resources[ResourceType::Room].isEmpty())
        throw CompileError(QStringLiteral("The project has no rooms."));
    for (const auto &room : resources[ResourceType::Room]) {
        for (const auto &instance : children(room.xml.firstChildElement("instances"), "instance")) {
            const QString name = instance.attribute("name");
            if (!name.isEmpty())
                environment.constants.insert(name, instanceId);
            ++instanceId;
        }
        tileId += children(room.xml.firstChildElement("tiles"), "tile").size();
    }
    for (int i = 0; i < request.project.audioGroups().size(); ++i)
        environment.constants.insert(request.project.audioGroups().at(i), i);
}
void CompilerBuild::general()
{
    file.bytes.append("FORM", 4);
    file.u32(0);
    file.chunk("GEN8", [&] {
        QString name = QFileInfo(request.project.filePath()).completeBaseName();
        if (name.endsWith(".project"))
            name.chop(8);
        file.u32(0x1001);
        file.string(name);
        file.string(request.project.configurations().at(request.configuration).name);
        file.u32(instanceId);
        file.u32(tileId);
        file.u32(option("gameid", 1));
        for (int i = 0; i < 4; ++i)
            file.u32(0);
        file.string(name);
        for (int version : { 1, 0, 0, 9999 })
            file.u32(version);
        const auto &room = resources[ResourceType::Room].first().xml;
        int left = 0, top = 0;
        int right = number(room, "width", 640), bottom = number(room, "height", 480);
        // GEN8 initializes the window/application surface before room code.
        // Match WADSaver: visible port bounds, or room size when none apply.
        if (number(room, "enableViews") != 0) {
            bool firstView = true;
            for (const auto &view : children(room.firstChildElement("views"), "view")) {
                if (attribute(view, "visible") == 0)
                    continue;
                const int x = attribute(view, "xport"), y = attribute(view, "yport");
                const int portRight = x + int(attribute(view, "wport"));
                const int portBottom = y + int(attribute(view, "hport"));
                left = firstView ? x : qMin(left, x);
                top = firstView ? y : qMin(top, y);
                right = firstView ? portRight : qMax(right, portRight);
                bottom = firstView ? portBottom : qMax(bottom, portBottom);
                firstView = false;
            }
        }
        file.u32(right - left);
        file.u32(bottom - top);
        quint32 flags = 0x800;
        const char *keys[] = { "fullscreen", "interpolate", "showcursor", "sizeable", "screenkey", "borderless" };
        const int masks[] = { 1, 8, 32, 64, 128, 0x4000 };
        for (int i = 0; i < 6; ++i)
            if (option(keys[i], i == 2 || i == 4))
                flags |= masks[i];
        if (option("scale", -1))
            flags |= 16;
        if (option("sync_vertex") & 1)
            flags |= 2;
        if (option("windows_save_location"))
            flags |= 0x2000;
        file.u32(flags);
        for (int i = 0; i < 5; ++i)
            file.u32(0);
        file.u64(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() / 1000);
        file.string(options.value("option_display_name", name));
        file.u64(2);
        classificationOffset = file.position();
        file.u64(0);
        file.u32(0);
        file.u32(6502);
        file.u32(resources[ResourceType::Room].size());
        for (int i = 0; i < resources[ResourceType::Room].size(); ++i)
            file.u32(i);
    });
    file.chunk("OPTN", [&] {
        file.u32(0x80000000u);
        file.u32(2);
        quint64 flags = 0;
        const char *keys[] = { "fullscreen", "interpolate", "use_new_audio", "noborder", "showcursor", "sizeable",
            "stayontop", "changeresolution", "nobuttons", "screenkey", "helpkey", "quitkey", "savekey", "screenshotkey",
            "closesec", "freeze", "showprogress", "loadtransparent", "scaleprogress", "displayerrors", "writeerrors",
            "aborterrors", "variableerrors", "creationeventorder", "use_front_touch", "use_rear_touch",
            "use_fast_collision", "fast_collision_compatibility" };
        for (int i = 0; i < 28; ++i)
            if (option(keys[i], i == 2 || i == 4 || i == 9 || i == 19))
                flags |= quint64(1) << i;
        file.u64(flags);
        file.u32(option("scale", -1));
        QString color = options.value("option_windowcolor", "0");
        file.u32(color.startsWith('$') ? color.mid(1).toUInt(nullptr, 16) : color.toUInt());
        for (const char *key : { "colordepth", "resolution", "frequency", "sync_vertex", "priority" })
            file.u32(option(key));
        for (int i = 0; i < 3; ++i)
            file.u32(0);
        file.u32(option("loadalpha", 255));
        file.u32(2);
        file.string("@@SleepMargin");
        file.string(QString::number(option("windows_sleep_margin", 1)));
        file.string("@@DrawColour");
        file.string("4294967295");
    });
    file.chunk("LANG", [&] {
        file.u32(1);
        file.u32(0);
        file.u32(0);
    });
}
