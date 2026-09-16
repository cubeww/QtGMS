#ifndef QTGMS_EDITORDIALOG_H
#define QTGMS_EDITORDIALOG_H

#include <QDialog>

// Modal editor forms share the same skin and logical-coordinate caption dragging.
class EditorDialog : public QDialog
{
public:
    explicit EditorDialog(QWidget *parent = nullptr);
    QWidget *bodyWidget() const;
private:
    QWidget *m_body;
};

#endif
