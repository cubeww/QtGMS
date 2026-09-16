#include "gameinformationwindow.h"
#include "richtexteditor.h"
#include "editorstandarddialogs.h"
#include "dialogframe.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDialog>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <functional>

GameInformationWindow::GameInformationWindow(QWidget *parent) : ResourceEditorWindow(parent), m_editor(new RichTextEditor(this)), m_font(new QFontComboBox), m_size(new QDoubleSpinBox)
{
    setAttribute(Qt::WA_DeleteOnClose); setObjectName(QStringLiteral("gameInformation")); setWindowTitle(tr("Game Information")); resize(1000, 700); setMinimumSize(740, 400);
    auto *body = new QWidget; auto *layout = new QVBoxLayout(body); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0); setCentralWidget(body);
    auto *toolbar = new QToolBar; toolbar->setMovable(false); toolbar->setFixedHeight(28); toolbar->setIconSize(QSize(16, 16)); layout->addWidget(toolbar); layout->addWidget(m_editor, 1);
    m_editor->setObjectName(QStringLiteral("informationText")); m_editor->setStyleSheet(QStringLiteral("RichTextEditor#informationText { border: 1px solid #a5a59b; }"));
    auto *fileMenu = editorMenuBar()->addMenu(tr("&File")); auto *editMenu = editorMenuBar()->addMenu(tr("&Edit")); auto *formatMenu = editorMenuBar()->addMenu(tr("F&ormat"));
    auto add = [this](QMenu *menu, const QString &key, const QString &title, const QString &icon, const QKeySequence &shortcut, const std::function<void()> &callback) {
        auto *action = new QAction(title, this); if (!icon.isEmpty()) action->setIcon(QIcon(icon)); action->setShortcut(shortcut); menu->addAction(action); m_actions.insert(key, action); connect(action, &QAction::triggered, this, callback); return action;
    };
    add(fileMenu, QStringLiteral("load"), tr("&Load from a file..."), QStringLiteral(":/images/open.png"), QKeySequence::Open, [this] { importRtf(); });
    add(fileMenu, QStringLiteral("export"), tr("&Save to a file..."), QStringLiteral(":/images/save.png"), QKeySequence(), [this] { exportRtf(); }); fileMenu->addSeparator();
    auto *printAction = add(fileMenu, QStringLiteral("print"), tr("&Print..."), QStringLiteral(":/images/code/print.png"), QKeySequence::Print, [this] { print(); }); fileMenu->addSeparator();
    auto *ok = add(fileMenu, QStringLiteral("ok"), tr("Close saving changes"), QStringLiteral(":/images/editor/ok.png"), QKeySequence(), [this] { if (save()) close(); });
    add(editMenu, QStringLiteral("undo"), tr("&Undo"), QStringLiteral(":/images/editor/undo.png"), QKeySequence::Undo, [this] { m_editor->undo(); });
    add(editMenu, QStringLiteral("redo"), tr("&Redo"), QStringLiteral(":/images/editor/redo.png"), QKeySequence::Redo, [this] { m_editor->redo(); }); editMenu->addSeparator();
    auto *cut = add(editMenu, QStringLiteral("cut"), tr("Cu&t"), QStringLiteral(":/images/editor/cut.png"), QKeySequence::Cut, [this] { m_editor->cut(); });
    auto *copy = add(editMenu, QStringLiteral("copy"), tr("&Copy"), QStringLiteral(":/images/editor/copy.png"), QKeySequence::Copy, [this] { m_editor->copy(); });
    auto *paste = add(editMenu, QStringLiteral("paste"), tr("&Paste"), QStringLiteral(":/images/editor/paste.png"), QKeySequence::Paste, [this] { m_editor->paste(); }); editMenu->addSeparator();
    add(editMenu, QStringLiteral("all"), tr("Select &all"), QString(), QKeySequence::SelectAll, [this] { m_editor->selectAll(); });
    add(editMenu, QStringLiteral("goto"), tr("&Go to line..."), QString(), QKeySequence(Qt::CTRL | Qt::Key_G), [this] { goToLine(); });
    add(formatMenu, QStringLiteral("font"), tr("&Font..."), QString(), QKeySequence(), [this] { chooseFont(); }); formatMenu->addSeparator();
    auto *bold = add(formatMenu, QStringLiteral("bold"), tr("&Bold"), QStringLiteral(":/images/information/bold.png"), QKeySequence::Bold, [this] { m_editor->setBold(m_actions.value(QStringLiteral("bold"))->isChecked()); }); bold->setCheckable(true);
    auto *italic = add(formatMenu, QStringLiteral("italic"), tr("&Italic"), QStringLiteral(":/images/information/italic.png"), QKeySequence::Italic, [this] { m_editor->setItalic(m_actions.value(QStringLiteral("italic"))->isChecked()); }); italic->setCheckable(true);
    auto *underline = add(formatMenu, QStringLiteral("underline"), tr("&Underline"), QStringLiteral(":/images/information/underline.png"), QKeySequence::Underline, [this] { m_editor->setUnderline(m_actions.value(QStringLiteral("underline"))->isChecked()); }); underline->setCheckable(true); formatMenu->addSeparator();
    auto *color = add(formatMenu, QStringLiteral("color"), tr("Text C&olor..."), QStringLiteral(":/images/information/color.png"), QKeySequence(), [this] { chooseTextColor(); });
    auto *background = add(formatMenu, QStringLiteral("background"), tr("B&ackground Color..."), QStringLiteral(":/images/information/background.png"), QKeySequence(), [this] { choosePageColor(); }); background->setToolTip(tr("Editor page background (saved as an editor preference)")); formatMenu->addSeparator();
    auto *alignments = new QActionGroup(this); alignments->setExclusive(true);
    const QStringList keys = {QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")}; const QStringList labels = {tr("Align &Left"), tr("Align &Center"), tr("Align &Right")};
    const QList<Qt::Alignment> values = {Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight};
    for (int i = 0; i < keys.size(); ++i) { const Qt::Alignment value = values.at(i); auto *action = add(formatMenu, keys.at(i), labels.at(i), QStringLiteral(":/images/information/%1.png").arg(keys.at(i)), QKeySequence(), [this, value] { m_editor->setAlignment(value); }); action->setCheckable(true); alignments->addAction(action); }
    formatMenu->addSeparator(); auto *bullets = add(formatMenu, QStringLiteral("bullets"), tr("&Bulleted List"), QStringLiteral(":/images/information/bullets.png"), QKeySequence(), [this] { m_editor->setBullets(m_actions.value(QStringLiteral("bullets"))->isChecked()); }); bullets->setCheckable(true);
    toolbar->addAction(ok); toolbar->addSeparator(); toolbar->addAction(printAction); toolbar->addSeparator(); toolbar->addActions({cut, copy, paste}); toolbar->addSeparator();
    m_font->setFixedSize(145, 22); toolbar->addWidget(m_font); m_size->setRange(1, 1638); m_size->setDecimals(1); m_size->setKeyboardTracking(false); m_size->setFixedSize(70, 22); m_size->setValue(10); toolbar->addWidget(m_size); toolbar->addSeparator();
    toolbar->addActions({bold, italic, underline}); toolbar->addSeparator(); toolbar->addActions({color, background}); toolbar->addSeparator();
    for (const auto &key : keys) toolbar->addAction(m_actions.value(key)); toolbar->addSeparator(); toolbar->addAction(bullets);
    connect(m_font, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) { if (!m_refreshing) m_editor->setFamily(font.family()); });
    connect(m_size, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, [this](double value) { if (!m_refreshing) m_editor->setPointSize(value); });
    connect(m_editor, &RichTextEditor::changed, this, &GameInformationWindow::refresh); connect(m_editor, &RichTextEditor::selectionChanged, this, &GameInformationWindow::refresh);
    connect(m_editor, &RichTextEditor::saveRequested, this, &GameInformationWindow::saveProjectRequested);
    connect(m_editor, &RichTextEditor::commandRequested, this, [this](const QString &command) { if (auto *action = m_actions.value(command)) action->trigger(); });
    connect(m_editor, &RichTextEditor::contextMenuRequested, this, [this](const QPoint &position) { refresh(); QMenu menu(this); menu.addActions({m_actions.value(QStringLiteral("undo")), m_actions.value(QStringLiteral("redo"))}); menu.addSeparator(); menu.addActions({m_actions.value(QStringLiteral("cut")), m_actions.value(QStringLiteral("copy")), m_actions.value(QStringLiteral("paste"))}); menu.addSeparator(); menu.addAction(m_actions.value(QStringLiteral("all"))); menu.exec(position); });
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &GameInformationWindow::refresh);
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this); connect(saveShortcut, &QShortcut::activated, this, &GameInformationWindow::saveProjectRequested);
    const QColor page = QSettings().value(QStringLiteral("information/pageColor"), QColor(33, 33, 33)).value<QColor>(); m_editor->setPageColor(page);
}
bool GameInformationWindow::load(const QString &path, QString &error)
{
    if (!m_document.load(path, error) || !m_editor->setRtf(m_document.bytes(), false, error)) return false;
    refresh(); QTimer::singleShot(0, m_editor, &RichTextEditor::focusEditor); return true;
}
void GameInformationWindow::refresh()
{
    if (m_refreshing) return; m_refreshing = true;
    const auto format = m_editor->currentFormat(); m_font->setCurrentFont(QFont(format.family)); m_size->setValue(format.pointSize);
    m_actions.value(QStringLiteral("bold"))->setChecked(format.bold); m_actions.value(QStringLiteral("italic"))->setChecked(format.italic); m_actions.value(QStringLiteral("underline"))->setChecked(format.underline); m_actions.value(QStringLiteral("bullets"))->setChecked(format.bullets);
    m_actions.value(QStringLiteral("left"))->setChecked(format.alignment == Qt::AlignLeft); m_actions.value(QStringLiteral("center"))->setChecked(format.alignment == Qt::AlignHCenter); m_actions.value(QStringLiteral("right"))->setChecked(format.alignment == Qt::AlignRight);
    m_actions.value(QStringLiteral("undo"))->setEnabled(m_editor->canUndo()); m_actions.value(QStringLiteral("redo"))->setEnabled(m_editor->canRedo());
    m_actions.value(QStringLiteral("cut"))->setEnabled(m_editor->hasSelection()); m_actions.value(QStringLiteral("copy"))->setEnabled(m_editor->hasSelection()); m_actions.value(QStringLiteral("paste"))->setEnabled(m_editor->canPaste());
    setWindowTitle(isModified() ? tr("Game Information *") : tr("Game Information")); m_refreshing = false;
}
bool GameInformationWindow::isModified() const { return m_editor->isModified(); }
bool GameInformationWindow::save()
{
    m_size->interpretText(); if (!isModified()) return true; QString error; const QByteArray bytes = m_editor->rtf(error);
    if (!error.isEmpty() || !m_document.save(bytes, error)) { EditorMessageBox::warning(this, tr("Cannot Save Game Information"), error); return false; }
    m_editor->setModified(false); refresh(); emit resourceSaved(ResourceType::GameInformation, filePath(), QString()); return true;
}
void GameInformationWindow::relocate(const QString &oldDirectory, const QString &newDirectory) { m_document.relocate(oldDirectory, newDirectory); }
void GameInformationWindow::closeEvent(QCloseEvent *event)
{
    m_size->interpretText(); if (isModified()) {
        const auto answer = EditorMessageBox::question(this, tr("Game Information"), tr("Save changes to Game Information?"), EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel || (answer == EditorMessageBox::Save && !save())) { event->ignore(); return; }
    }
    event->accept();
}
void GameInformationWindow::importRtf()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("File to load information from"), QFileInfo(filePath()).absolutePath(), tr("Rich Text Files (*.rtf)")); if (path.isEmpty()) return;
    QByteArray bytes; QString error; if (!RichTextDocument::readFile(path, bytes, error) || !m_editor->setRtf(bytes, true, error)) EditorMessageBox::warning(this, tr("Cannot Load Game Information"), error);
    m_editor->focusEditor();
}
void GameInformationWindow::exportRtf()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("File to write information to"), QFileInfo(filePath()).absolutePath() + QStringLiteral("/information.rtf"), tr("Rich Text Files (*.rtf)")); if (path.isEmpty()) return;
    if (QFileInfo(path).absoluteFilePath().compare(filePath(), Qt::CaseInsensitive) == 0) { save(); return; }
    QString error; const QByteArray bytes = m_editor->rtf(error); if (!error.isEmpty() || !RichTextDocument::writeFile(path, bytes, error)) EditorMessageBox::warning(this, tr("Cannot Export Game Information"), error);
}
void GameInformationWindow::chooseFont()
{
    const auto format = m_editor->currentFormat(); QFont font(format.family); font.setPointSizeF(format.pointSize); font.setBold(format.bold); font.setItalic(format.italic); font.setUnderline(format.underline);
    QFontDialog dialog(font, this); dialog.setOption(QFontDialog::DontUseNativeDialog); new DialogFrame(&dialog);
    if (dialog.exec() == QDialog::Accepted) m_editor->setFontFormat(dialog.selectedFont()); m_editor->focusEditor();
}
void GameInformationWindow::chooseTextColor() { const QColor color = EditorColorDialog::getColor(m_editor->currentFormat().color, this, tr("Text Color")); if (color.isValid()) m_editor->setTextColor(color); m_editor->focusEditor(); }
void GameInformationWindow::choosePageColor() { const QColor color = EditorColorDialog::getColor(m_editor->pageColor(), this, tr("Background Color")); if (color.isValid()) { m_editor->setPageColor(color); QSettings().setValue(QStringLiteral("information/pageColor"), color); } m_editor->focusEditor(); }
void GameInformationWindow::print() { QString error; if (!m_editor->print(error)) EditorMessageBox::warning(this, tr("Cannot Print Game Information"), error); }
void GameInformationWindow::goToLine() { bool accepted; const int line = EditorInputDialog::getInt(this, tr("Go to Line"), tr("Line:"), 1, 1, qMax(1, m_editor->lineCount()), 1, &accepted); if (accepted) m_editor->goToLine(line); }

GameInformationWindow::~GameInformationWindow()
{
    for (QObject *child : findChildren<QObject *>()) QObject::disconnect(child, nullptr, this, nullptr);
    delete takeCentralWidget();
}
