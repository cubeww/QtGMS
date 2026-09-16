#ifndef QTGMS_EDITORSTANDARDDIALOGS_H
#define QTGMS_EDITORSTANDARDDIALOGS_H
#include <QColorDialog>
#include <QInputDialog>
#include <QMessageBox>

class EditorInputDialog : public QInputDialog
{
public:
    explicit EditorInputDialog(QWidget *parent = nullptr);
    static int getInt(QWidget *parent, const QString &title, const QString &label,
                      int value, int minimum, int maximum, int step, bool *accepted);
};
class EditorColorDialog : public QColorDialog
{
public:
    explicit EditorColorDialog(QWidget *parent = nullptr);
    static QColor getColor(const QColor &initial, QWidget *parent = nullptr,
                           const QString &title = QString(), ColorDialogOptions options = ColorDialogOptions());
};
class EditorMessageBox : public QMessageBox
{
public:
    EditorMessageBox(Icon icon, const QString &title, const QString &text, StandardButtons buttons, QWidget *parent);
    static StandardButton information(QWidget *parent, const QString &title, const QString &text,
                                     StandardButtons buttons = Ok, StandardButton defaultButton = NoButton);
    static StandardButton warning(QWidget *parent, const QString &title, const QString &text,
                                 StandardButtons buttons = Ok, StandardButton defaultButton = NoButton);
    static StandardButton critical(QWidget *parent, const QString &title, const QString &text,
                                  StandardButtons buttons = Ok, StandardButton defaultButton = NoButton);
    static StandardButton question(QWidget *parent, const QString &title, const QString &text,
                                  StandardButtons buttons = StandardButtons(Yes | No), StandardButton defaultButton = NoButton);
    static void about(QWidget *parent, const QString &title, const QString &text);
private:
    static StandardButton showMessage(QWidget *parent, Icon icon, const QString &title, const QString &text,
                                      StandardButtons buttons, StandardButton defaultButton);
};
#endif
