#include "actionlibrary.h"
#include "editortheme.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QtEndian>

// Official GameMaker .lib 500/520 records: little-endian integers and
// length-prefixed ANSI strings. Layout records share IDs and are not actions.
class LibraryStream
{
public:
    explicit LibraryStream(const QByteArray &data) : m_data(data) {}
    QByteArray bytes(int count) {
        if (!m_ok || count < 0 || count > m_data.size() - m_position) { m_ok = false; return QByteArray(); }
        const QByteArray result = m_data.mid(m_position, count); m_position += count; return result;
    }
    int integer() {
        const QByteArray value = bytes(4);
        return value.size() == 4 ? static_cast<qint32>(qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(value.constData()))) : 0;
    }
    int count(int limit) { const int value = integer(); if (value < 0 || value > limit) m_ok = false; return m_ok ? value : 0; }
    QString string() { const int size = count(4 * 1024 * 1024); return QString::fromLocal8Bit(bytes(size)); }
    bool ok() const { return m_ok; }
    bool atEnd() const { return m_ok && m_position == m_data.size(); }
private:
    QByteArray m_data;
    int m_position = 0;
    bool m_ok = true;
};
bool ActionLibraryReader::read(const QString &path, ActionLibrary &library, QString &error, bool loadIcons)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > 32 * 1024 * 1024) { error = QObject::tr("Action library exceeds 32 MB."); return false; }
    LibraryStream stream(file.readAll()); ActionLibrary result; result.filePath = path;
    const int format = stream.integer();
    if (format != 500 && format != 520) { error = QObject::tr("Unsupported .lib version: %1").arg(format); return false; }
    result.caption = stream.string(); result.id = stream.integer(); result.author = stream.string(); result.version = stream.integer();
    stream.bytes(8); result.information = stream.string(); result.initializationCode = stream.string();
    result.advanced = stream.integer() != 0; stream.integer();
    const int count = stream.count(4096);
    for (int i = 0; i < count && stream.ok(); ++i) {
        LibraryAction action; action.libraryId = result.id;
        const int version = stream.integer();
        if (version != 500 && version != 520) { error = QObject::tr("Unsupported action record version: %1").arg(version); return false; }
        action.name = stream.string(); action.id = stream.integer();
        const int imageSize = stream.count(4 * 1024 * 1024); QByteArray imageBytes = stream.bytes(imageSize);
        if (loadIcons && !imageBytes.isEmpty()) {
            QBuffer buffer(&imageBytes); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer, QByteArrayLiteral("BMP"));
            const QSize size = reader.size();
            if (!size.isValid() || size.width() > 1024 || size.height() > 1024) { error = QObject::tr("Invalid action icon dimensions."); return false; }
            QImage image = reader.read().convertToFormat(QImage::Format_ARGB32);
            if (image.isNull()) { error = reader.errorString(); return false; }
            const QRgb key = image.pixel(0, image.height() - 1);
            for (int y = 0; y < image.height(); ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int x = 0; x < image.width(); ++x) if (line[x] == key) line[x] = 0;
            }
            action.icon = QIcon(QPixmap::fromImage(EditorTheme::tintActionImage(image.copy(0, 0, qMin(24, image.width()), qMin(24, image.height())))));
        }
        action.hidden = stream.integer() != 0; action.advanced = stream.integer() != 0;
        if (version == 520) action.registeredOnly = stream.integer() != 0;
        action.description = stream.string(); action.listText = stream.string(); action.hintText = stream.string();
        action.kind = stream.integer(); action.interfaceKind = stream.integer();
        action.question = stream.integer() != 0; action.canApplyTo = stream.integer() != 0; action.allowRelative = stream.integer() != 0;
        const int active = stream.count(64); const int slotCount = stream.count(64);
        if (active > slotCount) { error = QObject::tr("Invalid action argument count."); return false; }
        for (int a = 0; a < slotCount && stream.ok(); ++a) {
            LibraryArgument arg; arg.caption = stream.string(); arg.kind = stream.integer(); arg.defaultValue = stream.string(); arg.menu = stream.string();
            if (a < active) action.arguments.append(arg);
        }
        action.executionType = stream.integer(); action.functionName = stream.string(); action.code = stream.string();
        result.actions.append(action);
    }
    if (!stream.atEnd()) { error = QObject::tr("Truncated or invalid action library record."); return false; }
    library = result; return true;
}
ActionLibraryManager::ActionLibraryManager(QObject *parent) : QObject(parent) { reload(); }
QString ActionLibraryManager::directory() { return QCoreApplication::applicationDirPath() + QStringLiteral("/lib"); }
void ActionLibraryManager::reload()
{
    m_libraries.clear(); m_index.clear(); m_warnings.clear();
    const QDir dir(directory());
    const QStringList files = dir.entryList({QStringLiteral("*.lib")}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QString &file : files) {
        ActionLibrary library; QString error;
        if (!ActionLibraryReader::read(dir.absoluteFilePath(file), library, error)) { m_warnings.append(file + QStringLiteral(": ") + error); continue; }
        const int index = m_libraries.size();
        for (int i = 0; i < library.actions.size(); ++i) {
            LibraryAction &action = library.actions[i]; if (action.kind >= 8) continue;
            const auto key = qMakePair(library.id, action.id);
            if (m_index.contains(key)) {
                action.hidden = true;
                m_warnings.append(tr("%1: duplicate action %2/%3; using the first definition.").arg(file).arg(library.id).arg(action.id));
            } else m_index.insert(key, qMakePair(index, i));
        }
        m_libraries.append(library);
    }
    if (files.isEmpty()) m_warnings.append(tr("No .lib files found in %1").arg(directory()));
    emit changed();
}
const LibraryAction *ActionLibraryManager::action(int libraryId, int actionId) const
{
    const auto it = m_index.constFind(qMakePair(libraryId, actionId));
    return it == m_index.constEnd() ? nullptr : &m_libraries.at(it.value().first).actions.at(it.value().second);
}
