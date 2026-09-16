#ifndef QTGMS_PATHDOCUMENT_H
#define QTGMS_PATHDOCUMENT_H
#include <QObject>
#include <QDomDocument>
#include <QPointF>
#include <QVector>
#include <QUndoStack>
struct PathPoint {
    QPointF position;
    double speed = 100;
    QDomElement metadata;
};
struct PathState {
    QVector<PathPoint> points;
    bool smooth = false, closed = true;
    int precision = 4, snapX = 16, snapY = 16;
    int backgroundRoom = -1;
};
class PathChangeCommand;
class PathDocument : public QObject
{
    Q_OBJECT
public:
    explicit PathDocument(QObject *parent = nullptr);
    static bool createEmpty(const QString &path, QString &error);
    bool load(const QString &path, QString &error);
    bool save(QString &error);
    QString filePath() const { return m_path; }
    QString name() const;
    void relocate(const QString &oldDirectory, const QString &newDirectory);
    const PathState &state() const { return m_state; }
    void edit(const PathState &state, const QString &description, int selected);
    int selectedPoint() const { return m_selected; }
    void selectPoint(int index);
    bool isModified() const { return !m_undo.isClean(); }
    QUndoStack *undoStack() { return &m_undo; }
    static bool validate(const PathState &state, QString &error);
signals:
    void changed();
    void selectionChanged();
    void saved();
private:
    friend class PathChangeCommand;
    PathState m_state;
    QDomDocument m_xml;
    QByteArray m_source;
    QString m_path;
    int m_selected = -1;
    QUndoStack m_undo;
};
#endif
