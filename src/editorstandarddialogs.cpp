#include "editorstandarddialogs.h"
#include "dialogframe.h"
#include <QIcon>

EditorInputDialog::EditorInputDialog(QWidget *parent) : QInputDialog(parent) { new DialogFrame(this); }
int EditorInputDialog::getInt(QWidget *parent, const QString &title, const QString &label,
                              int value, int minimum, int maximum, int step, bool *accepted)
{
    EditorInputDialog dialog(parent); dialog.setWindowTitle(title); dialog.setLabelText(label);
    dialog.setInputMode(QInputDialog::IntInput); dialog.setIntRange(minimum, maximum);
    dialog.setIntStep(step); dialog.setIntValue(value);
    const bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted) *accepted = ok;
    return ok ? dialog.intValue() : value;
}
EditorColorDialog::EditorColorDialog(QWidget *parent) : QColorDialog(parent)
{ setOption(QColorDialog::DontUseNativeDialog); new DialogFrame(this); }
QColor EditorColorDialog::getColor(const QColor &initial, QWidget *parent, const QString &title, ColorDialogOptions options)
{
    EditorColorDialog dialog(parent); dialog.setOptions(options | QColorDialog::DontUseNativeDialog);
    dialog.setCurrentColor(initial); if (!title.isEmpty()) dialog.setWindowTitle(title);
    return dialog.exec() == QDialog::Accepted ? dialog.selectedColor() : QColor();
}
EditorMessageBox::EditorMessageBox(Icon icon, const QString &title, const QString &text, StandardButtons buttons, QWidget *parent)
    : QMessageBox(icon, title, text, buttons, parent)
{ new DialogFrame(this); }
EditorMessageBox::StandardButton EditorMessageBox::showMessage(QWidget *parent, Icon icon, const QString &title, const QString &text,
                                                               StandardButtons buttons, StandardButton defaultButton)
{
    EditorMessageBox dialog(icon, title, text, buttons, parent);
    if (defaultButton != NoButton) dialog.setDefaultButton(defaultButton);
    return static_cast<StandardButton>(dialog.exec());
}
EditorMessageBox::StandardButton EditorMessageBox::information(QWidget *parent, const QString &title, const QString &text,
                                                               StandardButtons buttons, StandardButton defaultButton)
{ return showMessage(parent, Information, title, text, buttons, defaultButton); }
EditorMessageBox::StandardButton EditorMessageBox::warning(QWidget *parent, const QString &title, const QString &text,
                                                           StandardButtons buttons, StandardButton defaultButton)
{ return showMessage(parent, Warning, title, text, buttons, defaultButton); }
EditorMessageBox::StandardButton EditorMessageBox::critical(QWidget *parent, const QString &title, const QString &text,
                                                            StandardButtons buttons, StandardButton defaultButton)
{ return showMessage(parent, Critical, title, text, buttons, defaultButton); }
EditorMessageBox::StandardButton EditorMessageBox::question(QWidget *parent, const QString &title, const QString &text,
                                                            StandardButtons buttons, StandardButton defaultButton)
{ return showMessage(parent, Question, title, text, buttons, defaultButton); }
void EditorMessageBox::about(QWidget *parent, const QString &title, const QString &text)
{
    EditorMessageBox dialog(NoIcon, title, text, Ok, parent);
    dialog.setIconPixmap(dialog.windowIcon().pixmap(32, 32)); dialog.exec();
}
