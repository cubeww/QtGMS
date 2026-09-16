#include <QSignalBlocker>
#include "editordialog.h"
#include "editorstandarddialogs.h"
#include "fontpropertieswindow.h"
#include "fontdocument.h"
#include "fontatlas.h"
#include "fontrangedialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QVBoxLayout>

FontPropertiesWindow::FontPropertiesWindow(FontDocument *document, const QStringList &textureGroups, QWidget *parent)
    : ResourceEditorWindow(parent), m_document(document), m_family(new QComboBox), m_antiAlias(new QComboBox), m_textureGroup(new QComboBox),
      m_size(new QSpinBox), m_bold(new QCheckBox(tr("Bold"))), m_italic(new QCheckBox(tr("Italic"))),
      m_highQuality(new QCheckBox(tr("High Quality"))), m_includeTTF(new QCheckBox(tr("Include in Asset Package"))),
      m_ranges(new QListWidget), m_sample(new QPlainTextEdit), m_characters(new QPlainTextEdit), m_fontStatus(new QLabel)
{
    document->setParent(this); setAttribute(Qt::WA_DeleteOnClose); editorMenuBar()->hide();
    resize(836, 377); setMinimumSize(836, 377);
    auto *body = new QWidget(this); auto *layout = new QGridLayout(body);
    layout->setContentsMargins(6, 8, 6, 6); layout->setSpacing(6);
    auto *settings = new QWidget; settings->setFixedWidth(220);
    auto *settingsLayout = new QVBoxLayout(settings); settingsLayout->setContentsMargins(0, 0, 0, 0);
    auto *names = new QGridLayout; auto *name = new QLineEdit(document->name()); enableResourceRenaming(name, ResourceType::Font);
    names->addWidget(new QLabel(tr("Name")), 0, 0, Qt::AlignRight); names->addWidget(name, 0, 1);
    names->addWidget(new QLabel(tr("Font")), 1, 0, Qt::AlignRight); names->addWidget(m_family, 1, 1);
    m_family->addItems(QFontDatabase().families());
    if (m_family->findText(document->state().family) < 0) m_family->addItem(document->state().family);
    settingsLayout->addLayout(names); m_fontStatus->setWordWrap(true); settingsLayout->addWidget(m_fontStatus); settingsLayout->addStretch();
    auto *style = new QGroupBox(tr("Style")); auto *styleLayout = new QGridLayout(style);
    styleLayout->setContentsMargins(7, 17, 7, 8);
    m_antiAlias->addItems({tr("off"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")});
    m_size->setRange(1, 499); m_size->setButtonSymbols(QAbstractSpinBox::NoButtons); m_size->setKeyboardTracking(false);
    styleLayout->addWidget(new QLabel(tr("Anti-Aliasing")), 0, 0); styleLayout->addWidget(m_antiAlias, 0, 1, 1, 2);
    styleLayout->addWidget(new QLabel(tr("Size")), 1, 0, Qt::AlignRight); styleLayout->addWidget(m_size, 1, 1, 1, 2);
    styleLayout->addWidget(m_highQuality, 2, 0); styleLayout->addWidget(m_bold, 2, 1); styleLayout->addWidget(m_italic, 2, 2);
    settingsLayout->addWidget(style); settingsLayout->addStretch(); settingsLayout->addWidget(m_includeTTF, 0, Qt::AlignRight);
    m_includeTTF->setToolTip(tr("Saved as a font resource option. Asset package export is not available yet."));
    auto *texture = new QPushButton(tr("Show Texture")); settingsLayout->addWidget(texture); settingsLayout->addStretch();
    auto *textureLayout = new QHBoxLayout; textureLayout->addWidget(new QLabel(tr("Texture Group"))); textureLayout->addWidget(m_textureGroup);
    settingsLayout->addLayout(textureLayout);
    for (int i = 0; i < textureGroups.size(); ++i) m_textureGroup->addItem(textureGroups.at(i), i);
    if (m_textureGroup->findData(document->state().textureGroup) < 0)
        m_textureGroup->addItem(QString::number(document->state().textureGroup), document->state().textureGroup);
    layout->addWidget(settings, 0, 0, 2, 1);
    m_sample->setPlainText(QStringLiteral("Hello World!!")); m_sample->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_characters->setReadOnly(true); m_characters->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(m_sample, 0, 1, 1, 2); layout->addWidget(m_ranges, 1, 1); layout->addWidget(m_characters, 1, 2);
    m_ranges->setFixedWidth(169); layout->setColumnStretch(2, 1); layout->setRowStretch(0, 1); layout->setRowStretch(1, 1);
    auto *buttons = new QHBoxLayout; auto *add = new QPushButton(QStringLiteral("+")); auto *remove = new QPushButton(QStringLiteral("-"));
    auto *clear = new QPushButton(tr("Clear all")); add->setFixedWidth(25); remove->setFixedWidth(25);
    add->setToolTip(tr("Add a font range")); remove->setToolTip(tr("Remove selected font range"));
    buttons->addWidget(add); buttons->addWidget(remove); buttons->addStretch(); buttons->addWidget(clear); layout->addLayout(buttons, 2, 1);
    auto *ok = new QPushButton(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("OK")); ok->setFixedSize(113, 25);
    layout->addWidget(ok, 2, 2, Qt::AlignRight); setCentralWidget(body);
    connect(ok, &QPushButton::clicked, this, [this] { if (save()) close(); });
    connect(texture, &QPushButton::clicked, this, &FontPropertiesWindow::showTexture);
    connect(add, &QPushButton::clicked, this, &FontPropertiesWindow::addRange);
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = m_ranges->currentRow(); FontState state = m_document->state();
        if (row >= 0 && row < state.ranges.size()) { state.ranges.remove(row); m_document->edit(state, tr("Remove font range")); }
    });
    connect(clear, &QPushButton::clicked, this, [this] { FontState state = m_document->state(); state.ranges.clear(); m_document->edit(state, tr("Clear font ranges")); });
    connect(m_ranges, &QListWidget::currentRowChanged, this, [this, remove](int row) { remove->setEnabled(row >= 0); if (!m_refreshing) updateCharacters(); });
    for (QComboBox *combo : {m_family, m_antiAlias, m_textureGroup})
        connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this] { applySettings(); });
    for (QCheckBox *check : {m_bold, m_italic, m_highQuality, m_includeTTF}) connect(check, &QCheckBox::toggled, this, [this] { applySettings(); });
    connect(m_size, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this] { applySettings(); });
    connect(document, &FontDocument::changed, this, &FontPropertiesWindow::refresh);
    connect(document->undoStack(), &QUndoStack::cleanChanged, this, [this] { refresh(); });
    connect(document, &FontDocument::saved, this, [this] { emit resourceSaved(ResourceType::Font, filePath(), QString()); });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this); connect(saveShortcut, &QShortcut::activated, this, &FontPropertiesWindow::saveProjectRequested);
    auto *undo = new QShortcut(QKeySequence::Undo, this); auto *redo = new QShortcut(QKeySequence::Redo, this);
    connect(undo, &QShortcut::activated, this, [this] {
        if (m_sample->hasFocus()) m_sample->undo(); else m_document->undoStack()->undo();
    });
    connect(redo, &QShortcut::activated, this, [this] {
        if (m_sample->hasFocus()) m_sample->redo(); else m_document->undoStack()->redo();
    });
    refresh();
}
void FontPropertiesWindow::refresh()
{
    m_refreshing = true; const FontState &state = m_document->state();
    setWindowTitle(tr("Font Properties: %1%2").arg(m_document->name(), m_document->isModified() ? QStringLiteral(" *") : QString()));
    m_family->setCurrentIndex(m_family->findText(state.family)); m_size->setValue(state.size); m_antiAlias->setCurrentIndex(state.antiAlias);
    m_bold->setChecked(state.bold); m_italic->setChecked(state.italic); m_highQuality->setChecked(state.highQuality);
    m_includeTTF->setChecked(state.includeTTF); m_textureGroup->setCurrentIndex(m_textureGroup->findData(state.textureGroup));
    const int row = m_ranges->currentRow(); m_ranges->clear();
    for (const FontRange &range : state.ranges) m_ranges->addItem(tr("%1 to %2").arg(range.first).arg(range.last));
    if (m_ranges->count()) m_ranges->setCurrentRow(qBound(0, row, m_ranges->count() - 1));
    const bool available = FontAtlas::isFamilyAvailable(state.family);
    m_fontStatus->setText(available ? QString() : tr("Font not installed. Preview uses a substitute."));
    m_sample->setFont(FontAtlas::font(state)); m_characters->setFont(FontAtlas::font(state));
    m_refreshing = false; updateCharacters();
}
void FontPropertiesWindow::applySettings()
{
    if (m_refreshing) return;
    FontState state = m_document->state(); state.family = m_family->currentText(); state.size = m_size->value();
    state.antiAlias = m_antiAlias->currentIndex(); state.bold = m_bold->isChecked(); state.italic = m_italic->isChecked();
    state.highQuality = m_highQuality->isChecked(); state.includeTTF = m_includeTTF->isChecked(); state.textureGroup = m_textureGroup->currentData().toInt();
    m_document->edit(state, tr("Change font properties"));
}
void FontPropertiesWindow::updateCharacters()
{
    QString text; const int row = m_ranges->currentRow(); const auto &ranges = m_document->state().ranges;
    if (row >= 0 && row < ranges.size()) {
        for (int code = ranges.at(row).first; code <= ranges.at(row).last && text.size() < 4096; ++code)
            if (QChar(static_cast<ushort>(code)).isPrint()) text.append(QChar(static_cast<ushort>(code)));
    }
    m_characters->setPlainText(text); m_characters->setToolTip(tr("Selected range preview (up to 4096 printable characters)."));
}
void FontPropertiesWindow::addRange()
{
    FontRangeDialog dialog(this); if (dialog.exec() != QDialog::Accepted) return;
    FontState state = m_document->state();
    for (const FontRange &range : dialog.ranges()) if (!state.ranges.contains(range)) state.ranges.append(range);
    m_document->edit(state, tr("Add font ranges"));
}
void FontPropertiesWindow::showTexture()
{
    applySettings(); FontAtlasData atlas; QString error; QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool success = FontAtlas::build(m_document->state(), atlas, error); QApplication::restoreOverrideCursor();
    if (!success) { EditorMessageBox::critical(this, tr("Cannot Build Font Texture"), error); return; }
    EditorDialog dialog(this); dialog.setWindowTitle(tr("Font Texture: %1 x %2, %3 glyphs").arg(atlas.image.width()).arg(atlas.image.height()).arg(atlas.glyphs.size()));
    auto *layout = new QVBoxLayout(dialog.bodyWidget()); auto *scroll = new QScrollArea; auto *image = new QLabel;
    image->setPixmap(QPixmap::fromImage(atlas.image)); image->setFixedSize(atlas.image.size()); image->setStyleSheet(QStringLiteral("background: #383838;")); scroll->setWidget(image); layout->addWidget(scroll);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(buttons); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(qBound(320, atlas.image.width() + 40, 900), qBound(220, atlas.image.height() + 80, 700)); dialog.exec();
}
QString FontPropertiesWindow::filePath() const { return m_document->filePath(); }
void FontPropertiesWindow::relocate(const QString &oldDirectory, const QString &newDirectory) { m_document->relocate(oldDirectory, newDirectory); }
bool FontPropertiesWindow::save()
{
    m_size->interpretText(); applySettings(); QString error; QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool success = m_document->save(error); QApplication::restoreOverrideCursor();
    if (!success) EditorMessageBox::critical(this, tr("Cannot Save Font"), error);
    return success;
}
void FontPropertiesWindow::closeEvent(QCloseEvent *event)
{
    m_size->interpretText(); applySettings();
    if (m_document->isModified()) {
        const auto answer = EditorMessageBox::question(this, tr("Font Properties"), tr("Save changes to %1?").arg(m_document->name()),
            EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel || (answer == EditorMessageBox::Save && !save())) { event->ignore(); return; }
    }
    event->accept();
}

void FontPropertiesWindow::setTextureGroups(const QStringList &groups)
{
    const QSignalBlocker blocker(m_textureGroup); const int group = m_textureGroup->currentData().toInt();
    m_textureGroup->clear();
    for (int i = 0; i < groups.size(); ++i) m_textureGroup->addItem(groups.at(i), i);
    if (m_textureGroup->findData(group) < 0) m_textureGroup->addItem(QString::number(group), group);
    m_textureGroup->setCurrentIndex(m_textureGroup->findData(group));
}
