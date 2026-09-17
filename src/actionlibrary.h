#ifndef QTGMS_ACTIONLIBRARY_H
#define QTGMS_ACTIONLIBRARY_H

#include <QObject>
#include <QIcon>
#include <QMap>
#include <QVector>

struct LibraryArgument {
    QString caption, defaultValue, menu;
    int kind = 0;
};
struct LibraryAction {
    int libraryId = 0, id = 0, kind = 0, interfaceKind = 0, executionType = 0;
    QString name, description, listText, hintText, functionName, code;
    bool hidden = false, advanced = false, registeredOnly = false;
    bool question = false, canApplyTo = false, allowRelative = false;
    QIcon icon;
    QVector<LibraryArgument> arguments;
};
struct ActionLibrary {
    int id = 0, version = 0;
    QString caption, author, information, initializationCode, filePath;
    bool advanced = false;
    QVector<LibraryAction> actions;
};
class ActionLibraryReader
{
public:
    static bool read(const QString &path, ActionLibrary &library, QString &error, bool loadIcons = true);
};
class ActionLibraryManager : public QObject
{
    Q_OBJECT
public:
    explicit ActionLibraryManager(QObject *parent = nullptr);
    void reload();
    static QString directory();
    const QVector<ActionLibrary> &libraries() const { return m_libraries; }
    const QStringList &warnings() const { return m_warnings; }
    const LibraryAction *action(int libraryId, int actionId) const;
signals:
    void changed();
private:
    QVector<ActionLibrary> m_libraries;
    QMap<QPair<int, int>, QPair<int, int>> m_index;
    QStringList m_warnings;
};
#endif
