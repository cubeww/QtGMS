#include "compilerbuild.h"
#include "compilertarget.h"
#include "shadercompiler.h"
#include <QDir>
#include <QRegularExpression>

void CompilerBuild::extensions()
{
    int functionId = 1;
    const QString configName = request.project.configurations().at(request.configuration).name;
    const auto active = [&](const QDomElement &element) {
        for (const auto &config : children(element.firstChildElement("ConfigOptions"), "Config"))
            if (config.attribute("name") == configName)
                return (text(config, "CopyToMask").toULongLong() & WindowsVmTargetMask) != 0;
        return true;
    };
    QVector<CompilerResource> included;
    QHash<QString, QString> disabledFunctions;
    for (const auto &extension : resources[ResourceType::Extension]) {
        const bool enabled = active(extension.xml);
        if (enabled)
            included.append(extension);
        for (const auto &entry : children(extension.xml.firstChildElement("files"), "file")) {
            if (enabled && active(entry)) {
                for (const auto &function : children(entry.firstChildElement("functions"), "function")) {
                    const QString name = text(function, "name");
                    environment.functions.insert(name);
                    const QString externalName = text(function, "externalName");
                    if (number(entry, "kind") == 2 && !externalName.isEmpty())
                        environment.extensionFunctionNames.insert(name, externalName);
                }
                for (const char *key : { "init", "final" }) {
                    const QString name = text(entry, key);
                    if (!name.isEmpty())
                        extensionEntryPoints.append(name);
                }
                continue;
            }
            for (const auto &function : children(entry.firstChildElement("functions"), "function"))
                disabledFunctions.insert(text(function, "name"), number(function, "returnType", 2) == 1
                        ? QStringLiteral("extension_stubfunc_string")
                        : QStringLiteral("extension_stubfunc_real"));
        }
    }
    file.chunk("EXTN", [&] {
        file.list(included.size(), [&](int i) {
            const auto &extension = included.at(i);
            file.string(QString());
            file.string(extension.node.name);
            file.string(text(extension.xml, "classname"));
            QVector<QDomElement> files;
            for (const auto &entry : children(extension.xml.firstChildElement("files"), "file"))
                if (active(entry))
                    files.append(entry);
            file.list(files.size(), [&](int f) {
                const auto &entry = files.at(f);
                const QString name = text(entry, "filename");
                const int kind = number(entry, "kind");
                file.string(name);
                const QString finalFunction = text(entry, "final");
                const QString initFunction = text(entry, "init");
                file.string(environment.extensionFunctionNames.value(finalFunction, finalFunction));
                file.string(environment.extensionFunctionNames.value(initFunction, initFunction));
                file.u32(kind);
                const auto functions = children(entry.firstChildElement("functions"), "function");
                file.list(kind == 2 ? 0 : functions.size(), [&](int n) {
                    const auto &function = functions.at(n);
                    file.string(text(function, "name"));
                    file.u32(functionId++);
                    file.u32(number(function, "kind", 1));
                    file.u32(number(function, "returnType", 2));
                    file.string(text(function, "externalName", text(function, "name")));
                    const auto args = children(function.firstChildElement("args"), "arg");
                    file.u32(args.size());
                    for (const auto &arg : args)
                        file.u32(arg.text().toInt());
                });
                for (const auto &constant : children(entry.firstChildElement("constants"), "constant"))
                    environment.macros.insert(text(constant, "name"), text(constant, "value"));
                QString sourceName = name;
                for (const auto &proxy : children(entry.firstChildElement("ProxyFiles"), "ProxyFile"))
                    if ((text(proxy, "TargetMask").toULongLong() & WindowsVmTargetMask) != 0) {
                        sourceName = text(proxy, "Name");
                        break;
                    }
                if (number(entry, "uncompress") != 0)
                    throw CompileError(extension.node.name + ": compressed extension archives are not supported yet");
                if (sourceName.endsWith(".ext", Qt::CaseInsensitive))
                    return;
                const QString sourcePath = assetPath(extension, extension.node.name + "/" + sourceName);
                if (kind == 2) {
                    for (const auto &function : functions)
                        extensionScriptFiles.insert(text(function, "name"), sourcePath);
                } else if (!sourceName.endsWith(".ext", Qt::CaseInsensitive))
                    external(name, read(sourcePath));
            });
        });
        // Product identifiers are metadata; unlicensed extensions use the
        // format's standard empty identifier, never another product's ID.
        for (int i = 0; i < included.size(); ++i) {
            file.u8(0);
            for (int j = 129; j <= 143; ++j)
                file.u8(j);
        }
    });
    // Match the official target resolver: active implementations, project
    // scripts and built-ins take precedence over disabled extension entries.
    for (auto it = disabledFunctions.cbegin(); it != disabledFunctions.cend(); ++it)
        if (!environment.functions.contains(it.key()))
            environment.extensionFunctionNames.insert(it.key(), it.value());
}

void CompilerBuild::includeExtensionScript(const QString &functionName)
{
    const QString path = extensionScriptFiles.value(functionName);
    if (path.isEmpty() || includedExtensionScriptFiles.contains(path))
        return;
    includedExtensionScriptFiles.insert(path);
    const QString source = QString::fromUtf8(read(path));
    const QRegularExpression define(
        QStringLiteral("^\\s*#define\\s+([A-Za-z_][A-Za-z0-9_]*)[^\\r\\n]*"),
        QRegularExpression::MultilineOption);
    auto matches = define.globalMatch(source);
    QVector<QRegularExpressionMatch> sections;
    while (matches.hasNext())
        sections.append(matches.next());
    if (sections.isEmpty() && !source.trimmed().isEmpty())
        throw CompileError(path + ": GML extension requires #define function sections");
    QSet<QString> scriptNames;
    for (const auto &script : resources[ResourceType::Script])
        scriptNames.insert(script.node.name);
    // The original compiler imports every #define in a referenced extension
    // file, but never parses files whose functions are unused.
    for (int i = 0; i < sections.size(); ++i) {
        const auto &section = sections.at(i);
        CompilerResource script;
        script.node.type = ResourceType::Script;
        script.node.name = section.captured(1);
        script.node.filePath = path;
        if (scriptNames.contains(script.node.name))
            throw CompileError(path + ": duplicate script name: " + script.node.name);
        scriptNames.insert(script.node.name);
        script.embeddedSource = source.mid(section.capturedEnd(),
            (i + 1 < sections.size() ? sections.at(i + 1).capturedStart() : source.size()) - section.capturedEnd());
        if (script.embeddedSource.trimmed().isEmpty())
            script.embeddedSource = "exit;";
        // A newly imported script also takes precedence over a disabled
        // extension's stub with the same name.
        if (!environment.functions.contains(script.node.name))
            environment.extensionFunctionNames.remove(script.node.name);
        environment.functions.insert(script.node.name);
        environment.constants.insert(script.node.name, resources[ResourceType::Script].size());
        script.codeIndex = code("gml_Script_" + script.node.name, script.embeddedSource);
        resources[ResourceType::Script].append(script);
    }
}

void CompilerBuild::shaders()
{
    file.chunk("SHDR", [&] {
        const auto &shaders = resources[ResourceType::Shader];
        file.list(shaders.size(), [&](int i) {
            const auto &resource = shaders.at(i);
            const QString type = request.project.shaderType(resource.node.filePath);
            if (type != "HLSL9" && type != "GLSLES")
                throw CompileError(resource.node.name + ": this Windows compiler supports GLSL ES and HLSL9 shaders");
            const QString source = QString::fromUtf8(read(resource.node.filePath));
            const QString marker = "//######################_==_YOYO_SHADER_MARKER_==_######################@~";
            const int split = source.indexOf(marker);
            if (split < 0)
                throw CompileError(resource.node.name + ": missing shader stage separator");
            const QString vertex = source.left(split), fragment = source.mid(split + marker.size());
            CompiledShader shader;
            QString vertexEs, fragmentEs;
            if (type == "GLSLES") {
                const QString defines = "#define LOWPREC lowp\n#define _YY_HLSL9_ 1\n";
                const QString vertexCommon = QString::fromUtf8(read(":/compiler/glslvertex.shader")),
                              fragmentCommon = QString::fromUtf8(read(":/compiler/glslfragment.shader"));
                shader = compileShader(resource.node.name, defines + vertexCommon + "\n" + vertex,
                    "precision mediump float;\n" + defines + fragmentCommon + "\n" + fragment);
                vertexEs = "#define LOWPREC lowp\n#define _YY_GLSLES_ 1\n" + vertexCommon + "\n" + vertex;
                fragmentEs = "precision mediump float;\n#define LOWPREC lowp\n#define _YY_GLSLES_ 1\n" + fragmentCommon
                    + "\n" + fragment;
            } else {
                shader.vertex
                    = QString::fromUtf8(read(":/compiler/hlsl9vertex.shader")) + "\n#define _YY_HLSL9_ 1\n" + vertex;
                shader.fragment = QString::fromUtf8(read(":/compiler/hlsl9fragment.shader"))
                    + "\n#define _YY_HLSL9_ 1\n" + fragment;
            }
            file.string(resource.node.name);
            file.u32(type == "GLSLES" ? 0x80000001u : 0x80000003u);
            file.string(vertexEs);
            file.string(fragmentEs);
            file.string(QString());
            file.string(QString());
            file.string(shader.vertex);
            file.string(shader.fragment);
            file.u32(0);
            file.u32(0);
            file.u32(shader.attributes.size());
            for (const auto &attribute : shader.attributes)
                file.string(attribute);
            file.u32(2);
            for (int s = 0; s < 12; ++s)
                file.u32(0);
        });
    });
}
