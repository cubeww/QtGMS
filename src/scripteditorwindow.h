#ifndef QTGMS_SCRIPTEDITORWINDOW_H
#define QTGMS_SCRIPTEDITORWINDOW_H

#include "resourceeditorwindow.h"
#include <QVector>
#include <QTextCursor>

class TextFileDocument;
class CodeEditorPanel;
class GmlHighlighter;
class QTabBar;
class QLineEdit;

class ScriptEditorWindow : public ResourceEditorWindow
{
    Q_OBJECT
public:
    ScriptEditorWindow(TextFileDocument *document, const Project &project, QWidget *parent = nullptr);
    bool save() override;
    QString filePath() const override;
    void relocate(const QString &oldDirectory, const QString &newDirectory) override;
    void selectCode(int offset, int length);
    void selectScript(const QString &name);
protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
private:
    void refreshTitle();
    void importCode();
    void exportCode();
    void loadSections();
    void rebuildTabs(int selected);
    void changeTab(int index);
    void addTab();
    void removeTab(int index);
    void renameTab();
    void synchronizeSource();
    void refreshSymbols();
    bool nameAvailable(const QString &name, int except, QString &error) const;
    struct ScriptTab {
        QString name;
        QTextDocument *document = nullptr;
        GmlHighlighter *highlighter = nullptr;
        QTextCursor cursor;
        int scroll = 0;
    };
    QVector<ScriptTab> m_tabs;
    QTabBar *m_tabBar = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    int m_currentTab = -1;
    bool m_definitions = false;
    QString m_savedSource;
    TextFileDocument *m_document;
    CodeEditorPanel *m_codePanel;
    const Project *m_project;
};

#endif
