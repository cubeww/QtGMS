#ifndef QTGMS_ACTIONPALETTE_H
#define QTGMS_ACTIONPALETTE_H
#include <QWidget>
#include <QListWidget>
class ActionLibraryManager;
class QTabWidget;
class ActionPalette : public QWidget
{
    Q_OBJECT
public:
    explicit ActionPalette(ActionLibraryManager *libraries, QWidget *parent = nullptr);
    void setHasActionContainer(bool available);
signals:
    void actionRequested(int libraryId, int actionId);
private:
    void rebuild();
    ActionLibraryManager *m_libraries;
    QTabWidget *m_tabs;
};
class ActionList : public QListWidget
{
    Q_OBJECT
public:
    explicit ActionList(QWidget *parent = nullptr);
signals:
    void actionDropped(int libraryId, int actionId, int row);
    void actionsMoved(const QList<int> &rows, int before);
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};
#endif
