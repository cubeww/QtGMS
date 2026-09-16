#ifndef QTGMS_EDITORLANGUAGE_H
#define QTGMS_EDITORLANGUAGE_H

#include <QString>

class QApplication;

class EditorLanguage
{
public:
    static QString selectedLanguage();
    static void setSelectedLanguage(const QString &language);
    static void install(QApplication &application);
};

#endif
