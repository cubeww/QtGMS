#include "editordialog.h"
#include "dialogframe.h"
#include <QVBoxLayout>

EditorDialog::EditorDialog(QWidget *parent)
    : QDialog(parent), m_body(new QWidget(this))
{
    new DialogFrame(this);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    layout->addWidget(m_body);
}
QWidget *EditorDialog::bodyWidget() const { return m_body; }
