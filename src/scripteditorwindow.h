#ifndef QTGMS_SCRIPTEDITORWINDOW_H
#define QTGMS_SCRIPTEDITORWINDOW_H

#include "resourceeditorwindow.h"

class TextFileDocument;
class CodeEditorPanel;
class GmlHighlighter;

class ScriptEditorWindow : public ResourceEditorWindow
{
    Q_OBJECT
public:
    ScriptEditorWindow(TextFileDocument *document, const Project &project, QWidget *parent = nullptr);
    bool save() override;
    QString filePath() const override;
    void relocate(const QString &oldDirectory, const QString &newDirectory) override;
protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
private:
    void refreshTitle();
    void importCode();
    void exportCode();
    TextFileDocument *m_document;
    CodeEditorPanel *m_codePanel;
    const Project *m_project;
    GmlHighlighter *m_highlighter;
};

#endif
