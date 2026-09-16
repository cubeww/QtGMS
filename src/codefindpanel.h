#ifndef QTGMS_CODEFINDPANEL_H
#define QTGMS_CODEFINDPANEL_H

#include <QWidget>
#include <QTextDocument>

class CodeEditor;
class QCheckBox;
class QComboBox;
class QLabel;
class QTimer;

class CodeFindPanel : public QWidget
{
    Q_OBJECT
public:
    explicit CodeFindPanel(CodeEditor *editor, QWidget *parent = nullptr);
    void open(bool replace);
    void find(bool backwards = false, bool boundary = false);
    void refreshHighlights();
protected:
    void hideEvent(QHideEvent *event) override;
private:
    QTextDocument::FindFlags findFlags(bool backwards = false) const;
    bool selectionMatches() const;
    void replace(bool backwards, bool boundary);
    void replaceAll();
    void highlightMatches();
    void remember(QComboBox *box);
    CodeEditor *m_editor;
    QComboBox *m_findBox;
    QComboBox *m_replaceBox;
    QCheckBox *m_caseCheck;
    QCheckBox *m_wordCheck;
    QLabel *m_resultLabel;
    QTimer *m_highlightTimer;
};

#endif
