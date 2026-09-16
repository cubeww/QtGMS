#ifndef QTGMS_CODEEDITORPANEL_H
#define QTGMS_CODEEDITORPANEL_H

#include <QWidget>

class CodeEditor;
class CodeFindPanel;
class QToolBar;
class QLabel;
class QTextDocument;
class QHBoxLayout;
class QTabBar;

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
    void setCodeTab(const QString &name);
    QTabBar *codeTabs() const { return m_codeTabs; }
    void selectRange(int offset, int length);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void setCompactStatus();
    void updateFontDescription();
    void updatePosition();
    CodeEditor *m_editor;
    CodeFindPanel *m_findPanel;
    QToolBar *m_toolBar;
    QLabel *m_positionLabel;
    QLabel *m_formatLabel;
    QLabel *m_modeLabel;
    QLabel *m_signatureLabel;
    QWidget *m_statusWidget;
    QHBoxLayout *m_statusLayout;
    QTabBar *m_codeTabs = nullptr;
    bool m_compactStatus = false;
    QString m_lineCommentPrefix;
};

#endif
