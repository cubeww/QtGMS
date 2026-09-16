#ifndef QTGMS_FONTRANGEDIALOG_H
#define QTGMS_FONTRANGEDIALOG_H

#include "fontdocument.h"
#include "editordialog.h"
#include <functional>

class QPlainTextEdit;
class QSpinBox;
class FontRangeDialog : public EditorDialog
{
    Q_OBJECT
public:
    explicit FontRangeDialog(const std::function<bool(QString &, QString &)> &codeCharacters, QWidget *parent = nullptr);
    QVector<FontRange> ranges() const { return m_ranges; }
private:
    void updateCharacters();
    void acceptRanges();
    void loadCharacters();
    void loadCodeCharacters();
    std::function<bool(QString &, QString &)> m_codeCharacters;
    QSpinBox *m_first;
    QSpinBox *m_last;
    QPlainTextEdit *m_characters;
    bool m_fromText = false;
    QVector<FontRange> m_ranges;
};

#endif
