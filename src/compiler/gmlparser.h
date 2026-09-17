#ifndef QTGMS_GMLPARSER_H
#define QTGMS_GMLPARSER_H
#include "gmlcompiler.h"
#include <QSharedPointer>

enum class GmlNodeKind {
    Block,
    Number,
    String,
    Name,
    Member,
    Index,
    ArrayLiteral,
    Call,
    Conditional,
    Binary,
    Unary,
    Postfix,
    Declaration,
    Assign,
    Enum,
    If,
    While,
    Repeat,
    With,
    Do,
    For,
    Switch,
    Case,
    Default,
    Break,
    Continue,
    Exit,
    Return,
};

const char *gmlNodeKindName(GmlNodeKind kind);

struct GmlNode;
typedef QSharedPointer<GmlNode> GmlNodePtr;
struct GmlNode
{
    GmlNodeKind kind = GmlNodeKind::Block;
    QString text;
    int line = 1;
    bool isIntegerConstant = false;
    bool isBooleanConstant = false;
    QVector<GmlNodePtr> children;
};
GmlNodePtr parseGml(const QString &source, const QString &name, const GmlEnvironment &environment);
#endif
