#ifndef QTGMS_CODEEDITOR_H
#define QTGMS_CODEEDITOR_H

#include <QPlainTextEdit>
#include "codecompletionitem.h"
#include "codeeditorsettings.h"
#include <QVector>

class CodeCompletion;
class CodeSignatureHelp;

class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit CodeEditor(QWidget *parent = nullptr);
    int lineNumberWidth() const;
    void paintLineNumbers(QPaintEvent *event);
    void indentSelection(bool unindent = false);
    void toggleLineComment(const QString &prefix);
    void setSearchHighlights(const QList<QTextEdit::ExtraSelection> &selections);
    void goToLine(int line);
    void setCompletionItems(const QVector<CodeCompletionItem> &items);
    void requestCompletion();
    void moveLines(bool down);
    void duplicateLines(bool down);
    void deleteLines();
signals:
    void identifierActivated(const QString &name);
    void insertModeChanged(bool overwrite);
    void signatureHelpChanged(const QString &text, int parameterStart, int parameterLength);
protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
private:
    void updateGutter();
    void applySettings();
    void updateHighlights();
    void setCodePixelSize(int size);
    bool isCodePosition(int position) const;
    int matchingBracket(int position) const;
    bool editBracket(QKeyEvent *event);
    void insertIndentedLine();
    void insertAdjacentLine(bool above);
    bool deleteIndent();
    void editLines(bool down, bool duplicate);
    QWidget *m_lineNumbers;
    QList<QTextEdit::ExtraSelection> m_searchHighlights;
    int m_indentSize;
    CodeEditorOptions m_options;
    bool m_highlightsPending = false;
    CodeCompletion *m_completion = nullptr;
    CodeSignatureHelp *m_signatureHelp;
};

#endif
