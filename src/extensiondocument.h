#ifndef QTGMS_EXTENSIONDOCUMENT_H
#define QTGMS_EXTENSIONDOCUMENT_H
#include <QObject>
#include <QDomDocument>
#include <QMap>
#include <QSet>
#include <QUndoStack>

struct ExtensionState {
    QDomDocument xml;
    QMap<QString, QByteArray> files;
};
class ExtensionChangeCommand;
class ExtensionDocument : public QObject
{
    Q_OBJECT
public:
    explicit ExtensionDocument(QObject *parent = nullptr);
    static bool createEmpty(const QString &path, QString &error);
    bool load(const QString &path, QString &error);
    bool save(QString &error);
    void relocate(const QString &oldDirectory, const QString &newDirectory);
    QString filePath() const { return m_path; }
    QString name() const;
    QString contentDirectory() const;
    bool isModified() const { return !m_undo.isClean(); }
    QUndoStack *undoStack() { return &m_undo; }
    ExtensionState state() const;
    void edit(const ExtensionState &state, const QString &description);
    bool fileData(const QString &name, QByteArray &bytes, QString &error);
    bool stageFile(ExtensionState &state, const QString &name, const QByteArray &bytes, QString &error);
    static bool validFileName(const QString &name);
    static int fileKind(const QString &name);
signals:
    void changed();
    void saved();
private:
    friend class ExtensionChangeCommand;
    QString m_path;
    QByteArray m_source;
    ExtensionState m_state;
    QMap<QString, QByteArray> m_originalFiles;
    QMap<QString, QByteArray> m_initialFiles;
    QSet<QString> m_missingFiles;
    QUndoStack m_undo;
};
#endif
