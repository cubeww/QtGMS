#include "codesnippets.h"

const QVector<CodeSnippet> &CodeSnippets::items()
{
    static const QVector<CodeSnippet> Snippets = {
        {QStringLiteral("block"), QStringLiteral("{\n\t|statement|;\n}")},
        {QStringLiteral("if"), QStringLiteral("if (|condition|)\n{\n\t\n}")},
        {QStringLiteral("if-else"), QStringLiteral("if (|condition|)\n{\n\t\n}\nelse\n{\n\t\n}")},
        {QStringLiteral("for"), QStringLiteral("for (|initialisation|; condition; increment)\n{\n\t\n}")},
        {QStringLiteral("for i"), QStringLiteral("for (var i = 0; i < |count|; ++i)\n{\n\t\n}")},
        {QStringLiteral("while"), QStringLiteral("while (|condition|)\n{\n\t\n}")},
        {QStringLiteral("do"), QStringLiteral("do\n{\n\t|statement|;\n}\nuntil (condition);")},
        {QStringLiteral("repeat"), QStringLiteral("repeat (|count|)\n{\n\t\n}")},
        {QStringLiteral("switch"), QStringLiteral("switch (|expression|)\n{\n\tcase value:\n\t\tbreak;\n\tdefault:\n\t\tbreak;\n}")},
        {QStringLiteral("with"), QStringLiteral("with (|object|)\n{\n\t\n}")},
        {QStringLiteral("comment"), QStringLiteral("// |text|")},
        {QStringLiteral("multiline comment"), QStringLiteral("/*\n\t|text|\n*/")}
    };
    return Snippets;
}
