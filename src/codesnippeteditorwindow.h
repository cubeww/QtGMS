#ifndef QTGMS_CODESNIPPETEDITORWINDOW_H
#define QTGMS_CODESNIPPETEDITORWINDOW_H

#include "editorwindow.h"
class QTextDocument;
class CodeEditorPanel;
class GmlHighlighter;
class Project;

// Private code transaction shared by action, room and instance code editors.
class CodeSnippetEditorWindow : public EditorWindow
{
    Q_OBJECT
public:
    CodeSnippetEditorWindow(const QString &title, const QString &tabName, const QString &code,
                            const QString &contextName, QWidget *parent = nullptr);
    bool exec();
    QString code() const;
    void setResources(const Project &project);
    void selectRange(int offset, int length);
    void setReadOnly(bool readOnly);
    virtual bool commitChanges();
    virtual bool hasChanges() const;
signals:
    void finished();
protected:
    void changeEvent(QEvent *event) override;
    void resetCode(const QString &code);
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    QTextDocument *m_document;
    CodeEditorPanel *m_codePanel;
private:
    const Project *m_project = nullptr;
    GmlHighlighter *m_highlighter;
    void accept();
    void importCode();
    void exportCode();
    void updateFontDescription();
    QString m_contextName;
    QString m_originalCode;
    bool m_accepted = false;
};
#endif
