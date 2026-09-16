#ifndef QTGMS_RESOURCEEDITORWINDOW_H
#define QTGMS_RESOURCEEDITORWINDOW_H

#include "editorwindow.h"
#include "project.h"

class QLineEdit;

class ResourceEditorWindow : public EditorWindow
{
    Q_OBJECT
public:
    explicit ResourceEditorWindow(QWidget *parent = nullptr) : EditorWindow(parent) {}
    virtual bool save() = 0;
    virtual QString filePath() const = 0;
    virtual void relocate(const QString &oldDirectory, const QString &newDirectory) = 0;
protected:
    void enableResourceRenaming(QLineEdit *edit, ResourceType type);
signals:
    void renameResourceRequested(ResourceType type, const QString &filePath, const QString &name);
    void saveProjectRequested();
    void resourceSaved(ResourceType type, const QString &filePath, const QString &thumbnailPath);
};

#endif
