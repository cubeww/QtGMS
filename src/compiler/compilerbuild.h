#ifndef QTGMS_COMPILERBUILD_H
#define QTGMS_COMPILERBUILD_H
#include "projectcompiler.h"
#include "gmlcompiler.h"
#include "texturecompiler.h"
#include <QDomDocument>
#include <QTemporaryFile>

class ProjectFileTransaction;

struct CompilerResource
{
    ResourceNode node;
    QDomDocument document;
    QDomElement xml;
    QByteArray sourceBytes;
    QString embeddedSource;
    int codeIndex = -1;
};
struct CompilerAudioEntry
{
    qint64 offset = 0;
    int size = 0;
};
struct CompilerBuild
{
    CompilerBuild(const CompileRequest &request, ProjectFileTransaction &outputTransaction);
    const CompileRequest &request;
    ProjectFileTransaction &outputTransaction;
    DataWriter file;
    TextureCompiler textures;
    GmlEnvironment environment;
    QMap<ResourceType, QVector<CompilerResource>> resources;
    QVector<VmCode> codes;
    QHash<QString, QString> extensionScriptFiles;
    QSet<QString> includedExtensionScriptFiles;
    QStringList extensionEntryPoints;
    // Source paths for copied files; final output paths for generated files.
    QMap<QString, QString> externalFiles;
    QMap<int, QVector<CompilerAudioEntry>> audio;
    QTemporaryFile audioData;
    QMap<QString, QString> options;
    int instanceId = 100000;
    int tileId = 10000000;
    int classificationOffset = 0;
    void load();
    void general();
    void assets(CompileProfile &profile, const std::function<void(const QString &)> &progress);
    void objects();
    void rooms(CompileProfile &profile);
    void extensions();
    void includeExtensionScript(const QString &functionName);
    void scripts();
    void shaders();
    int code(const QString &name, const QString &source);
    int resourceId(ResourceType type, const QString &name, int absent = -1) const;
    int option(const char *key, int fallback = 0) const;
    QString actionCode(const QDomElement &event);
    void external(const QString &name, const QByteArray &bytes);
    void externalData(const QString &name, const std::function<void(DataWriter &)> &write);
    void externalFile(const QString &name, const QString &sourcePath);
    int addAudio(int group, const QByteArray &bytes);
    static QByteArray read(const QString &path);
    static QDomDocument xml(const QString &path, QByteArray *sourceBytes = nullptr);
    static QString text(const QDomElement &element, const char *key, const QString &fallback = QString());
    static double number(const QDomElement &element, const char *key, double fallback = 0);
    static double attribute(const QDomElement &element, const char *key, double fallback = 0);
    static QVector<QDomElement> children(const QDomElement &element, const char *name);
    static QString assetPath(const CompilerResource &resource, const QString &relative);
};
#endif
