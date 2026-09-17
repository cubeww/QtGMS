#include "gm82projectimporter.h"
#include "actionxml.h"

#include <QFileInfo>

void Gm82ProjectImporter::option(const QString &key, const QString &value)
{
    ActionXml::setText(ActionXml::child(m_configuration.documentElement(), QStringLiteral("Options")), QStringLiteral("option_") + key, value);
}

void Gm82ProjectImporter::readSettings(const Gm82Properties &header)
{
    option(QStringLiteral("gameid"), QString::number(header.integer(QStringLiteral("gameid"))));
    option(QStringLiteral("author"), header.text(QStringLiteral("info_author")));
    option(QStringLiteral("version"), header.text(QStringLiteral("info_version")));
    option(QStringLiteral("information"), undelimit(header.text(QStringLiteral("info_information"))));
    for (const QString &key : QStringLiteral("company|product|copyright|description").split(QLatin1Char('|'))) {
        const QString value = header.text(QStringLiteral("exe_") + key);
        option(QStringLiteral("version_") + key, value);
        option(QStringLiteral("windows_") + key + QStringLiteral("_info"), value);
    }
    const QStringList version = header.text(QStringLiteral("exe_version"), QStringLiteral("1.0.0.0")).split(QLatin1Char('.'));
    if (version.size() != 4) throw QObject::tr("Invalid GM82 executable version.");
    const QStringList components = QStringLiteral("major|minor|release|build").split(QLatin1Char('|'));
    const QStringList windows = QStringLiteral("major|mainor|release|build").split(QLatin1Char('|'));
    for (int i = 0; i < 4; ++i) {
        bool ok;
        const uint value = version.at(i).toUInt(&ok);
        if (!ok || value > 65535) throw QObject::tr("Invalid GM82 executable version.");
        option(QStringLiteral("version_") + components.at(i), QString::number(value));
        option(QStringLiteral("windows_") + windows.at(i) + QStringLiteral("_version"), QString::number(value));
    }
    const auto settings = readProperties(QStringLiteral("settings/settings.txt"));
    for (const QString &pair : QStringLiteral("fullscreen:fullscreen|interpolate_pixels:interpolate|dont_draw_border:noborder|display_cursor:showcursor|allow_resize:sizeable|window_on_top:stayontop|set_resolution:changeresolution|dont_show_buttons:nobuttons|disable_screensaver:noscreensaver|f4_fullscreen_toggle:screenkey|f1_help_menu:helpkey|esc_close_game:quitkey|f5_save_f6_load:savekey|f9_screenshot:screenshotkey|treat_close_as_esc:closeesc|freeze_on_lose_focus:freeze|transparent:loadtransparent|scale_progress_bar:scaleprogress|show_error_messages:displayerrors|log_errors:writeerrors|always_abort:aborterrors|zero_uninitialized_vars:variableerrors|error_on_uninitialized_args:argumenterrors").split(QLatin1Char('|'))) {
        const QString source = pair.section(QLatin1Char(':'), 0, 0);
        if (settings.values.contains(source)) option(pair.section(QLatin1Char(':'), 1), settings.boolean(source) ? QStringLiteral("true") : QStringLiteral("false"));
    }
    for (const QString &pair : QStringLiteral("scaling:scale|color_depth:colordepth|resolution:resolution|frequency:frequency|priority:priority|custom_bar:showprogress|translucency:loadalpha").split(QLatin1Char('|'))) {
        const QString source = pair.section(QLatin1Char(':'), 0, 0);
        if (settings.values.contains(source)) option(pair.section(QLatin1Char(':'), 1), QString::number(qint32(settings.integer(source))));
    }
    option(QStringLiteral("windowcolor"), QStringLiteral("$%1").arg(quint32(settings.integer(QStringLiteral("clear_color"))), 8, 16, QLatin1Char('0')));
    option(QStringLiteral("sync_vertex"), settings.boolean(QStringLiteral("vsync")) ? QStringLiteral("1") : QStringLiteral("0"));
    if (settings.boolean(QStringLiteral("swap_creation_events")))
        m_warnings.append(QObject::tr("GM82's swapped creation event order was not converted. Review initialization code in GMS."));
    option(QStringLiteral("use_new_audio"), QStringLiteral("false"));
    option(QStringLiteral("shortcircuit"), QStringLiteral("false"));
    const bool loader = settings.boolean(QStringLiteral("custom_loader"));
    option(QStringLiteral("windows_use_splash"), loader ? QStringLiteral("true") : QStringLiteral("false"));
    if (loader) {
        const QString destination = QStringLiteral("Configs/Default/windows/splash.bmp");
        copy(QStringLiteral("settings/loader.bmp"), destination);
        option(QStringLiteral("loadimage"), QString(destination).replace(QLatin1Char('/'), QLatin1Char('\\')));
    }
    if (settings.integer(QStringLiteral("custom_bar")) == 2) {
        for (const QString &key : {QStringLiteral("bg"), QStringLiteral("fg")}) {
            if (!settings.boolean(QStringLiteral("bar_has_") + key)) continue;
            const QString source = key == QStringLiteral("bg") ? QStringLiteral("back") : QStringLiteral("front");
            const QString destination = QStringLiteral("Configs/Default/windows/") + source + QStringLiteral(".bmp");
            copy(QStringLiteral("settings/") + source + QStringLiteral(".bmp"), destination);
            option(source + QStringLiteral("image"), QString(destination).replace(QLatin1Char('/'), QLatin1Char('\\')));
        }
    }
    copy(QStringLiteral("settings/icon.ico"), QStringLiteral("Configs/Default/windows/runner_icon.ico"));
    auto group = ActionXml::child(m_manifest.documentElement(), QStringLiteral("constants"));
    // Keep the declaration order (and repeated entries) from the source file.
    for (const QString &line : readText(QStringLiteral("settings/constants.txt")).split(QLatin1Char('\n'))) {
        if (line.isEmpty()) continue;
        const auto properties = Gm82Properties::parse(line, QStringLiteral("settings/constants.txt"));
        const auto value = properties.values.cbegin();
        auto constant = m_manifest.createElement(QStringLiteral("constant"));
        constant.setAttribute(QStringLiteral("name"), value.key());
        constant.appendChild(m_manifest.createTextNode(value.value()));
        group.appendChild(constant);
    }
    const auto info = readProperties(QStringLiteral("settings/game_information.txt"));
    auto help = ActionXml::child(m_manifest.documentElement(), QStringLiteral("help"));
    fields(help, info, QStringLiteral("color:backgroundColor|left|top|width|height"));
    fields(help, info, QStringLiteral("new_window:separateWindow|border:showBorder|resizable:allowResize|window_on_top:stayOnTop|freeze_game:pauseGame"), true);
    ActionXml::setText(help, QStringLiteral("caption"), info.text(QStringLiteral("caption")));
    copy(QStringLiteral("settings/game_information.rtf"), QStringLiteral("help.rtf"));
    for (const QString &extension : readText(QStringLiteral("settings/extensions.txt")).split(QLatin1Char('\n'))) {
        if (!extension.isEmpty()) m_warnings.append(QObject::tr("GM82 extension '%1' is not embedded in the project. Add a compatible GMS extension manually.").arg(extension));
    }
}

void Gm82ProjectImporter::readIncludedFiles()
{
    auto group = ActionXml::child(m_manifest.documentElement(), QStringLiteral("datafiles"));
    group.setAttribute(QStringLiteral("number"), m_indexes.value(QStringLiteral("datafiles")).size());
    for (const QString &name : m_names.value(QStringLiteral("datafiles"))) {
        if (name.isEmpty()) continue;
        const auto properties = readProperties(QStringLiteral("datafiles/") + name + QStringLiteral(".txt"));
        const QString source = QStringLiteral("datafiles/include/") + name;
        const QString target = QStringLiteral("datafiles/") + name;
        copy(source, target);
        auto file = m_manifest.createElement(QStringLiteral("datafile"));
        group.appendChild(file);
        ActionXml::setText(file, QStringLiteral("name"), name);
        ActionXml::setText(file, QStringLiteral("filename"), name);
        ActionXml::setText(file, QStringLiteral("size"), QString::number(QFileInfo(m_destination.filePath(target)).size()));
        ActionXml::setText(file, QStringLiteral("exportDir"), properties.text(QStringLiteral("export_folder")));
        fields(file, properties, QStringLiteral("export:exportAction"));
        fields(file, properties, QStringLiteral("free:freeData|overwrite|remove:removeEnd"), true);
        // GM82 keeps every payload in its directory even if 'store' is false.
        ActionXml::setText(file, QStringLiteral("store"), QStringLiteral("-1"));
        auto config = m_manifest.createElement(QStringLiteral("Config"));
        config.setAttribute(QStringLiteral("name"), QStringLiteral("Default"));
        ActionXml::child(file, QStringLiteral("ConfigOptions")).appendChild(config);
        const int exportAction = int(properties.integer(QStringLiteral("export")));
        ActionXml::setText(config, QStringLiteral("CopyToMask"), exportAction ? QStringLiteral("9223372036854775807") : QStringLiteral("0"));
        if (exportAction == 1 || exportAction == 3 || properties.boolean(QStringLiteral("remove")))
            m_warnings.append(QObject::tr("Included file '%1' uses legacy extraction options. GMS copies included files beside the game; review its file paths and cleanup code.").arg(name));
    }
}
