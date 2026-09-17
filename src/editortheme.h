#ifndef QTGMS_EDITORTHEME_H
#define QTGMS_EDITORTHEME_H

class QApplication;
class QImage;
class QProgressBar;
class QWidget;

class EditorTheme
{
public:
    static void apply(QApplication &application);
    static void applyProgressDialogStyle(QWidget *body, QProgressBar *progressBar);
    static QImage tintActionImage(const QImage &image);
};

#endif
