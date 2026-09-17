#ifndef QTGMS_PATHPROPERTIESWINDOW_H
#define QTGMS_PATHPROPERTIESWINDOW_H
#include "resourceeditorwindow.h"
#include <QSharedPointer>
class PathDocument;
class PathCanvas;
class Project;
class RoomDocument;
class RoomAssets;
class RoomCanvas;
class QListWidget;
class QDoubleSpinBox;
class QSpinBox;
class QRadioButton;
class QCheckBox;
class QLabel;
class QToolButton;
class QPushButton;
class PathPropertiesWindow : public ResourceEditorWindow
{
    Q_OBJECT
public:
    PathPropertiesWindow(PathDocument *document, const Project *project, const QSharedPointer<RoomAssets> &assets, QWidget *parent = nullptr);
    ~PathPropertiesWindow() override;
    bool save() override;
    QString filePath() const override;
    void relocate(const QString &oldDirectory, const QString &newDirectory) override;
    void updateResources();
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void refresh();
    void refreshSelection();
    void updateStatus();
    void updateTitle();
    void addPoint(bool insert);
    void editPoint();
    void transformPath(const QString &operation);
    bool loadRoomPreview(int roomIndex, QString &error);
    PathDocument *m_document;
    const Project *m_project;
    PathCanvas *m_canvas;
    QListWidget *m_points;
    QDoubleSpinBox *m_x, *m_y, *m_speed;
    QSpinBox *m_precision, *m_snapX, *m_snapY;
    QRadioButton *m_straight, *m_smooth;
    QCheckBox *m_closed;
    QLabel *m_status;
    QToolButton *m_roomButton;
    QPushButton *m_deleteButton;
    QSharedPointer<RoomAssets> m_assets;
    RoomDocument *m_roomDocument = nullptr;
    RoomCanvas *m_roomPreview = nullptr;
    int m_previewIndex = -2;
    QPointF m_cursor;
    bool m_refreshing = false;
};
#endif
