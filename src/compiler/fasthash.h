#ifndef QTGMS_FASTHASH_H
#define QTGMS_FASTHASH_H

#include <QByteArray>

// Canonical 128-bit XXH3 digest. Context separates resource kinds and settings.
QByteArray fastHash(const QByteArray &bytes, const QByteArray &context = QByteArray());

#endif
