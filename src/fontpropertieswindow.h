#ifndef QTGMS_FONTPROPERTIESWINDOW_H
#define QTGMS_FONTPROPERTIESWINDOW_H

#include "resourceeditorwindow.h"

class FontDocument;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;

class FontPropertiesWindow : public ResourceEditorWindow
{
    Q_OBJECT
public:
    explicit FontPropertiesWindow(FontDocument *document, const QStringList &textureGroups, QWidget *parent = nullptr);
    void setTextureGroups(const QStringList &groups);
    bool save() override;
    QString filePath() const override;
    void relocate(const QString &oldDirectory, const QString &newDirectory) override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void refresh();
    void applySettings();
    void updateCharacters();
    void addRange();
    void showTexture();
    FontDocument *m_document;
    QComboBox *m_family;
    QComboBox *m_antiAlias;
    QComboBox *m_textureGroup;
    QSpinBox *m_size;
    QCheckBox *m_bold;
    QCheckBox *m_italic;
    QCheckBox *m_highQuality;
    QCheckBox *m_includeTTF;
    QListWidget *m_ranges;
    QPlainTextEdit *m_sample;
    QPlainTextEdit *m_characters;
    QLabel *m_fontStatus;
    bool m_refreshing = false;
};

#endif
