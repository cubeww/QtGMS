#ifndef QTGMS_RICHTEXTDOCUMENT_H
#define QTGMS_RICHTEXTDOCUMENT_H
#include <QString>
#include <QByteArray>
class RichTextDocument
{
public:
    static QByteArray emptyRtf();
    bool load(const QString &path, QString &error);
    bool save(const QByteArray &rtf, QString &error);
    static bool readFile(const QString &path, QByteArray &bytes, QString &error);
    static bool writeFile(const QString &path, const QByteArray &bytes, QString &error);
    QString filePath() const { return m_path; }
    const QByteArray &bytes() const { return m_source; }
    void relocate(const QString &oldDirectory, const QString &newDirectory);
private:
    QString m_path;
    QByteArray m_source;
};
#endif
