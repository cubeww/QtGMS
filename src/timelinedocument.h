#ifndef QTGMS_TIMELINEDOCUMENT_H
#define QTGMS_TIMELINEDOCUMENT_H
#include <QDomDocument>
#include <QObject>
#include <QUndoStack>

enum class MomentOperation { Delete, Shift, Duplicate, Spread, Merge };
class TimelineChangeCommand;
class TimelineDocument : public QObject
{
    Q_OBJECT
public:
    explicit TimelineDocument(QObject *parent = nullptr);
    static bool createEmpty(const QString &path, QString &error);
    bool load(const QString &path, QString &error);
    bool save(QString &error);
    void relocate(const QString &oldDirectory, const QString &newDirectory);
    QString filePath() const { return m_path; }
    QString name() const;
    bool isModified() const { return !m_undo.isClean(); }
    QUndoStack *undoStack() { return &m_undo; }
    QDomDocument xml() const { return m_xml.cloneNode(true).toDocument(); }
    int selectedStep() const { return m_selectedStep; }
    void setSelectedStep(int step) { m_selectedStep = step; }
    void edit(const QDomDocument &xml, const QString &description, int selectedStep);
    bool addMoment(int step, QString &error);
    bool changeMoment(int oldStep, int newStep, QString &error);
    bool transform(MomentOperation operation, int first, int last, double value, QString &error);
    void clear();
    static QList<QDomElement> moments(QDomDocument &xml);
    static QDomElement moment(QDomDocument &xml, int step);
signals:
    void changed();
    void saved();
private:
    friend class TimelineChangeCommand;
    QString m_path;
    QByteArray m_source;
    QDomDocument m_xml;
    QUndoStack m_undo;
    int m_selectedStep = -1;
};
#endif
