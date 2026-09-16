#ifndef QTGMS_CODEEDITORSETTINGS_H
#define QTGMS_CODEEDITORSETTINGS_H

#include <QObject>
#include <QString>

struct CodeEditorOptions
{
    QString fontFamily = QStringLiteral("Courier New");
    int fontPixelSize = 13;
    int indentSize = 4;
    int completionDelay = 60;
    bool automaticIndentation = true;
    bool automaticBrackets = true;
    bool automaticCompletion = true;
    bool functionHelp = true;
    bool lineNumbers = true;
    bool matchingBrackets = true;
    bool indentGuides = true;
    bool currentLine = true;
    bool searchHighlights = true;
};

class CodeEditorSettings : public QObject
{
    Q_OBJECT
public:
    static CodeEditorSettings &instance();
    const CodeEditorOptions &options() const { return m_options; }
    void setOptions(const CodeEditorOptions &options);
signals:
    void changed();
private:
    CodeEditorSettings();
    CodeEditorOptions m_options;
};

#endif
