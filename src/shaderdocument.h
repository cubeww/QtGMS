#ifndef QTGMS_SHADERDOCUMENT_H
#define QTGMS_SHADERDOCUMENT_H

#include <QObject>

class QTextDocument;
class TextFileDocument;

class ShaderDocument : public QObject
{
    Q_OBJECT
public:
    explicit ShaderDocument(QObject *parent = nullptr);
    static bool createEmpty(const QString &filePath, QString &error);
    bool load(const QString &filePath, QString &error);
    bool save(QString &error);
    bool importStage(int stage, const QString &filePath, QString &error);
    bool exportStage(int stage, const QString &filePath, QString &error);
    void relocate(const QString &oldDirectory, const QString &newDirectory);
    QTextDocument *stageDocument(int stage) const;
    bool isModified() const;
    QString filePath() const;
    QString name() const;
    QString formatDescription() const;
signals:
    void modifiedChanged();
private:
    TextFileDocument *m_file;
    QTextDocument *m_stages[2];
};

#endif
