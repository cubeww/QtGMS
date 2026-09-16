#ifndef QTGMS_GMLCOMPILER_H
#define QTGMS_GMLCOMPILER_H

#include "datawriter.h"
#include <QSet>
#include <QSharedPointer>
struct GmlNode;

struct VmReference
{
    QString name;
    int offset = 0;
    int scope = -1;
    bool function = false;
};
struct VmCode
{
    QString name;
    QString source;
    DataWriter bytecode;
    QVector<VmReference> references;
    QStringList locals;
    QHash<QString, int> localIds;
    int address = 0;
    QSharedPointer<GmlNode> syntax;
};
struct GmlEnvironment
{
    QHash<QString, double> constants;
    QHash<QString, QString> macros;
    QSet<QString> functions;
    QHash<QString, QString> extensionFunctionStubs;
    QSet<QString> builtInVariables;
    QSet<QString> builtInGlobalVariables;
    QSet<QString> readOnlyVariables;
    QSet<QString> globalVariables;
    QHash<QString, int> functionArguments;
    QHash<QString, quint64> functionClassifications;
    bool shortCircuit = true;
};

void compileGml(VmCode &code, DataWriter &file, const GmlEnvironment &environment);
void prepareGml(QVector<VmCode> &code, GmlEnvironment &environment);
void writeVmChunks(DataWriter &file, QVector<VmCode> &code, const GmlEnvironment &environment);

#endif
