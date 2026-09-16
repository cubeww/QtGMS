#ifndef QTGMS_OBJECTDOCUMENT_H
#define QTGMS_OBJECTDOCUMENT_H

#include "project.h"
#include <QDomDocument>
#include <QObject>
#include <QUndoStack>

struct LibraryAction;
class ActionLibraryManager;
class ObjectChangeCommand;
// XML snapshots preserve unrecognized resource, event and action metadata.
class ObjectDocument : public QObject
{
    Q_OBJECT
public:
    explicit ObjectDocument(QObject *parent = nullptr);
    static bool createEmpty(const QString &path, QString &error);
    bool load(const QString &path, QString &error);
    bool save(const Project &project, QString &error);
    void relocate(const QString &oldDirectory, const QString &newDirectory);
    QString filePath() const { return m_path; }
    QString name() const;
    bool isModified() const { return !m_undo.isClean(); }
    QUndoStack *undoStack() { return &m_undo; }
    QDomDocument xml() const { return m_xml.cloneNode(true).toDocument(); }
    void edit(const QDomDocument &xml, const QString &description);
signals:
    void changed();
    void saved();
private:
    friend class ObjectChangeCommand;
    QString m_path;
    QByteArray m_source;
    QDomDocument m_xml;
    QUndoStack m_undo;
};
#endif
