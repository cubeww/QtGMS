#ifndef QTGMS_CODECOMPLETIONITEM_H
#define QTGMS_CODECOMPLETIONITEM_H

#include <QString>

struct CodeCompletionItem
{
    enum class Kind { Keyword, Function, Variable, Constant, Script, Resource };
    QString name;
    QString detail;
    Kind kind = Kind::Variable;
    bool readOnly = false;
    bool obsolete = false;

    bool operator==(const CodeCompletionItem &other) const
    {
        return name == other.name && detail == other.detail && kind == other.kind
            && readOnly == other.readOnly && obsolete == other.obsolete;
    }
};

#endif
