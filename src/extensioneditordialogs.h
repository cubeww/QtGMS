#ifndef QTGMS_EXTENSIONEDITORDIALOGS_H
#define QTGMS_EXTENSIONEDITORDIALOGS_H
#include <QDomElement>
class QWidget;
class ExtensionDocument;
struct ExtensionState;
class ExtensionEditorDialogs
{
public:
    static bool package(QDomElement root, QWidget *parent);
    static bool file(QDomElement file, ExtensionDocument *document, ExtensionState &state, QWidget *parent);
    static bool function(QDomElement function, int fileKind, QWidget *parent);
    static bool constant(QDomElement constant, QWidget *parent);
};
#endif
