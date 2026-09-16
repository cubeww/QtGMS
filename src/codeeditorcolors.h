#ifndef QTGMS_CODEEDITORCOLORS_H
#define QTGMS_CODEEDITORCOLORS_H

#include <QColor>

// GameMaker Studio's GMGreen palette, converted from Delphi BGR to RGB.
class CodeEditorColors
{
public:
    static const QColor Text;
    static const QColor Keyword;
    static const QColor Value;
    static const QColor Comment;
    static const QColor Constant;
    static const QColor BuiltinVariable;
    static const QColor Function;
    static const QColor Resource;
    static const QColor Background;
    static const QColor CurrentLine;
    static const QColor Selection;
    static const QColor LineNumber;
    static const QColor LineNumberBackground;
    static const QColor SelectedLineNumber;
    static const QColor IndentGuide;
    static const QColor ActiveIndentGuide;
};

#endif
