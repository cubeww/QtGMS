#include "runnerversion.h"
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QStringList>
#include <QtEndian>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

static QByteArray versionText(const QString &text)
{
    QByteArray bytes((text.size() + 1) * 2, '\0');
    for (int index = 0; index < text.size(); ++index)
        qToLittleEndian<quint16>(text.at(index).unicode(), reinterpret_cast<uchar *>(bytes.data()) + index * 2);
    return bytes;
}

static void alignVersionData(QByteArray &bytes)
{
    while (bytes.size() % 4) bytes.append('\0');
}

// VERSIONINFO uses byte lengths for binary values and UTF-16 character counts
// for strings. Every value and child starts on a DWORD boundary.
static QByteArray versionBlock(const QString &key, bool text, const QByteArray &value, const QByteArray &children = QByteArray())
{
    QByteArray bytes(6, '\0');
    bytes.append(versionText(key));
    alignVersionData(bytes);
    bytes.append(value);
    if (!children.isEmpty()) { alignVersionData(bytes); bytes.append(children); }
    if (bytes.size() > 65535) throw QObject::tr("Game version information exceeds the Windows resource size limit.");
    auto *header = reinterpret_cast<uchar *>(bytes.data());
    qToLittleEndian<quint16>(quint16(bytes.size()), header);
    qToLittleEndian<quint16>(quint16(text ? value.size() / 2 : value.size()), header + 2);
    qToLittleEndian<quint16>(text ? 1 : 0, header + 4);
    return bytes;
}

bool RunnerVersion::replace(const QString &executablePath, const QMap<QString, QString> &options, QString &error)
{
    // Match the language of the bundled Runner's RT_VERSION/1 resource, so
    // Windows cannot select its original placeholder metadata instead.
    const WORD VersionLanguage = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_UK);
    const WORD UnicodeCodePage = 1200;
    QByteArray resource;
    try {
        const QStringList components = {QStringLiteral("major"), QStringLiteral("mainor"), QStringLiteral("release"), QStringLiteral("build")};
        quint32 version[4];
        QStringList numbers;
        for (int index = 0; index < 4; ++index) {
            bool valid = false;
            version[index] = options.value(QStringLiteral("option_windows_%1_version").arg(components.at(index)),
                index == 0 ? QStringLiteral("1") : QStringLiteral("0")).toUInt(&valid);
            if (!valid || version[index] > 65535) throw QObject::tr("Invalid Windows game version: each component must be between 0 and 65535.");
            numbers.append(QString::number(version[index]));
        }
        const quint32 high = (version[0] << 16) | version[1];
        const quint32 low = (version[2] << 16) | version[3];
        const quint32 fields[] = {VS_FFI_SIGNATURE, VS_FFI_STRUCVERSION, high, low, high, low,
            VS_FFI_FILEFLAGSMASK, 0, VOS_NT_WINDOWS32, VFT_APP, 0, 0, 0};
        QByteArray fixed(sizeof(fields), '\0');
        for (int index = 0; index < 13; ++index)
            qToLittleEndian<quint32>(fields[index], reinterpret_cast<uchar *>(fixed.data()) + index * 4);

        QByteArray strings;
        const auto addString = [&](const QString &key, const QString &value) {
            alignVersionData(strings);
            strings.append(versionBlock(key, true, versionText(value)));
        };
        addString(QStringLiteral("CompanyName"), options.value(QStringLiteral("option_windows_company_info")));
        addString(QStringLiteral("ProductName"), options.value(QStringLiteral("option_windows_product_info")));
        addString(QStringLiteral("LegalCopyright"), options.value(QStringLiteral("option_windows_copyright_info")));
        addString(QStringLiteral("FileDescription"), options.value(QStringLiteral("option_windows_description_info")));
        addString(QStringLiteral("FileVersion"), numbers.join(QLatin1Char('.')));
        addString(QStringLiteral("ProductVersion"), numbers.join(QLatin1Char('.')));
        addString(QStringLiteral("InternalName"), QFileInfo(executablePath).completeBaseName());
        addString(QStringLiteral("OriginalFilename"), QFileInfo(executablePath).fileName());

        const QByteArray table = versionBlock(QStringLiteral("080904b0"), true, QByteArray(), strings);
        QByteArray children = versionBlock(QStringLiteral("StringFileInfo"), true, QByteArray(), table);
        QByteArray translation(4, '\0');
        qToLittleEndian<quint16>(VersionLanguage, reinterpret_cast<uchar *>(translation.data()));
        qToLittleEndian<quint16>(UnicodeCodePage, reinterpret_cast<uchar *>(translation.data()) + 2);
        alignVersionData(children);
        children.append(versionBlock(QStringLiteral("VarFileInfo"), true, QByteArray(),
            versionBlock(QStringLiteral("Translation"), false, translation)));
        resource = versionBlock(QStringLiteral("VS_VERSION_INFO"), false, fixed, children);
    } catch (const QString &message) {
        error = message;
        return false;
    }

    const QString nativePath = QDir::toNativeSeparators(executablePath);
    HANDLE update = BeginUpdateResourceW(reinterpret_cast<LPCWSTR>(nativePath.utf16()), FALSE);
    if (!update) {
        error = QObject::tr("Cannot update game version information (Windows error %1).").arg(GetLastError());
        return false;
    }
    if (!UpdateResourceW(update, MAKEINTRESOURCEW(16), MAKEINTRESOURCEW(1), VersionLanguage,
            resource.data(), DWORD(resource.size()))) {
        const DWORD code = GetLastError();
        EndUpdateResourceW(update, TRUE);
        error = QObject::tr("Cannot update game version information (Windows error %1).").arg(code);
        return false;
    }
    if (!EndUpdateResourceW(update, FALSE)) {
        error = QObject::tr("Cannot save game version information (Windows error %1).").arg(GetLastError());
        return false;
    }
    return true;
}
