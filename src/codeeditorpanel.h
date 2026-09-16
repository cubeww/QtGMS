#ifndef QTGMS_CODEEDITORPANEL_H
#define QTGMS_CODEEDITORPANEL_H

#include <QWidget>

class CodeEditor;
class CodeFindPanel;
class QToolBar;
class QLabel;
class QTextDocument;
class QHBoxLayout;

// Embeddable editor surface: no resource type, project or file-system knowledge.
class CodeEditorPanel : public QWidget
{
    Q_OBJECT
public:
    explicit CodeEditorPanel(QTextDocument *document, QWidget *parent = nullptr);
    CodeEditor *editor() const;
    QToolBar *toolBar() const;
    void setTextDocument(QTextDocument *document);
    void setHeaderWidget(QWidget *widget);
    void setFooterWidget(QWidget *widget);
    void setLineCommentPrefix(const QString &prefix);
    void setFormatDescription(const QString &description);
    void setCompactStatus();
    void selectRange(int offset, int length);
private:
    void updatePosition();
    CodeEditor *m_editor;
    CodeFindPanel *m_findPanel;
    QToolBar *m_toolBar;
    QLabel *m_positionLabel;
    QLabel *m_formatLabel;
    QLabel *m_modeLabel;
    QLabel *m_signatureLabel;
    QHBoxLayout *m_statusLayout;
    bool m_compactStatus = false;
    QString m_lineCommentPrefix;
};

#endif
