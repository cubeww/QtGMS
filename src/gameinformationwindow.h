#ifndef QTGMS_GAMEINFORMATIONWINDOW_H
#define QTGMS_GAMEINFORMATIONWINDOW_H
#include "resourceeditorwindow.h"
#include "richtextdocument.h"
#include <QMap>
class RichTextEditor;
class QFontComboBox;
class QDoubleSpinBox;
class QAction;
class GameInformationWindow : public ResourceEditorWindow
{
    Q_OBJECT
public:
    explicit GameInformationWindow(QWidget *parent = nullptr);
    ~GameInformationWindow() override;
    bool load(const QString &path, QString &error);
    bool save() override;
    QString filePath() const override { return m_document.filePath(); }
    void relocate(const QString &oldDirectory, const QString &newDirectory) override;
    bool isModified() const;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void refresh();
    void importRtf();
    void exportRtf();
    void chooseFont();
    void chooseTextColor();
    void choosePageColor();
    void print();
    void goToLine();
    RichTextDocument m_document;
    RichTextEditor *m_editor;
    QFontComboBox *m_font;
    QDoubleSpinBox *m_size;
    QMap<QString, QAction *> m_actions;
    bool m_refreshing = false;
};
#endif
