#include "resourceeditorwindow.h"
#include <QLineEdit>
void ResourceEditorWindow::enableResourceRenaming(QLineEdit *edit, ResourceType type)
{
    const QString original = edit->text(); edit->setReadOnly(false); edit->setObjectName(QStringLiteral("resourceNameEdit")); edit->setToolTip(tr("Rename this resource (code references are not changed)"));
    connect(edit, &QLineEdit::editingFinished, this, [this, edit, original, type] {
        const QString name = edit->text().trimmed();
        if (name == original) return;
        // The owner commits the rename and reconstructs affected editor documents.
        edit->setText(original); emit renameResourceRequested(type, filePath(), name);
    });
}
