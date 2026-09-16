#ifndef QTGMS_MACRODOCUMENT_H
#define QTGMS_MACRODOCUMENT_H
#include <QObject>
#include <QDomDocument>
#include <QUndoStack>
class Project;
class MacroChangeCommand;
class MacroDocument : public QObject
{
    Q_OBJECT
public:
    explicit MacroDocument(Project *project, QObject *parent = nullptr);
    bool load(const QString &scopePath, QString &error);
    bool save(QString &error);
    QString filePath() const { return m_path; }
    QString scopeName() const;
    bool isGlobal() const { return m_global; }
    bool isModified() const { return !m_undo.isClean(); }
    void relocate(const QString &oldDirectory, const QString &newDirectory);
    QUndoStack *undoStack() { return &m_undo; }
    QDomDocument xml() const { return m_xml.cloneNode(true).toDocument(); }
    void edit(const QDomDocument &xml, const QString &description);
    static bool validate(const QDomDocument &xml, QString &error);
signals:
    void changed();
    void saved();
private:
    friend class MacroChangeCommand;
    Project *m_project;
    QString m_path;
    bool m_global = false;
    QDomDocument m_xml, m_saved;
    QUndoStack m_undo;
};
#endif
