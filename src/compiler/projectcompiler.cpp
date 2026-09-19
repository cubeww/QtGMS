#include "projectcompiler.h"
#include "compilerbuild.h"
#include "builddirectory.h"
#include "projectfiletransaction.h"
#include "runnericon.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>

CompileResult ProjectCompiler::compile(
    const CompileRequest &request, const std::function<void(const QString &, int, int)> &progress)
{
    CompileResult result;
    // Progress counts completed compiler phases, not estimated elapsed time.
    int completedStages = 0;
    const int StageCount = 12;
    const auto stage = [&](const QString &message) { progress(message, completedStages++, StageCount); };
    QElapsedTimer timer;
    timer.start();
    CompileProfile profile(result.timings);
    profile.start(QStringLiteral("Output directory validation"));
    try {
        QString directoryError;
        if (!BuildDirectory::validate(request.project, directoryError))
            throw CompileError(directoryError);
        QDir output(BuildDirectory::path(request.project));
        if (!QDir().mkpath(output.absolutePath()))
            throw CompileError(QStringLiteral("Cannot create the output directory."));
        ProjectFileTransaction transaction(output.absolutePath());
        QString error;
        try {
            // Both the writer and its file are destroyed before the catch
            // handlers restore previous output files on failure.
            const QString data = output.absoluteFilePath(QStringLiteral("data.win"));
            QFile dataFile(data);
            CompilerBuild build(request, transaction);
            profile.start(QStringLiteral("Project / configuration / resource XML loading"));
            stage(QStringLiteral("Reading project and configuration..."));
            {
                CompileDetailScope timing(profile, QStringLiteral("Resource loading / symbol indexing"));
                build.load();
            }
            {
                CompileDetailScope timing(profile, QStringLiteral("Previous data.win backup / output opening"));
                if (!transaction.openWrite(dataFile, error))
                    throw CompileError(error);
            }
            build.file = DataWriter(dataFile);
            profile.start(QStringLiteral("Game metadata / extensions"));
            stage(QStringLiteral("Compiling game metadata and extensions..."));
            build.general();
            build.extensions();
            stage(QStringLiteral("Compiling sprites, backgrounds, audio, paths, scripts and fonts..."));
            build.assets(profile, [&](const QString &message) {
                progress(message, completedStages - 1, StageCount);
            });
            profile.start(QStringLiteral("Object / timeline events"));
            stage(QStringLiteral("Compiling object and timeline events..."));
            build.objects();
            profile.start(QStringLiteral("Rooms / Included Files"));
            stage(QStringLiteral("Compiling rooms and Included Files..."));
            build.rooms(profile);
            // Resource XML has been serialized. Only script metadata is still
            // needed while discovering extension dependencies and writing SCPT.
            {
                CompileDetailScope timing(profile, QStringLiteral("Resource XML release"));
                for (auto it = build.resources.begin(); it != build.resources.end();) {
                    if (it.key() != ResourceType::Script)
                        it = build.resources.erase(it);
                    else {
                        for (auto &script : it.value())
                            script.embeddedSource.clear();
                        ++it;
                    }
                }
            }
            profile.start(QStringLiteral("Texture page packing"));
            stage(QStringLiteral("Packing texture pages..."));
            build.textures.write(build.file, qBound(256, build.option("windows_texture_page", 2048), 8192));
            profile.start(QStringLiteral("GML parsing / symbol preparation"));
            stage(QStringLiteral("Compiling %1 GML code blocks (VM16)...").arg(build.codes.size()));
            for (const auto &function : build.extensionEntryPoints)
                build.includeExtensionScript(function);
            prepareGml(build.codes, build.environment,
                [&](const QString &function) { build.includeExtensionScript(function); });
            build.scripts();
            build.resources.clear();
            build.extensionScriptFiles.clear();
            build.includedExtensionScriptFiles.clear();
            build.extensionEntryPoints.clear();
            profile.start(QStringLiteral("GML bytecode generation"));
            stage(QStringLiteral("Generating GML bytecode..."));
            for (auto &code : build.codes) {
                compileGml(code, build.file, build.environment);
                code.syntax.clear();
                code.source.clear();
            }
            profile.start(QStringLiteral("Bytecode linking / string table"));
            stage(QStringLiteral("Linking bytecode and strings..."));
            quint64 classifications = 0;
            for (const auto &code : build.codes)
                for (const auto &reference : code.references)
                    if (reference.function)
                        classifications |= build.environment.functionClassifications.value(reference.name);
            build.file.patch(build.classificationOffset, quint32(classifications));
            build.file.patch(build.classificationOffset + 4, quint32(classifications >> 32));
            {
                CompileDetailScope timing(profile, QStringLiteral("Bytecode / symbol table linking and serialization"));
                writeVmChunks(build.file, build.codes, build.environment);
            }
            {
                CompileDetailScope timing(profile, QStringLiteral("Code / symbol metadata release"));
                QVector<VmCode>().swap(build.codes);
                build.environment = GmlEnvironment();
            }
            {
                CompileDetailScope timing(profile, QStringLiteral("String table serialization / reference patching"));
                build.file.strings();
            }
            profile.start(QStringLiteral("Texture PNG encoding / cache"));
            stage(QStringLiteral("Encoding texture pages..."));
            build.textures.writePages(build.file, profile);
            profile.start(QStringLiteral("Audio group assembly"));
            stage(QStringLiteral("Assembling audio groups..."));
            const auto writeAudio = [&](DataWriter &file, int group) {
                const auto &entries = build.audio[group];
                file.chunk("AUDO", [&] {
                    file.u32(entries.size());
                    int table = file.position();
                    for (int i = 0; i < entries.size(); ++i)
                        file.u32(0);
                    for (int i = 0; i < entries.size(); ++i) {
                        file.align(4);
                        file.patch(table + i * 4, file.position());
                        const auto &entry = entries.at(i);
                        file.u32(entry.size);
                        if (!build.audioData.seek(entry.offset))
                            throw CompileError(QStringLiteral("Cannot seek compiled audio: %1").arg(build.audioData.errorString()));
                        int remaining = entry.size;
                        while (remaining > 0) {
                            const QByteArray block = build.audioData.read(qMin(remaining, 1024 * 1024));
                            if (block.isEmpty() || build.audioData.error() != QFile::NoError)
                                throw CompileError(QStringLiteral("Cannot read compiled audio: %1").arg(build.audioData.errorString()));
                            file.append(block);
                            remaining -= block.size();
                        }
                    }
                });
            };
            writeAudio(build.file, 0);
            build.file.finish();
            if (!dataFile.flush())
                throw CompileError(QStringLiteral("Cannot save %1: %2").arg(data, dataFile.errorString()));
            dataFile.close();
            for (int group = 1; group < request.project.audioGroups().size(); ++group) {
                build.externalData(QStringLiteral("audiogroup%1.dat").arg(group), [&](DataWriter &audio) {
                    audio.append("FORM", 4);
                    audio.u32(0);
                    writeAudio(audio, group);
                    audio.finish();
                });
            }
            profile.start(QStringLiteral("Output file writing / transaction"));
            stage(QStringLiteral("Writing data.win and companion files..."));
            QByteArray ini = "[Windows]\r\n";
            const char *iniNames[]
                = { "CreateTexturesOnDemand", "AlternateSyncMethod", "VertexBufferMethod", "SleepMargin" };
            const char *optionNames[] = { "windows_create_textures_on_demand", "windows_alternate_sync_method",
                "windows_vertex_buffer_method2", "windows_sleep_margin" };
            for (int i = 0; i < 4; ++i)
                ini += QByteArray(iniNames[i]) + "=" + QByteArray::number(build.option(optionNames[i], i >= 2 ? 1 : 0))
                    + "\r\n";
            build.external("options.ini", ini);
            QString splash = build.options.value("option_windows_splash_screen");
            splash.replace('\\', '/');
            if (build.option("windows_use_splash", 1) && !splash.isEmpty())
                build.externalFile("splash.png",
                    QFileInfo(request.project.filePath()).absoluteDir().absoluteFilePath(splash));
            const QString executablePath = BuildDirectory::executablePath(request.project);
            const QDir runtime(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runtime/windows")));
            for (const auto &name : {QStringLiteral("Runner.exe"), QStringLiteral("d3dx9_43.dll"), QStringLiteral("d3dcompiler_43.dll")})
                build.externalFile(name == QStringLiteral("Runner.exe") ? QFileInfo(executablePath).fileName() : name,
                    runtime.filePath(name));
            QString iconPath = build.options.value(QStringLiteral("option_windows_game_icon")).trimmed();
            if (!iconPath.isEmpty()) {
                iconPath = QFileInfo(request.project.filePath()).absoluteDir().absoluteFilePath(iconPath.replace('\\', '/'));
                // A missing optional project icon leaves the freshly copied
                // Runner's built-in icon intact. Existing invalid icons still
                // report their read/format errors when replaced below.
                if (!QFileInfo::exists(iconPath)) {
                    progress(QStringLiteral("WARNING: Game icon not found: %1. Using the Runner's default icon.")
                            .arg(iconPath), completedStages - 1, StageCount);
                    iconPath.clear();
                }
            }
            // The Windows Runner checks for splash.png on startup. Omitting a
            // disabled splash from this build must also remove the previous one.
            bool hasSplash = false;
            for (auto it = build.externalFiles.cbegin(); it != build.externalFiles.cend(); ++it)
                hasSplash |= it.key().compare(QStringLiteral("splash.png"), Qt::CaseInsensitive) == 0;
            if (!hasSplash && !transaction.remove(output.filePath(QStringLiteral("splash.png")), error))
                throw CompileError(error);
            for (auto it = build.externalFiles.cbegin(); it != build.externalFiles.cend(); ++it) {
                const QString path = output.absoluteFilePath(it.key());
                if (it.value() != path && !transaction.copy(it.value(), path, error, true))
                    throw CompileError(error);
                result.files.append(path);
            }
            if (!iconPath.isEmpty() && !RunnerIcon::replace(executablePath, iconPath, error))
                throw CompileError(error);
            result.files.prepend(data);
            profile.start(QStringLiteral("Releasing compiler data"));
            if (!transaction.finish(error))
                throw CompileError(error);
            result.success = true;
        } catch (const CompileError &failure) {
            error = failure.message;
            transaction.rollback(error);
            throw CompileError(error);
        } catch (...) {
            transaction.rollback(error);
            throw;
        }
    } catch (const CompileError &error) {
        result.error = error.message;
        result.files.clear();
    } catch (const std::bad_alloc &) {
        result.error = QStringLiteral("Compilation exceeded the 32-bit process memory limit.");
        result.files.clear();
    }
    profile.finish();
    result.elapsedMilliseconds = timer.elapsed();
    return result;
}
