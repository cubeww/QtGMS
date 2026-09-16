#ifndef QTGMS_COMPILERCACHE_H
#define QTGMS_COMPILERCACHE_H
#include <QByteArray>
#include <QString>
#include <functional>
class CompileProfile;

QByteArray cachedCompilerAsset(const QString &directory, const QByteArray &kind, const QByteArray &input,
    const std::function<QByteArray()> &produce, CompileProfile &profile);
#endif
