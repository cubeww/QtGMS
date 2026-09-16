#ifndef QTGMS_SHADERCOMPILER_H
#define QTGMS_SHADERCOMPILER_H
#include <QStringList>

struct CompiledShader
{
    QString vertex, fragment;
    QStringList attributes;
};
CompiledShader compileShader(const QString &name, const QString &vertex, const QString &fragment);
#endif
