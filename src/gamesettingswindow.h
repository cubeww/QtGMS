#ifndef QTGMS_GAMESETTINGSWINDOW_H
#define QTGMS_GAMESETTINGSWINDOW_H
#include "resourceeditorwindow.h"
class GameSettingsDocument;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QVBoxLayout;
class GameSettingsWindow : public ResourceEditorWindow
{
    Q_OBJECT
public:
    explicit GameSettingsWindow(GameSettingsDocument *document, QWidget *parent = nullptr);
    ~GameSettingsWindow() override;
    bool save() override;
    QString filePath() const override;
    bool isModified() const;
    void relocate(const QString &oldDirectory, const QString &newDirectory) override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    QVBoxLayout *page(QTabWidget *tabs, const QString &title);
    QLineEdit *textField(QFormLayout *form, const QString &title, const QString &key, const QString &fallback = QString());
    QSpinBox *numberField(QFormLayout *form, const QString &title, const QString &key, int fallback, int minimum, int maximum);
    QCheckBox *check(QVBoxLayout *layout, const QString &title, const QString &key, bool fallback = false);
    QComboBox *choice(QFormLayout *form, const QString &title, const QString &key, const QStringList &labels, const QStringList &values, const QString &fallback);
    void colorField(QFormLayout *form, const QString &title, const QString &key, bool delphi);
    void bindImage(QLabel *preview, QPushButton *update, const QString &key, const QString &fileName, const QString &filter, const QSize &requiredSize = QSize());
    void editTextAsset(const QString &title, const QString &key, const QString &fileName);
    QGroupBox *radioGroup(const QString &title, const QString &key, const QStringList &labels, const QStringList &values, const QString &fallback);
    void createGeneral(QTabWidget *tabs);
    void createGroups(QTabWidget *tabs, bool audio);
    void createProjectInfo(QTabWidget *tabs);
    void createWindows(QTabWidget *tabs);
    GameSettingsDocument *m_document;
    bool m_discard = false;
};
#endif
