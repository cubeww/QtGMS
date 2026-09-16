#include "configurationmanager.h"
#include "editorstandarddialogs.h"
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegExp>
#include <QVBoxLayout>

ConfigurationManager::ConfigurationManager(const QList<ProjectConfiguration> &configurations, int currentIndex,
    const std::function<bool(const QList<ConfigurationEdit> &)> &apply, QWidget *parent)
    : EditorDialog(parent), m_list(new QListWidget), m_deleteButton(new QPushButton(tr("Del"))),
      m_renameButton(new QPushButton(tr("Rename"))), m_upButton(new QPushButton(QStringLiteral("+"))), m_downButton(new QPushButton(QStringLiteral("-")))
{
    setWindowTitle(tr("Manage Configurations")); resize(315, 385); setMinimumSize(300, 270);
    for (int i = 0; i < configurations.size(); ++i) { ConfigurationEdit edit; edit.name = configurations.at(i).name; edit.sourceIndex = i; m_edits.append(edit); }
    auto *layout = new QGridLayout(bodyWidget()); layout->setContentsMargins(15, 15, 8, 12); layout->setHorizontalSpacing(7); layout->setVerticalSpacing(8);
    layout->addWidget(m_list, 0, 0, 1, 3); layout->setRowStretch(0, 1);
    auto *order = new QVBoxLayout; m_upButton->setFixedSize(36, 38); m_downButton->setFixedSize(36, 38); order->addWidget(m_upButton); order->addSpacing(32); order->addWidget(m_downButton); order->addStretch(); layout->addLayout(order, 0, 3);
    auto *addButton = new QPushButton(tr("Add")); addButton->setFixedSize(45, 23); m_deleteButton->setFixedSize(45, 23); m_renameButton->setFixedSize(58, 23);
    layout->addWidget(addButton, 1, 0, Qt::AlignLeft); layout->addWidget(m_deleteButton, 1, 1, Qt::AlignHCenter); layout->addWidget(m_renameButton, 1, 2, Qt::AlignRight);
    auto *ok = new QPushButton(tr("OK")); ok->setFixedSize(75, 24); layout->setRowMinimumHeight(2, 6); layout->addWidget(ok, 3, 0, 1, 3, Qt::AlignHCenter);
    m_upButton->setToolTip(tr("Move selected configuration up")); m_downButton->setToolTip(tr("Move selected configuration down"));
    connect(m_list, &QListWidget::currentRowChanged, this, [this] { updateButtons(); });
    connect(addButton, &QPushButton::clicked, this, [this] { add(); }); connect(m_deleteButton, &QPushButton::clicked, this, [this] { remove(); }); connect(m_renameButton, &QPushButton::clicked, this, [this] { rename(); });
    connect(m_upButton, &QPushButton::clicked, this, [this] { move(-1); }); connect(m_downButton, &QPushButton::clicked, this, [this] { move(1); });
    connect(ok, &QPushButton::clicked, this, [this, apply, configurations] {
        bool changed = m_edits.size() != configurations.size();
        for (int i = 0; i < m_edits.size() && !changed; ++i) changed = m_edits.at(i).added || m_edits.at(i).sourceIndex != i || m_edits.at(i).name != configurations.at(i).name;
        if (!changed || apply(m_edits)) accept();
    });
    refresh(qBound(0, currentIndex, m_edits.size() - 1));
}
QString ConfigurationManager::selectedName() const { const int index = m_list->currentRow(); return index >= 0 ? m_edits.at(index).name : QString(); }
void ConfigurationManager::refresh(int selected) { m_list->clear(); for (const auto &edit : m_edits) m_list->addItem(edit.name); m_list->setCurrentRow(selected); updateButtons(); }
void ConfigurationManager::updateButtons()
{
    const int row = m_list->currentRow(); m_deleteButton->setEnabled(row > 0); m_renameButton->setEnabled(row > 0); m_upButton->setEnabled(row > 1); m_downButton->setEnabled(row > 0 && row + 1 < m_edits.size());
}
bool ConfigurationManager::validName(const QString &name, int except)
{
    bool valid = QRegExp(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*")).exactMatch(name)
        && !QRegExp(QStringLiteral("(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])"), Qt::CaseInsensitive).exactMatch(name);
    for (int i = 0; i < m_edits.size(); ++i) if (i != except && m_edits.at(i).name.compare(name, Qt::CaseInsensitive) == 0) valid = false;
    if (!valid) EditorMessageBox::warning(this, tr("Configuration Name"), tr("Use a unique name containing letters, digits and underscores, starting with a letter or underscore. Windows reserved filenames are not allowed."));
    return valid;
}
void ConfigurationManager::add()
{
    EditorDialog dialog(this); dialog.setWindowTitle(tr("Create Configuration")); dialog.resize(365, 200);
    auto *layout = new QVBoxLayout(dialog.bodyWidget()); layout->setContentsMargins(15, 20, 15, 12); auto *form = new QFormLayout; form->setVerticalSpacing(24); layout->addLayout(form);
    auto *name = new QLineEdit; auto *source = new QComboBox; for (const auto &edit : m_edits) source->addItem(edit.name); source->setCurrentIndex(qMax(0, m_list->currentRow()));
    int suffix = 1; QString suggested; do { suggested = QStringLiteral("Config%1").arg(suffix++); } while (source->findText(suggested) >= 0); name->setText(suggested); name->selectAll();
    form->addRow(tr("New Configuration Name:"), name); form->addRow(tr("Create Config From:"), source); layout->addStretch();
    auto *buttons = new QHBoxLayout; auto *cancel = new QPushButton(tr("Cancel")); auto *ok = new QPushButton(tr("OK")); buttons->addStretch(); buttons->addWidget(cancel); buttons->addWidget(ok); layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject); connect(ok, &QPushButton::clicked, &dialog, [&] { if (validName(name->text().trimmed())) dialog.accept(); });
    if (dialog.exec() != QDialog::Accepted) return;
    ConfigurationEdit edit = m_edits.at(source->currentIndex()); edit.added = true; edit.name = name->text().trimmed(); m_edits.append(edit); refresh(m_edits.size() - 1);
}
void ConfigurationManager::rename()
{
    const int row = m_list->currentRow(); if (row <= 0) return;
    EditorInputDialog dialog(this); dialog.setWindowTitle(tr("Rename Configuration")); dialog.setLabelText(tr("Name:")); dialog.setTextValue(m_edits.at(row).name);
    if (dialog.exec() == QDialog::Accepted && validName(dialog.textValue().trimmed(), row)) { m_edits[row].name = dialog.textValue().trimmed(); refresh(row); }
}
void ConfigurationManager::remove()
{
    const int row = m_list->currentRow(); if (row <= 0) return;
    if (EditorMessageBox::question(this, tr("Delete Configuration"), tr("Delete configuration %1?").arg(m_edits.at(row).name), EditorMessageBox::Yes | EditorMessageBox::No, EditorMessageBox::No) != EditorMessageBox::Yes) return;
    m_edits.removeAt(row); refresh(qMin(row, m_edits.size() - 1));
}
void ConfigurationManager::move(int delta)
{
    const int row = m_list->currentRow(), target = row + delta; if (row <= 0 || target <= 0 || target >= m_edits.size()) return;
    m_edits.move(row, target); refresh(target);
}
