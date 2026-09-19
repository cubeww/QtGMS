#include "compilerbuild.h"
#include "compilertarget.h"
#include "actionxml.h"
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>

QString CompilerBuild::actionCode(const QDomElement &event)
{
    const auto actions = children(event, "action");
    int index = 0;
    bool relativeBaseline = false, relativeEnabled = false, foundRelative = false;
    bool conditionDeclared = false;
    for (const auto &entry : actions)
        if (number(entry, "userelative") != 0) {
            const bool value = number(entry, "relative") != 0;
            if (!foundRelative) {
                relativeBaseline = value;
                foundRelative = true;
            }
            relativeEnabled = relativeEnabled || value;
        }
    std::function<QString()> action = [&]() -> QString {
        if (index >= actions.size())
            return QStringLiteral("{}\n");
        const auto current = actions.at(index++);
        const int kind = number(current, "kind");
        if (kind == 1) {
            QString block = "{\n";
            while (index < actions.size() && number(actions.at(index), "kind") != 2)
                block += action();
            if (index >= actions.size())
                throw CompileError(QStringLiteral("Unclosed DND action block"));
            ++index;
            return block + "}\n";
        }
        if (kind == 2 || kind == 3)
            throw CompileError(QStringLiteral("Unexpected DND End / Else action"));
        if (kind == 4)
            return (relativeEnabled ? QStringLiteral("action_set_relative(0);\n") : QString())
                + QStringLiteral("exit;\n");
        const auto arguments = children(current.firstChildElement("arguments"), "argument");
        QStringList values;
        for (const auto &argument : arguments) {
            QString value;
            for (auto child = argument.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
                if (child.tagName() != "kind") {
                    value = child.text();
                    break;
                }
            const int type = number(argument, "kind");
            if (kind != 6 && kind != 7 && (type == 1 || type == 2) && !value.startsWith('"') && !value.startsWith('\'')) {
                if (value.contains('"'))
                    throw CompileError(QStringLiteral("DND string contains an unquoted double quote"));
                value = '"' + value + '"';
            }
            // Variable assignments contain a raw lvalue and expression; Execute
            // Code contains source text. Neither uses ordinary string arguments.
            if (kind != 6 && kind != 7) {
                if (value.isEmpty())
                    value = "0";
                if (value == "<undefined>")
                    value = "-1";
            }
            values.append(value);
        }
        if (kind == 5)
            return "repeat (" + values.value(0, "0") + ") " + action();
        QString body;
        if (kind == 6) {
            if (values.size() != 2 || values.at(0).trimmed().isEmpty() || values.at(1).trimmed().isEmpty())
                throw CompileError(QStringLiteral("DND Set Variable requires a variable and a value"));
            body = text(current, "codestring") + values.at(0)
                + (number(current, "relative") != 0 ? QStringLiteral(" += ") : QStringLiteral(" = "))
                + values.at(1);
        } else if (kind == 7)
            body = values.value(0);
        else if (number(current, "exetype") == 1) {
            QString function = text(current, "functionname");
            if (function.isEmpty())
                throw CompileError(QStringLiteral("DND action has no function name"));
            if (function == "action_execute_script") {
                function = values.takeFirst();
            }
            body = function + "(" + values.join(",") + ")";
        } else if (number(current, "exetype") == 2) {
            body = text(current, "codestring");
            for (int i = values.size() - 1; i >= 0; --i)
                body.replace(QRegularExpression("\\bargument" + QString::number(i) + "\\b"), values.at(i));
            if (body.isEmpty())
                throw CompileError(QStringLiteral("DND action has no executable code"));
        } else
            return QStringLiteral("{}\n");
        const bool relative = number(current, "userelative") != 0 && relativeEnabled
            && (number(current, "relative") != 0) != relativeBaseline;
        const QString prefix = relative
            ? QStringLiteral("action_set_relative(%1);\n").arg(number(current, "relative") != 0 ? 1 : 0)
            : QString();
        const QString suffix
            = relative ? QStringLiteral("action_set_relative(%1);\n").arg(relativeBaseline ? 1 : 0) : QString();
        QString owner = text(current, "whoName", "self");
        // Apply-to references use the same undefined-object sentinel as the
        // original compiler; the XML marker itself is not a GML expression.
        if (owner == QStringLiteral("<undefined>"))
            owner = QStringLiteral("-100");
        const bool apply = number(current, "useapplyto") != 0 && owner != "self";
        if (number(current, "isquestion") != 0) {
            // Evaluate the condition in its apply-to scope, then execute the body
            // in the event's original scope, matching the action compiler.
            const QString condition = "__b__";
            QString evaluate = apply ? "with (" + owner + ") {\n" : QString();
            if (!conditionDeclared) {
                evaluate += "var " + condition + ";\n";
                conditionDeclared = true;
            }
            evaluate += prefix + condition + "=(" + body + ");\n";
            if (apply && owner != "other")
                evaluate += "if (" + (number(current, "isnot") != 0 ? QStringLiteral("!") : QString()) + condition
                    + ") break;\n";
            evaluate += suffix;
            if (apply)
                evaluate += "}\n";
            evaluate += "if (" + (number(current, "isnot") != 0 ? QStringLiteral("!") : QString()) + condition + ") "
                + action();
            if (index < actions.size() && number(actions.at(index), "kind") == 3) {
                ++index;
                evaluate += "else " + action();
            }
            return evaluate;
        }
        // GMEvent terminates code actions with an empty block comment. Its
        // closing delimiter also ends an open user comment before generated
        // scope/relative-state cleanup and the next action.
        if (kind == 6 || kind == 7 || number(current, "exetype") == 2)
            body += "\n/* */\n";
        body = prefix + body + "\n;\n" + suffix;
        return apply ? "with (" + owner + ") {\n" + body + "}\n" : "{\n" + body + "}\n";
    };
    QString result
        = relativeEnabled ? QStringLiteral("action_set_relative(%1);\n").arg(relativeBaseline ? 1 : 0) : QString();
    while (index < actions.size())
        result += action();
    if (relativeEnabled)
        result += "action_set_relative(0);\n";
    return result;
}

static void writeCompiledAction(DataWriter &file, int code)
{
    file.list(code < 0 ? 0 : 1, [&](int) {
        for (int value : { 1, 603, 7, 0, 0, 0, 2 })
            file.u32(value);
        file.string(QString());
        file.u32(code);
        file.u32(0);
        file.u32(-1);
        file.u32(0);
        file.u32(0);
        file.u32(0);
    });
}

void CompilerBuild::objects()
{
    file.chunk("TMLN", [&] {
        const auto &timelines = resources[ResourceType::Timeline];
        file.list(timelines.size(), [&](int i) {
            const auto &resource = timelines.at(i);
            file.string(resource.node.name);
            auto moments = children(resource.xml, "entry");
            for (int m = moments.size() - 1; m >= 0; --m) {
                if (moments.at(m).firstChildElement("event").firstChildElement("action").isNull())
                    moments.removeAt(m);
            }
            file.u32(moments.size());
            QVector<int> pointers;
            for (const auto &moment : moments) {
                file.u32(number(moment, "step"));
                pointers.append(file.reserve());
            }
            for (int m = 0; m < moments.size(); ++m) {
                file.patch(pointers.at(m), file.position());
                const auto &moment = moments.at(m);
                const QString source = actionCode(moment.firstChildElement("event"));
                writeCompiledAction(file,
                    code("gml_Timeline_" + resource.node.name + "_" + QString::number(number(moment, "step")), source));
            }
        });
    });
    file.chunk("OBJT", [&] {
        const auto &objects = resources[ResourceType::Object];
        file.list(objects.size(), [&](int i) {
            const auto &resource = objects.at(i);
            const auto &root = resource.xml;
            file.string(resource.node.name);
            file.u32(resourceId(ResourceType::Sprite, text(root, "spriteName")));
            file.u32(number(root, "visible", 1) != 0);
            file.u32(number(root, "solid") != 0);
            file.u32(number(root, "depth"));
            file.u32(number(root, "persistent") != 0);
            file.u32(resourceId(ResourceType::Object, text(root, "parentName"), -100));
            file.u32(resourceId(ResourceType::Sprite, text(root, "maskName")));
            file.u32(number(root, "PhysicsObject") != 0);
            file.u32(number(root, "PhysicsObjectSensor") != 0);
            file.u32(number(root, "PhysicsObjectShape"));
            file.f32(number(root, "PhysicsObjectDensity", 0.5));
            file.f32(number(root, "PhysicsObjectRestitution", 0.1));
            file.u32(number(root, "PhysicsObjectGroup"));
            file.f32(number(root, "PhysicsObjectLinearDamping", 0.1));
            file.f32(number(root, "PhysicsObjectAngularDamping", 0.1));
            const auto points = children(root.firstChildElement("PhysicsShapePoints"), "point");
            file.u32(points.size());
            file.f32(number(root, "PhysicsObjectFriction", 0.2));
            file.u32(number(root, "PhysicsObjectAwake", 1) != 0);
            file.u32(number(root, "PhysicsObjectKinematic") != 0);
            for (const auto &point : points) {
                const auto values = point.text().split(',');
                if (values.size() != 2)
                    throw CompileError(resource.node.name + ": invalid physics vertex");
                file.f32(values.at(0).toFloat());
                file.f32(values.at(1).toFloat());
            }
            QMap<int, QVector<QDomElement>> events;
            for (const auto &event : children(root.firstChildElement("events"), "event")) {
                // An absent handler must not override an inherited event with an empty action list.
                if (event.firstChildElement("action").isNull()) continue;
                const int type = attribute(event, "eventtype");
                if (type < 0 || type > 11)
                    throw CompileError(resource.node.name + ": unsupported event type");
                events[type].append(event);
            }
            file.list(12, [&](int type) {
                const auto &entries = events[type];
                file.list(entries.size(), [&](int e) {
                    const auto &event = entries.at(e);
                    int subtype = type == 4 ? resourceId(ResourceType::Object, event.attribute("ename"))
                                            : attribute(event, "enumb");
                    file.u32(subtype);
                    writeCompiledAction(file,
                        code("gml_Object_" + resource.node.name + "_" + QString::number(type) + "_"
                                + QString::number(subtype),
                            actionCode(event)));
                });
            });
        });
    });
}

void CompilerBuild::rooms(CompileProfile &profile)
{
    int nextInstance = 100000, nextTile = 10000000;
    file.chunk("ROOM", [&] {
        CompileDetailScope timing(profile, QStringLiteral("Room metadata / instances / tiles serialization"));
        const auto &rooms = resources[ResourceType::Room];
        profile.count(QStringLiteral("Rooms"), rooms.size());
        file.list(rooms.size(), [&](int i) {
            const auto &resource = rooms.at(i);
            const auto &root = resource.xml;
            file.string(resource.node.name);
            file.string(text(root, "caption"));
            for (const char *key : { "width", "height", "speed" })
                file.u32(number(root, key));
            file.u32(number(root, "persistent") != 0);
            file.u32(number(root, "colour"));
            file.u32(number(root, "showcolour") != 0);
            file.u32(code("gml_Room_" + resource.node.name + "_Create", text(root, "code")));
            file.u32((number(root, "enableViews") != 0 ? 1 : 0) | (number(root, "clearViewBackground", 1) != 0 ? 2 : 0)
                | (number(root, "clearDisplayBuffer", 1) == 0 ? 4 : 0));
            int backgrounds = file.reserve(), views = file.reserve(), instances = file.reserve(),
                tiles = file.reserve();
            file.u32(number(root, "PhysicsWorld") != 0);
            for (const char *key : { "PhysicsWorldTop", "PhysicsWorldLeft", "PhysicsWorldRight", "PhysicsWorldBottom" })
                file.u32(number(root, key));
            for (const char *key : { "PhysicsWorldGravityX", "PhysicsWorldGravityY", "PhysicsWorldPixToMeters" })
                file.f32(number(root, key));
            file.patch(backgrounds, file.position());
            const auto backs = children(root.firstChildElement("backgrounds"), "background");
            file.list(backs.size(), [&](int b) {
                const auto &back = backs.at(b);
                file.u32(attribute(back, "visible") != 0);
                file.u32(attribute(back, "foreground") != 0);
                file.u32(resourceId(ResourceType::Background, back.attribute("name")));
                for (const char *key : { "x", "y" })
                    file.u32(attribute(back, key));
                for (const char *key : { "htiled", "vtiled" })
                    file.u32(attribute(back, key) != 0);
                for (const char *key : { "hspeed", "vspeed" })
                    file.u32(attribute(back, key));
                file.u32(attribute(back, "stretch") != 0);
            });
            file.patch(views, file.position());
            const auto camera = children(root.firstChildElement("views"), "view");
            file.list(camera.size(), [&](int v) {
                const auto &view = camera.at(v);
                file.u32(attribute(view, "visible") != 0);
                for (const char *key : { "xview", "yview", "wview", "hview", "xport", "yport", "wport", "hport",
                         "hborder", "vborder", "hspeed", "vspeed" })
                    file.u32(attribute(view, key));
                file.u32(resourceId(ResourceType::Object, view.attribute("objName")));
            });
            file.patch(instances, file.position());
            const auto objects = children(root.firstChildElement("instances"), "instance");
            profile.count(QStringLiteral("Instances"), objects.size());
            file.list(objects.size(), [&](int o) {
                const auto &object = objects.at(o);
                file.u32(attribute(object, "x"));
                file.u32(attribute(object, "y"));
                file.u32(resourceId(ResourceType::Object, object.attribute("objName")));
                const int id = nextInstance++;
                file.u32(id);
                file.u32(code(
                    "gml_Room_" + resource.node.name + "_Instance_" + QString::number(id), object.attribute("code")));
                file.f32(attribute(object, "scaleX", 1));
                file.f32(attribute(object, "scaleY", 1));
                // GMX stores ARGB; the Runner expects ABGR for both instances and tiles.
                const quint32 color = quint32(attribute(object, "colour", 4294967295.0));
                file.u32((color & 0xff00ff00u) | ((color & 255) << 16) | ((color >> 16) & 255));
                file.f32(attribute(object, "rotation"));
                file.u32(-1);
            });
            file.patch(tiles, file.position());
            const auto tileList = children(root.firstChildElement("tiles"), "tile");
            profile.count(QStringLiteral("Tiles"), tileList.size());
            file.list(tileList.size(), [&](int t) {
                const auto &tile = tileList.at(t);
                file.u32(attribute(tile, "x"));
                file.u32(attribute(tile, "y"));
                file.u32(resourceId(ResourceType::Background, tile.attribute("bgName")));
                for (const char *key : { "xo", "yo", "w", "h", "depth" })
                    file.u32(attribute(tile, key));
                file.u32(nextTile++);
                file.f32(attribute(tile, "scaleX", 1));
                file.f32(attribute(tile, "scaleY", 1));
                const quint32 color = quint32(attribute(tile, "colour", 4294967295.0));
                file.u32((color & 0xff00ff00u) | ((color & 255) << 16) | ((color >> 16) & 255));
            });
        });
    });
    CompileDetailScope timing(profile, QStringLiteral("Included File registration"));
    file.chunk("DAFL", [&] { file.u32(0); });
    const auto manifestDocument = xml(request.project.filePath());
    const auto manifest = manifestDocument.documentElement();
    const QString configName = request.project.configurations().at(request.configuration).name;
    std::function<void(const QDomElement &, const QString &, const QString &)> included
        = [&](const QDomElement &group, const QString &sourceDirectory, const QString &outputDirectory) {
              for (auto child = group.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
                  if (child.tagName() == "datafiles") {
                      const QString folder = child.attribute("name");
                      included(child, sourceDirectory + "/" + folder, outputDirectory + folder + "/");
                  } else if (child.tagName() == "datafile") {
                      bool enabled = true;
                      for (const auto &config : children(child.firstChildElement("ConfigOptions"), "Config"))
                          if (config.attribute("name") == configName)
                              enabled = (text(config, "CopyToMask").toULongLong() & WindowsVmTargetMask) != 0;
                      if (enabled) {
                          QString name = text(child, "name"), relative = sourceDirectory + "/" + name;
                          relative.replace('\\', '/');
                          externalFile(outputDirectory + name,
                              QFileInfo(request.project.filePath()).absoluteDir().absoluteFilePath(relative));
                      }
                  }
              }
          };
    const auto datafiles = manifest.firstChildElement("datafiles");
    included(datafiles, datafiles.attribute("name", "datafiles"), QString());
}
