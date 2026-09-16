#ifndef QTGMS_SOUNDPROPERTIESWINDOW_H
#define QTGMS_SOUNDPROPERTIESWINDOW_H
#include "resourceeditorwindow.h"

class AudioPreview;
class SoundDocument;
class QButtonGroup;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

class SoundPropertiesWindow : public ResourceEditorWindow
{
    Q_OBJECT
public:
    SoundPropertiesWindow(SoundDocument *document, const QStringList &audioGroups, QWidget *parent = nullptr);
    void setAudioGroups(const QStringList &groups);
    bool save() override;
    QString filePath() const override;
    void relocate(const QString &oldDirectory, const QString &newDirectory) override;
protected:
    void closeEvent(QCloseEvent *event) override;
    bool event(QEvent *event) override;
private:
    void refresh();
    void applySettings();
    void loadAudio();
    SoundDocument *m_document;
    AudioPreview *m_preview;
    QLabel *m_fileName;
    QButtonGroup *m_attributes;
    QSlider *m_volume;
    QSlider *m_quality;
    QComboBox *m_channels;
    QComboBox *m_sampleRate;
    QComboBox *m_bitRate;
    QComboBox *m_bitDepth;
    QComboBox *m_audioGroup;
    QPushButton *m_playButton;
    QPushButton *m_stopButton;
    QByteArray m_previewAudio;
    bool m_refreshing = false;
};
#endif
