#ifndef QTGMS_MACROEDITORWINDOW_H
#define QTGMS_MACROEDITORWINDOW_H
#include "resourceeditorwindow.h"
class MacroDocument;
class QTableWidget;
class QPushButton;
class MacroEditorWindow : public ResourceEditorWindow
{
    Q_OBJECT
public:
    MacroEditorWindow(MacroDocument *document, QWidget *parent = nullptr);
    ~MacroEditorWindow() override;
    bool save() override;
    QString filePath() const override;
    void relocate(const QString &oldDirectory, const QString &newDirectory) override;
    bool isModified() const;
    void selectMacro(const QString &name, int column);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void commitEditing();
    void refresh();
    void updateActions();
    void addMacro(bool insert);
    void deleteMacro();
    void clearMacros();
    void moveMacro(int offset);
    void sortMacros();
    void importMacros();
    void exportMacros();
    MacroDocument *m_document;
    QTableWidget *m_table;
    QPushButton *m_deleteButton, *m_upButton, *m_downButton, *m_clearButton, *m_sortButton;
    bool m_discard = false;
};
#endif
