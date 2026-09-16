#ifndef QTGMS_CODECOMPLETION_H
#define QTGMS_CODECOMPLETION_H

#include "codecompletionitem.h"
#include <QObject>
#include <QVector>

class CodeEditor;
class QCompleter;
class QStandardItemModel;
class QTimer;
class QKeyEvent;
class QModelIndex;

class CodeCompletion : public QObject
{
    Q_OBJECT
public:
    explicit CodeCompletion(CodeEditor *editor);
    void setItems(const QVector<CodeCompletionItem> &items);
    void configure(bool automatic, int delay);
    void request();
    bool handleKeyPress(QKeyEvent *event);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    bool token(QString &query, int &start, int &end) const;
    void updatePopup(bool manual);
    void accept(const QModelIndex &index);
    void dismiss();
    CodeEditor *m_editor;
    QCompleter *m_completer;
    QStandardItemModel *m_model;
    QTimer *m_timer;
    QVector<CodeCompletionItem> m_items;
    QString m_query;
    int m_start = -1;
    bool m_automatic = true;
};

#endif
