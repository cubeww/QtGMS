#ifndef QTGMS_TIMELINEPROPERTIESWINDOW_H
#define QTGMS_TIMELINEPROPERTIESWINDOW_H
#include "resourceeditorwindow.h"
#include "timelinedocument.h"
class ActionLibraryManager;
class ActionEditorPanel;
class QListWidget;
class QPushButton;
class TimelinePropertiesWindow : public ResourceEditorWindow
{
    Q_OBJECT
public:
    TimelinePropertiesWindow(TimelineDocument *document, Project *project, ActionLibraryManager *libraries, QWidget *parent = nullptr);
    bool save() override;
    void showAction(int step, int actionIndex, int offset, int length, int argument);
    QString filePath() const override;
    void relocate(const QString &oldDirectory, const QString &newDirectory) override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void refresh();
    void selectMoment();
    void editMoment(bool change);
    void transformMoments(MomentOperation operation);
    void clearMoments();
    void showInformation();
    TimelineDocument *m_document;
    ActionLibraryManager *m_libraries;
    QListWidget *m_moments;
    ActionEditorPanel *m_actionEditor;
    QPushButton *m_changeButton;
    QList<QPushButton *> m_rangeButtons;
    bool m_refreshing = false;
};
#endif
