#ifndef QTGMS_TEXTFILEDOCUMENT_H
#define QTGMS_TEXTFILEDOCUMENT_H

#include <QObject>
#include <QByteArray>
#include <QString>

class QTextDocument;

// File persistence only. Editing and undo belong to the shared QTextDocument.
class TextFileDocument : public QObject
{
    Q_OBJECT
public:
    explicit TextFileDocument(QObject *parent = nullptr);
    static bool createEmpty(const QString &filePath, QString &error);
    bool load(const QString &filePath, QString &error);
    bool save(QString &error);
    bool importFile(const QString &filePath, QString &error);
    bool exportFile(const QString &filePath, QString &error);
    static bool readText(const QString &filePath, QString &text, QString &error);
    void relocate(const QString &oldDirectory, const QString &newDirectory);
    QTextDocument *textDocument() const;
    QString filePath() const;
    QString name() const;
    QString formatDescription() const;
signals:
    void saved();
private:
    QByteArray encodedText() const;
    QTextDocument *m_textDocument;
    QString m_filePath;
    QByteArray m_sourceBytes;
    QByteArray m_encoding;
    bool m_byteOrderMark;
    QString m_newline;
};

#endif
