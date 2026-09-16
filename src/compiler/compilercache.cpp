#include "compilercache.h"
#include "compileprofile.h"
#include "fasthash.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>

QByteArray cachedCompilerAsset(const QString &directory, const QByteArray &kind, const QByteArray &input,
    const std::function<QByteArray()> &produce, CompileProfile &profile)
{
    profile.count(QStringLiteral("Cache hits"), 0);
    profile.count(QStringLiteral("Cache misses (including invalid entries)"), 0);
    QByteArray key;
    {
        CompileDetailScope timing(profile, QStringLiteral("Source XXH3-128 / cache key"));
        key = fastHash(input, kind).toHex();
    }
    const QString path = QDir(directory).filePath(QString::fromLatin1(key));
    QFile cached(path);
    QByteArray digest, bytes;
    bool opened, readable;
    {
        CompileDetailScope timing(profile, QStringLiteral("Cache file lookup / read"));
        opened = cached.open(QIODevice::ReadOnly);
        if (opened) {
            digest = cached.read(16);
            bytes = cached.readAll();
        }
        readable = opened && cached.error() == QFile::NoError;
        cached.close();
    }
    if (opened) {
        bool valid = false;
        {
            CompileDetailScope timing(profile, QStringLiteral("Cached data XXH3-128 verification"));
            valid = readable && digest.size() == 16 && fastHash(bytes) == digest;
        }
        if (valid) {
            profile.count(QStringLiteral("Cache hits"));
            return bytes;
        }
        profile.count(QStringLiteral("Invalid cache entries"));
    }
    profile.count(QStringLiteral("Cache misses (including invalid entries)"));
    bytes.clear();
    digest.clear();
    QByteArray result;
    {
        CompileDetailScope timing(profile, QStringLiteral("Conversion / encoding on cache miss"));
        result = produce();
    }
    QByteArray outputDigest;
    {
        CompileDetailScope timing(profile, QStringLiteral("Encoded data XXH3-128"));
        outputDigest = fastHash(result);
    }
    CompileDetailScope timing(profile, QStringLiteral("Cache file writing"));
    bool saved = false;
    if (QDir().mkpath(directory)) {
        QSaveFile output(path);
        if (output.open(QIODevice::WriteOnly)) {
            if (output.write(outputDigest) == outputDigest.size() && output.write(result) == result.size())
                saved = output.commit();
        }
    }
    if (!saved)
        profile.count(QStringLiteral("Cache write failures"));
    return result;
}
