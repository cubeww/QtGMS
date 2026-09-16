#include "textfiledocument.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPlainTextDocumentLayout>
#include <QRegExp>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QTextCodec>
#include <QTextCursor>
#include <QTextDocument>

static bool readTextFile(const QString &path, QByteArray &bytes, QString &text,
                         QByteArray &encoding, bool &bom, QString &newline, QString &error)
{
    error.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    // Keep text, highlighting and undo memory bounded on the 32-bit baseline.
    if (file.size() > 16 * 1024 * 1024) { error = QObject::tr("Code files must be smaller than 16 MiB."); return false; }
    bytes = file.readAll();
    if (file.error() != QFile::NoError) { error = file.errorString(); return false; }
    encoding = QByteArray("UTF-8"); bom = false;
    int skip = 0;
    if (bytes.startsWith(QByteArray::fromHex("efbbbf"))) { bom = true; skip = 3; }
    else if (bytes.startsWith(QByteArray::fromHex("fffe"))) { encoding = QByteArray("UTF-16LE"); bom = true; skip = 2; }
    else if (bytes.startsWith(QByteArray::fromHex("feff"))) { encoding = QByteArray("UTF-16BE"); bom = true; skip = 2; }
    QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
    text = QTextCodec::codecForName(encoding)->toUnicode(bytes.constData() + skip, bytes.size() - skip, &state);
    if (state.invalidChars || state.remainingChars || text.contains(QChar(0))) {
        error = QObject::tr("The file is not valid UTF-8 or BOM-marked UTF-16 text. Convert its encoding before importing it.");
        return false;
    }
    const int firstBreak = text.indexOf(QRegExp(QStringLiteral("[\r\n]")));
    newline = firstBreak < 0 ? QStringLiteral("\r\n")
        : text.at(firstBreak) == QLatin1Char('\n') ? QStringLiteral("\n")
        : text.mid(firstBreak, 2) == QStringLiteral("\r\n") ? QStringLiteral("\r\n") : QStringLiteral("\r");
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return true;
}

TextFileDocument::TextFileDocument(QObject *parent)
    : QObject(parent), m_textDocument(new QTextDocument(this)), m_encoding("UTF-8"),
      m_byteOrderMark(false), m_newline(QStringLiteral("\r\n"))
{
    m_textDocument->setDocumentLayout(new QPlainTextDocumentLayout(m_textDocument));
}
bool TextFileDocument::createEmpty(const QString &filePath, QString &error)
{
    error.clear();
    QTemporaryFile file(QFileInfo(filePath).absoluteDir().filePath(QStringLiteral(".qtgms-code-XXXXXX")));
    if (!file.open()) { error = file.errorString(); return false; }
    file.close();
    if (!file.rename(filePath)) { error = file.errorString(); return false; }
    file.setAutoRemove(false); return true;
}
bool TextFileDocument::load(const QString &filePath, QString &error)
{
    QByteArray bytes, encoding; QString text, newline; bool bom;
    if (!readTextFile(filePath, bytes, text, encoding, bom, newline, error)) return false;
    m_filePath = QFileInfo(filePath).absoluteFilePath(); m_sourceBytes = bytes;
    m_encoding = encoding; m_byteOrderMark = bom; m_newline = newline;
    m_textDocument->setPlainText(text); m_textDocument->setModified(false);
    return true;
}
QByteArray TextFileDocument::encodedText() const
{
    QString text = m_textDocument->toPlainText();
    text.replace(QStringLiteral("\n"), m_newline);
    QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
    QByteArray bytes = QTextCodec::codecForName(m_encoding)->fromUnicode(text.constData(), text.size(), &state);
    if (m_byteOrderMark) {
        const char *hex = m_encoding == "UTF-8" ? "efbbbf" : m_encoding == "UTF-16LE" ? "fffe" : "feff";
        bytes.prepend(QByteArray::fromHex(hex));
    }
    return bytes;
}
static bool writeTextFile(const QString &path, const QByteArray &bytes, QString &error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = file.errorString(); return false;
    }
    return true;
}
bool TextFileDocument::save(QString &error)
{
    error.clear();
    if (!m_textDocument->isModified()) return true;
    QFile source(m_filePath);
    if (!source.open(QIODevice::ReadOnly)) { error = source.errorString(); return false; }
    if (source.readAll() != m_sourceBytes || source.error() != QFile::NoError) {
        error = tr("The code file changed outside the editor. Reopen it before saving, or export your changes to another file.");
        return false;
    }
    source.close();
    const QByteArray bytes = encodedText();
    if (!writeTextFile(m_filePath, bytes, error)) return false;
    m_sourceBytes = bytes; m_textDocument->setModified(false); emit saved(); return true;
}
bool TextFileDocument::importFile(const QString &filePath, QString &error)
{
    QByteArray bytes, encoding; QString text, newline; bool bom;
    if (!readTextFile(filePath, bytes, text, encoding, bom, newline, error)) return false;
    // Import is a single undoable edit, and keeps the destination file's format.
    QTextCursor cursor(m_textDocument); cursor.beginEditBlock();
    cursor.select(QTextCursor::Document); cursor.insertText(text); cursor.endEditBlock();
    return true;
}
bool TextFileDocument::readText(const QString &filePath, QString &text, QString &error)
{
    QByteArray bytes, encoding; QString newline; bool bom;
    return readTextFile(filePath, bytes, text, encoding, bom, newline, error);
}
bool TextFileDocument::exportFile(const QString &filePath, QString &error)
{
    error.clear();
    if (QFileInfo(filePath).absoluteFilePath().compare(m_filePath, Qt::CaseInsensitive) == 0)
        return save(error);
    return writeTextFile(filePath, m_textDocument->isModified() ? encodedText() : m_sourceBytes, error);
}
void TextFileDocument::relocate(const QString &oldDirectory, const QString &newDirectory)
{ m_filePath = QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(m_filePath)); }
QTextDocument *TextFileDocument::textDocument() const { return m_textDocument; }
QString TextFileDocument::filePath() const { return m_filePath; }
QString TextFileDocument::name() const { return QFileInfo(m_filePath).completeBaseName(); }
QString TextFileDocument::formatDescription() const
{
    return QString::fromLatin1(m_encoding) + (m_byteOrderMark ? QStringLiteral(" BOM") : QString())
        + QStringLiteral(" | ") + (m_newline == QStringLiteral("\r\n") ? QStringLiteral("CRLF")
                                  : m_newline == QStringLiteral("\n") ? QStringLiteral("LF") : QStringLiteral("CR"));
}
