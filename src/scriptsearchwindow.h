#ifndef QTGMS_SCRIPTSEARCHWINDOW_H
#define QTGMS_SCRIPTSEARCHWINDOW_H

#include "editordialog.h"
#include "editorwindow.h"
#include "scriptsearch.h"
class QLineEdit;
class QCheckBox;
class QListWidget;
class QTableWidget;

class ScriptSearchDialog : public EditorDialog
{
    Q_OBJECT
public:
    ScriptSearchDialog(const ScriptSearchOptions &options, QWidget *parent);
    ScriptSearchOptions options() const;
private:
    QLineEdit *m_searchEdit;
    QCheckBox *m_caseCheck, *m_commentsCheck, *m_wordCheck;
    QListWidget *m_scopeList, *m_filterList;
};

class ScriptSearchWindow : public EditorWindow
{
    Q_OBJECT
public:
    ScriptSearchWindow(const QString &query, const QVector<ScriptSearchMatch> &matches,
                       const QStringList &warnings, QWidget *parent);
signals:
    void matchActivated(const ScriptSearchMatch &match);
private:
    QString reportHtml() const;
    void saveReport();
    void printReport();
    QVector<ScriptSearchMatch> m_matches;
    QTableWidget *m_table;
};

#endif
