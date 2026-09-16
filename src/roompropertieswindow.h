#ifndef QTGMS_ROOMPROPERTIESWINDOW_H
#define QTGMS_ROOMPROPERTIESWINDOW_H
#include "resourceeditorwindow.h"
#include "roomassets.h"
#include <QDomDocument>
#include <QSet>
#include <functional>
class RoomDocument;
class RoomCanvas;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QComboBox;
class ResourceComboBox;
class QLabel;
class QStackedWidget;
class RoomTilePicker;
enum class RoomPropertyScope { Room, Background, View, Entity };
class RoomPropertiesWindow : public ResourceEditorWindow
{
    Q_OBJECT
public:
    RoomPropertiesWindow(RoomDocument *document, Project *project, QWidget *parent = nullptr);
    ~RoomPropertiesWindow() override;
    bool save() override;
    QString filePath() const override;
    void relocate(const QString &oldDirectory, const QString &newDirectory) override;
    void updateResources();
    void selectPlacementObject(const QString &name);
    void showCode(const QString &id, int offset, int length);
signals:
    void openResourceRequested(ResourceType type, const QString &path);
protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    QString value(RoomPropertyScope scope, const QString &key) const;
    void setValue(RoomPropertyScope scope, const QString &key, const QString &value);
    QWidget *createObjectsPage();
    QWidget *createSettingsPage(QAction *orderAction);
    QWidget *createTilesPage();
    QWidget *createBackgroundsPage();
    QWidget *createViewsPage();
    QWidget *createPhysicsPage();
    QDoubleSpinBox *numberControl(QWidget *parent, RoomPropertyScope scope, const QString &key, double minimum, double maximum, int decimals = 0);
    QCheckBox *checkControl(QWidget *parent, const QString &label, RoomPropertyScope scope, const QString &key);
    QWidget *resourceControl(QWidget *parent, RoomPropertyScope scope, const QString &key, ResourceType type);
    QPushButton *colorControl(QWidget *parent, RoomPropertyScope scope, const QString &key, bool alpha);
    QWidget *resourcePicker(QWidget *parent, QComboBox *combo, int fieldWidth);
    void refreshTileLayers();
    void editTileLayer(bool add);
    void deleteTileLayer();
    ResourceComboBox *resourceCombo(ResourceType type);
    void refresh();
    void refreshSelection();
    void refreshObjectPreview();
    void editCode(const QString &id);
    void changeLock(bool locked);
    RoomDocument *m_document;
    Project *m_project;
    RoomAssets m_assets;
    RoomCanvas *m_canvas;
    QLabel *m_status, *m_preview;
    RoomTilePicker *m_tilePicker;
    ResourceComboBox *m_objectChoice, *m_tileChoice;
    QComboBox *m_tileLayerChoice = nullptr;
    QSet<int> m_emptyTileLayers;
    QStackedWidget *m_pages;
    QList<std::function<void()>> m_refreshers;
    QList<QPair<ResourceComboBox *, ResourceType>> m_resourceCombos;
    int m_backgroundIndex = 0, m_viewIndex = 0;
    QRect m_tileSource = QRect(0, 0, 16, 16);
    int m_tileDepth = 1000000;
    QString m_selectedId;
    bool m_refreshing = false;
    QDomDocument m_propertySettings;
    int m_selectionCount = 0;
};
#endif
