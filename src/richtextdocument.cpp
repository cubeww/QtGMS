#include "richtextdocument.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QObject>
QByteArray RichTextDocument::emptyRtf() { return QByteArrayLiteral("{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fnil Arial;}}{\\colortbl ;\\red221\\green221\\blue221;}\\viewkind4\\uc1\\pard\\cf1\\f0\\fs20\\par}\n"); }
bool RichTextDocument::readFile(const QString &path, QByteArray &bytes, QString &error)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > 64 * 1024 * 1024) { error = QObject::tr("The RTF file exceeds the editor's 64 MiB limit."); return false; }
    bytes = file.readAll(); if (file.error() != QFile::NoError) { error = file.errorString(); return false; }
    if (!bytes.trimmed().startsWith("{\\rtf")) { error = QObject::tr("Expected a Rich Text Format (RTF) document."); return false; } return true;
}
bool RichTextDocument::writeFile(const QString &path, const QByteArray &bytes, QString &error)
{
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { error = file.errorString(); return false; } return true;
}
bool RichTextDocument::load(const QString &path, QString &error) { QByteArray bytes; if (!readFile(path, bytes, error)) return false; m_source = bytes; m_path = QFileInfo(path).absoluteFilePath(); return true; }
bool RichTextDocument::save(const QByteArray &rtf, QString &error)
{
    QFile file(m_path); if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.readAll() != m_source || file.error() != QFile::NoError) { error = QObject::tr("Game Information changed outside the editor. Reopen it before saving."); return false; } file.close();
    if (!writeFile(m_path, rtf, error)) return false; m_source = rtf; return true;
}
void RichTextDocument::relocate(const QString &oldDirectory, const QString &newDirectory) { m_path = QDir(newDirectory).absoluteFilePath(QDir(oldDirectory).relativeFilePath(m_path)); }
