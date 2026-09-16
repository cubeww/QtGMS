#ifndef QTGMS_SHADEREDITORWINDOW_H
#define QTGMS_SHADEREDITORWINDOW_H

#include "resourceeditorwindow.h"
#include <QTextCursor>

class CodeEditorPanel;
class ShaderDocument;
class ShaderHighlighter;
class QComboBox;
class QTabBar;

class ShaderEditorWindow : public ResourceEditorWindow
{
    Q_OBJECT
public:
    explicit ShaderEditorWindow(ShaderDocument *document, Project *project, QWidget *parent = nullptr);
    bool save() override;
    void selectCode(int stage, int offset, int length);
    QString filePath() const override;
    void relocate(const QString &oldDirectory, const QString &newDirectory) override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    bool isModified() const;
    void refreshTitle();
    void changeStage(int stage);
    void importCode();
    void exportCode();
    ShaderDocument *m_document;
    Project *m_project;
    CodeEditorPanel *m_codePanel;
    QTabBar *m_tabs;
    QComboBox *m_typeCombo;
    ShaderHighlighter *m_highlighters[2];
    QTextCursor m_cursors[2];
    int m_verticalScroll[2] = {0, 0};
    int m_horizontalScroll[2] = {0, 0};
    int m_stage;
    QString m_savedType;
};

#endif
