#ifndef QTGMS_EDITORTHEME_H
#define QTGMS_EDITORTHEME_H

class QApplication;
class QImage;

class EditorTheme
{
public:
    static void apply(QApplication &application);
    static QImage tintActionImage(const QImage &image);
};

#endif
