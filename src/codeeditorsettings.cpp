#include "codeeditorsettings.h"
#include <QSettings>

static CodeEditorOptions normalizedOptions(CodeEditorOptions options)
{
    if (options.fontFamily.trimmed().isEmpty()) options.fontFamily = QStringLiteral("Courier New");
    options.fontPixelSize = qBound(8, options.fontPixelSize, 32);
    options.indentSize = qBound(1, options.indentSize, 16);
    options.completionDelay = qBound(0, options.completionDelay, 2000);
    return options;
}

CodeEditorSettings &CodeEditorSettings::instance()
{
    static CodeEditorSettings settings;
    return settings;
}

CodeEditorSettings::CodeEditorSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("codeEditor"));
    m_options.fontFamily = settings.value(QStringLiteral("fontFamily"), m_options.fontFamily).toString();
    m_options.fontPixelSize = settings.value(QStringLiteral("fontPixelSize"), m_options.fontPixelSize).toInt();
    m_options.indentSize = settings.value(QStringLiteral("indentSize"), m_options.indentSize).toInt();
    m_options.completionDelay = settings.value(QStringLiteral("completionDelay"), m_options.completionDelay).toInt();
    m_options.automaticIndentation = settings.value(QStringLiteral("automaticIndentation"), m_options.automaticIndentation).toBool();
    m_options.automaticBrackets = settings.value(QStringLiteral("automaticBrackets"), m_options.automaticBrackets).toBool();
    m_options.automaticCompletion = settings.value(QStringLiteral("automaticCompletion"), m_options.automaticCompletion).toBool();
    m_options.functionHelp = settings.value(QStringLiteral("functionHelp"), m_options.functionHelp).toBool();
    m_options.lineNumbers = settings.value(QStringLiteral("lineNumbers"), m_options.lineNumbers).toBool();
    m_options.matchingBrackets = settings.value(QStringLiteral("matchingBrackets"), m_options.matchingBrackets).toBool();
    m_options.indentGuides = settings.value(QStringLiteral("indentGuides"), m_options.indentGuides).toBool();
    m_options.currentLine = settings.value(QStringLiteral("currentLine"), m_options.currentLine).toBool();
    m_options.searchHighlights = settings.value(QStringLiteral("searchHighlights"), m_options.searchHighlights).toBool();
    m_options = normalizedOptions(m_options);
}

void CodeEditorSettings::setOptions(const CodeEditorOptions &options)
{
    m_options = normalizedOptions(options);
    QSettings settings;
    settings.beginGroup(QStringLiteral("codeEditor"));
    settings.setValue(QStringLiteral("fontFamily"), m_options.fontFamily);
    settings.setValue(QStringLiteral("fontPixelSize"), m_options.fontPixelSize);
    settings.setValue(QStringLiteral("indentSize"), m_options.indentSize);
    settings.setValue(QStringLiteral("completionDelay"), m_options.completionDelay);
    settings.setValue(QStringLiteral("automaticIndentation"), m_options.automaticIndentation);
    settings.setValue(QStringLiteral("automaticBrackets"), m_options.automaticBrackets);
    settings.setValue(QStringLiteral("automaticCompletion"), m_options.automaticCompletion);
    settings.setValue(QStringLiteral("functionHelp"), m_options.functionHelp);
    settings.setValue(QStringLiteral("lineNumbers"), m_options.lineNumbers);
    settings.setValue(QStringLiteral("matchingBrackets"), m_options.matchingBrackets);
    settings.setValue(QStringLiteral("indentGuides"), m_options.indentGuides);
    settings.setValue(QStringLiteral("currentLine"), m_options.currentLine);
    settings.setValue(QStringLiteral("searchHighlights"), m_options.searchHighlights);
    emit changed();
}
