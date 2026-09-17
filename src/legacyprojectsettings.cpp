#include "legacyprojectimporter.h"
#include "actionxml.h"
#include "sevenziparchive.h"

#include <QFileInfo>
#include <QObject>

void LegacyProjectImporter::readSettings()
{
    m_reader.setContext(QObject::tr("Game settings"));
    const int version = m_reader.version({702, 800, 810});
    if (version >= 800) m_reader.beginBlock();
    for (const QString &key : {QStringLiteral("fullscreen"), QStringLiteral("interpolate"), QStringLiteral("noborder"), QStringLiteral("showcursor")}) booleanOption(key);
    option(QStringLiteral("scale"), QString::number(m_reader.integer()));
    booleanOption(QStringLiteral("sizeable"));
    booleanOption(QStringLiteral("stayontop"));
    option(QStringLiteral("windowcolor"), QStringLiteral("$%1").arg(quint32(m_reader.integer()), 8, 16, QLatin1Char('0')));
    booleanOption(QStringLiteral("changeresolution"));
    for (const QString &key : {QStringLiteral("colordepth"), QStringLiteral("resolution"), QStringLiteral("frequency")}) option(key, QString::number(m_reader.integer()));
    booleanOption(QStringLiteral("nobuttons"));
    option(QStringLiteral("sync_vertex"), QString::number(quint32(m_reader.integer())));
    if (version >= 800) booleanOption(QStringLiteral("noscreensaver"));
    for (const QString &key : {QStringLiteral("screenkey"), QStringLiteral("helpkey"), QStringLiteral("quitkey"), QStringLiteral("savekey"), QStringLiteral("screenshotkey"), QStringLiteral("closeesc")}) booleanOption(key);
    option(QStringLiteral("priority"), QString::number(m_reader.integer()));
    booleanOption(QStringLiteral("freeze"));
    const int progress = m_reader.integer();
    option(QStringLiteral("showprogress"), QString::number(progress));
    if (progress == 2) {
        for (const QString &key : {QStringLiteral("backimage"), QStringLiteral("frontimage")}) {
            const bool exists = version >= 800 ? m_reader.boolean() : m_reader.integer() != -1;
            if (exists) {
                const QString path = QStringLiteral("Configs/Default/windows/%1.png").arg(key);
                saveImage(path, readImage(false));
                option(key, QString(path).replace(QLatin1Char('/'), QLatin1Char('\\')));
            }
        }
    }
    const bool customLoadImage = m_reader.boolean();
    if (customLoadImage) {
        const bool exists = version >= 800 ? m_reader.boolean() : m_reader.integer() != -1;
        if (exists) {
            const QString path = QStringLiteral("Configs/Default/windows/splash.png");
            saveImage(path, readImage(false));
            option(QStringLiteral("loadimage"), QString(path).replace(QLatin1Char('/'), QLatin1Char('\\')));
            option(QStringLiteral("windows_use_splash"), QStringLiteral("true"));
        }
    }
    booleanOption(QStringLiteral("loadtransparent"));
    option(QStringLiteral("loadalpha"), QString::number(m_reader.integer()));
    booleanOption(QStringLiteral("scaleprogress"));
    const QByteArray icon = m_reader.blob();
    if (!icon.isEmpty()) write(QStringLiteral("Configs/Default/windows/runner_icon.ico"), icon);
    for (const QString &key : {QStringLiteral("displayerrors"), QStringLiteral("writeerrors"), QStringLiteral("aborterrors")}) booleanOption(key);
    const int errors = m_reader.integer();
    option(QStringLiteral("variableerrors"), errors & 1 ? QStringLiteral("true") : QStringLiteral("false"));
    option(QStringLiteral("argumenterrors"), errors & 2 ? QStringLiteral("true") : QStringLiteral("false"));
    option(QStringLiteral("author"), m_reader.string());
    option(QStringLiteral("version"), m_reader.string());
    m_reader.skip(8);
    option(QStringLiteral("information"), m_reader.string());
    if (version < 800) readConstants();
    const QStringList original = QStringLiteral("version_major|version_minor|version_release|version_build").split(QLatin1Char('|'));
    const QStringList windows = QStringLiteral("windows_major_version|windows_mainor_version|windows_release_version|windows_build_version").split(QLatin1Char('|'));
    for (int i = 0; i < original.size(); ++i) {
        const QString value = QString::number(m_reader.integer());
        option(original.at(i), value);
        option(windows.at(i), value);
    }
    for (const QString &key : {QStringLiteral("company"), QStringLiteral("product"), QStringLiteral("copyright"), QStringLiteral("description")}) {
        const QString value = m_reader.string();
        option(QStringLiteral("version_") + key, value);
        option(QStringLiteral("windows_") + key + QStringLiteral("_info"), value);
    }
    if (version >= 800) m_reader.skip(8);
    m_reader.endBlock();
    option(QStringLiteral("use_new_audio"), QStringLiteral("false"));
    option(QStringLiteral("shortcircuit"), QStringLiteral("false")); // GM7/8 evaluate both sides of boolean operators.
}

void LegacyProjectImporter::readIncludedFiles()
{
    m_reader.setContext(QObject::tr("Included files"));
    const int version = m_reader.version({620, 800, 810});
    const int count = m_reader.count(100000);
    auto group = ActionXml::child(m_manifest.documentElement(), QStringLiteral("datafiles"));
    group.setAttribute(QStringLiteral("number"), count);
    QSet<QString> names;
    for (int i = 0; i < count; ++i) {
        if (version >= 800) { m_reader.beginBlock(); m_reader.skip(8); }
        m_reader.version({620, 800, 810});
        const QString name = QString(m_reader.string()).replace(QLatin1Char('\\'), QLatin1Char('/'));
        const QString originalPath = m_reader.string();
        m_reader.boolean(); // Whether the original file was present when saved.
        const int originalSize = m_reader.count(512 * 1024 * 1024);
        const bool stored = m_reader.boolean();
        const QByteArray bytes = stored ? m_reader.blob() : QByteArray();
        const int exportAction = m_reader.integer();
        const QString exportDirectory = m_reader.string();
        const bool overwrite = m_reader.boolean();
        const bool freeData = m_reader.boolean();
        const bool removeEnd = m_reader.boolean();
        m_reader.endBlock();
        if (!SevenZipArchive::validPath(name) || names.contains(name.toCaseFolded()))
            m_reader.fail(QObject::tr("Invalid or duplicate included filename: %1").arg(name));
        names.insert(name.toCaseFolded());
        m_progress(name, 81 + i * 5 / qMax(1, count));
        bool present = stored;
        if (stored) write(QStringLiteral("datafiles/") + name, bytes);
        else {
            // External paths in a project are not authority to read arbitrary
            // files on this machine. Resolve only files alongside the source.
            QFile source(QFileInfo(m_source).absoluteDir().filePath(name));
            if (source.open(QIODevice::ReadOnly)) {
                const QByteArray data = source.readAll();
                if (source.error() != QFile::NoError) m_reader.fail(source.errorString());
                write(QStringLiteral("datafiles/") + name, data);
                present = true;
            }
        }
        auto parent = group;
        const QStringList components = name.split(QLatin1Char('/'));
        for (int part = 0; part + 1 < components.size(); ++part) {
            QDomElement folder;
            for (auto candidate : ActionXml::elements(parent, QStringLiteral("datafiles")))
                if (candidate.attribute(QStringLiteral("name")) == components.at(part)) { folder = candidate; break; }
            if (folder.isNull()) {
                folder = m_manifest.createElement(QStringLiteral("datafiles"));
                folder.setAttribute(QStringLiteral("name"), components.at(part));
                parent.appendChild(folder);
            }
            parent = folder;
        }
        auto file = m_manifest.createElement(QStringLiteral("datafile"));
        parent.appendChild(file);
        ActionXml::setText(file, QStringLiteral("name"), components.last());
        ActionXml::setText(file, QStringLiteral("filename"), originalPath);
        ActionXml::setText(file, QStringLiteral("size"), QString::number(stored ? bytes.size() : originalSize));
        ActionXml::setText(file, QStringLiteral("exportDir"), exportDirectory);
        ActionXml::setText(file, QStringLiteral("exportAction"), QString::number(exportAction));
        ActionXml::setText(file, QStringLiteral("overwrite"), overwrite ? QStringLiteral("-1") : QStringLiteral("0"));
        ActionXml::setText(file, QStringLiteral("store"), present ? QStringLiteral("-1") : QStringLiteral("0"));
        ActionXml::setText(file, QStringLiteral("freeData"), freeData ? QStringLiteral("-1") : QStringLiteral("0"));
        ActionXml::setText(file, QStringLiteral("removeEnd"), removeEnd ? QStringLiteral("-1") : QStringLiteral("0"));
        auto configuration = m_manifest.createElement(QStringLiteral("Config"));
        configuration.setAttribute(QStringLiteral("name"), QStringLiteral("Default"));
        ActionXml::child(file, QStringLiteral("ConfigOptions")).appendChild(configuration);
        ActionXml::setText(configuration, QStringLiteral("CopyToMask"), exportAction != 0 && present ? QStringLiteral("9223372036854775807") : QStringLiteral("0"));
        if (!present) m_warnings.append(QObject::tr("Included file '%1' was not embedded and could not be found beside the project. Restore it manually.").arg(name));
        if (exportAction == 1 || exportAction == 3 || removeEnd)
            m_warnings.append(QObject::tr("Included file '%1' uses legacy extraction options. GMS copies included files beside the game; review its file paths and cleanup code.").arg(name));
    }
}

void LegacyProjectImporter::readInformation()
{
    m_reader.setContext(QObject::tr("Game information"));
    const int version = m_reader.version({600, 620, 800, 810});
    if (version >= 800) m_reader.beginBlock();
    const auto help = ActionXml::child(m_manifest.documentElement(), QStringLiteral("help"));
    ActionXml::setText(help, QStringLiteral("backgroundColor"), QString::number(m_reader.integer()));
    const bool separate = m_reader.boolean();
    ActionXml::setText(help, QStringLiteral("separateWindow"), (version >= 800 ? separate : !separate) ? QStringLiteral("-1") : QStringLiteral("0"));
    ActionXml::setText(help, QStringLiteral("caption"), m_reader.string());
    integerFields(help, QStringLiteral("left|top|width|height"));
    booleanFields(help, QStringLiteral("showBorder|allowResize|stayOnTop|pauseGame"));
    if (version >= 800) m_reader.skip(8);
    // RTF has its own encoding declarations; preserve its original bytes.
    write(QStringLiteral("help.rtf"), m_reader.blob());
    m_reader.endBlock();
}
