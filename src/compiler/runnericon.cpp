#include "runnericon.h"
#include <QDir>
#include <QFile>
#include <QObject>
#include <QtEndian>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

bool RunnerIcon::replace(const QString &executablePath, const QString &iconPath, QString &error)
{
    QFile iconFile(iconPath);
    if (!iconFile.open(QIODevice::ReadOnly)) {
        error = QObject::tr("Cannot read game icon %1: %2").arg(iconPath, iconFile.errorString());
        return false;
    }
    // The settings editor uses the same size limit for imported assets.
    if (iconFile.size() < 6 || iconFile.size() > 32 * 1024 * 1024) {
        error = QObject::tr("Invalid ICO file: %1").arg(iconPath);
        return false;
    }
    QByteArray icon = iconFile.readAll();
    if (iconFile.error() != QFile::NoError) {
        error = iconFile.errorString();
        return false;
    }
    const auto *bytes = reinterpret_cast<const uchar *>(icon.constData());
    const int count = icon.size() >= 6 ? qFromLittleEndian<quint16>(bytes + 4) : 0;
    if (!count || qFromLittleEndian<quint16>(bytes) != 0
        || qFromLittleEndian<quint16>(bytes + 2) != 1 || icon.size() < 6 + count * 16) {
        error = QObject::tr("Invalid ICO file: %1").arg(iconPath);
        return false;
    }
    QByteArray group = icon.left(6);
    for (int i = 0; i < count; ++i) {
        const int entry = 6 + i * 16;
        const quint32 size = qFromLittleEndian<quint32>(bytes + entry + 8);
        const quint32 offset = qFromLittleEndian<quint32>(bytes + entry + 12);
        if (!size || offset < quint32(6 + count * 16) || quint64(offset) + size > quint64(icon.size())) {
            error = QObject::tr("Invalid ICO image entry in %1.").arg(iconPath);
            return false;
        }
        // ICO entries store an image offset; group resources store an RT_ICON ID.
        group.append(icon.constData() + entry, 12);
        uchar id[2];
        qToLittleEndian<quint16>(quint16(i + 1), id);
        group.append(reinterpret_cast<const char *>(id), 2);
    }

    // The bundled GMS 1.4 Runner loads group 152, with en-GB icon resources.
    const WORD IconGroupId = 152;
    const WORD IconLanguage = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_UK);
    const QString nativePath = QDir::toNativeSeparators(executablePath);
    HANDLE update = BeginUpdateResourceW(reinterpret_cast<LPCWSTR>(nativePath.utf16()), FALSE);
    if (!update) {
        error = QObject::tr("Cannot update game icon (Windows error %1).").arg(GetLastError());
        return false;
    }
    bool updated = true;
    for (int i = 0; i < count && updated; ++i) {
        const int entry = 6 + i * 16;
        const quint32 size = qFromLittleEndian<quint32>(bytes + entry + 8);
        const quint32 offset = qFromLittleEndian<quint32>(bytes + entry + 12);
        updated = UpdateResourceW(update, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(i + 1),
            IconLanguage, icon.data() + offset, size);
    }
    if (updated)
        updated = UpdateResourceW(update, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(IconGroupId),
            IconLanguage, group.data(), DWORD(group.size()));
    if (!updated) {
        const DWORD code = GetLastError();
        EndUpdateResourceW(update, TRUE);
        error = QObject::tr("Cannot update game icon (Windows error %1).").arg(code);
        return false;
    }
    if (!EndUpdateResourceW(update, FALSE)) {
        error = QObject::tr("Cannot save game icon (Windows error %1).").arg(GetLastError());
        return false;
    }
    return true;
}
