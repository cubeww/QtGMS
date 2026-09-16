#ifndef QTGMS_BACKGROUNDPROPERTIESWINDOW_H
#define QTGMS_BACKGROUNDPROPERTIESWINDOW_H

#include "resourceeditorwindow.h"
#include <QPointer>

class BackgroundDocument;
class BackgroundPreview;
class ImageEditorWindow;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QSpinBox;

class BackgroundPropertiesWindow : public ResourceEditorWindow
{
    Q_OBJECT
public:
    BackgroundPropertiesWindow(BackgroundDocument *document, const QStringList &textureGroups, QWidget *parent = nullptr);
    void setTextureGroups(const QStringList &groups);
    bool save() override;
    QString filePath() const override;
    void relocate(const QString &oldDirectory, const QString &newDirectory) override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void refresh();
    void applySettings();
    void loadImage();
    void editImage();
    BackgroundDocument *m_document;
    BackgroundPreview *m_preview;
    QLabel *m_dimensions;
    QCheckBox *m_tileSetCheck;
    QCheckBox *m_horizontalCheck;
    QCheckBox *m_verticalCheck;
    QCheckBox *m_for3DCheck;
    QComboBox *m_textureGroup;
    QGroupBox *m_tileProperties;
    QList<QSpinBox *> m_tileValues;
    QPointer<ImageEditorWindow> m_imageEditor;
    bool m_refreshing = false;
};

#endif
