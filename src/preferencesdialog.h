#ifndef QTGMS_PREFERENCESDIALOG_H
#define QTGMS_PREFERENCESDIALOG_H

#include "editordialog.h"

class PreferencesDialog : public EditorDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);
};

#endif
