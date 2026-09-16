#include "macroeditorwindow.h"
#include "macrodocument.h"
#include "actionxml.h"
#include "editorstandarddialogs.h"
#include "textfiledocument.h"
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QSaveFile>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

void MacroEditorWindow::selectMacro(const QString &name, int column)
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (!m_table->item(row, 0) || m_table->item(row, 0)->text() != name) continue;
        m_table->setCurrentCell(row, column);
        m_table->scrollToItem(m_table->item(row, column));
        m_table->setFocus();
        return;
    }
}

MacroEditorWindow::MacroEditorWindow(MacroDocument *document, QWidget *parent)
    : ResourceEditorWindow(parent), m_document(document), m_table(new QTableWidget(0, 2))
{
    document->setParent(this); setAttribute(Qt::WA_DeleteOnClose); editorMenuBar()->hide(); resize(440, 480); setMinimumSize(430, 330);
    auto *body = new QWidget; auto *layout = new QVBoxLayout(body); layout->setContentsMargins(10, 8, 10, 8); layout->setSpacing(7);
    auto *scope = new QLabel(document->isGlobal() ? tr("Available in all configurations.") : tr("These macros override same-name macros in All Configurations.")); scope->setWordWrap(true); layout->addWidget(scope);
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Value")}); m_table->setColumnWidth(0, 156); m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->hide(); m_table->setSelectionBehavior(QAbstractItemView::SelectRows); m_table->setSelectionMode(QAbstractItemView::SingleSelection); m_table->setTabKeyNavigation(true); layout->addWidget(m_table, 1);
    auto *controls = new QGridLayout; controls->setHorizontalSpacing(10); controls->setVerticalSpacing(7); layout->addLayout(controls);
    auto button = [controls](const QString &label, int row, int column) {
        auto *button = new QPushButton(label); button->setAutoDefault(false); button->setFixedHeight(25); button->setMinimumWidth(65);
        controls->addWidget(button, row, column); return button;
    };
    auto *insert = button(tr("&Insert"), 0, 0), *add = button(tr("&Add"), 1, 0);
    m_deleteButton = button(tr("&Delete"), 0, 1); m_clearButton = button(tr("&Clear"), 1, 1);
    m_upButton = button(tr("&Up"), 0, 2); m_downButton = button(tr("Do&wn"), 1, 2);
    m_sortButton = button(tr("&Sort"), 0, 3);
    auto *load = button(tr("&Load"), 0, 4), *exportButton = button(tr("Sa&ve"), 1, 4);
    load->setToolTip(tr("Append macros from a NAME = VALUE text file")); exportButton->setToolTip(tr("Export this macro list to a text file"));
    auto *bottom = new QHBoxLayout; layout->addLayout(bottom);
    auto *ok = new QPushButton(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("&OK")); ok->setFixedSize(83, 25);
    auto *cancel = new QPushButton(QIcon(QStringLiteral(":/object/controls/delete.png")), tr("&Cancel")); cancel->setFixedSize(83, 25); bottom->addWidget(ok); bottom->addStretch(); bottom->addWidget(cancel); setCentralWidget(body);
    connect(ok, &QPushButton::clicked, this, [this] { if (save()) close(); });
    connect(cancel, &QPushButton::clicked, this, [this] { m_discard = true; close(); });
    connect(insert, &QPushButton::clicked, this, [this] { addMacro(true); }); connect(add, &QPushButton::clicked, this, [this] { addMacro(false); });
    connect(m_deleteButton, &QPushButton::clicked, this, &MacroEditorWindow::deleteMacro); connect(m_clearButton, &QPushButton::clicked, this, &MacroEditorWindow::clearMacros);
    connect(m_upButton, &QPushButton::clicked, this, [this] { moveMacro(-1); }); connect(m_downButton, &QPushButton::clicked, this, [this] { moveMacro(1); });
    connect(m_sortButton, &QPushButton::clicked, this, &MacroEditorWindow::sortMacros); connect(load, &QPushButton::clicked, this, &MacroEditorWindow::importMacros); connect(exportButton, &QPushButton::clicked, this, &MacroEditorWindow::exportMacros);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &MacroEditorWindow::updateActions);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        auto xml = m_document->xml(); auto entries = ActionXml::elements(xml.documentElement(), QStringLiteral("constant")); if (item->row() >= entries.size()) return;
        auto entry = entries.at(item->row());
        if (item->column() == 0) entry.setAttribute(QStringLiteral("name"), item->text());
        else { while (!entry.firstChild().isNull()) entry.removeChild(entry.firstChild()); entry.appendChild(xml.createTextNode(item->text())); }
        m_document->edit(xml, tr("Edit macro"));
    });
    connect(document, &MacroDocument::changed, this, &MacroEditorWindow::refresh);
    connect(document->undoStack(), &QUndoStack::cleanChanged, this, [this] { setWindowTitle(tr("User-Defined Macros: %1%2").arg(m_document->scopeName(), m_document->isModified() ? QStringLiteral(" *") : QString())); });
    connect(document, &MacroDocument::saved, this, [this] { emit resourceSaved(ResourceType::Macro, filePath(), QString()); });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this); connect(saveShortcut, &QShortcut::activated, this, &MacroEditorWindow::saveProjectRequested);
    auto *undo = new QShortcut(QKeySequence::Undo, this), *redo = new QShortcut(QKeySequence::Redo, this);
    connect(undo, &QShortcut::activated, this, [this] { commitEditing(); m_document->undoStack()->undo(); });
    connect(redo, &QShortcut::activated, this, [this] { commitEditing(); m_document->undoStack()->redo(); });
    refresh();
}
MacroEditorWindow::~MacroEditorWindow()
{
    for (QObject *child : findChildren<QObject *>()) QObject::disconnect(child, nullptr, this, nullptr);
    delete takeCentralWidget();
}
void MacroEditorWindow::commitEditing()
{
    if (QWidget *focus = QApplication::focusWidget()) if (m_table->isAncestorOf(focus)) m_table->setFocus(Qt::OtherFocusReason);
}
void MacroEditorWindow::refresh()
{
    const QSignalBlocker blocker(m_table); const int selected = m_table->currentRow();
    const auto xml = m_document->xml(); const auto entries = ActionXml::elements(xml.documentElement(), QStringLiteral("constant"));
    m_table->setRowCount(entries.size());
    for (int row = 0; row < entries.size(); ++row) {
        const QStringList values = {entries.at(row).attribute(QStringLiteral("name")), entries.at(row).text()};
        for (int column = 0; column < 2; ++column) {
            auto *item = m_table->item(row, column);
            if (!item) m_table->setItem(row, column, new QTableWidgetItem(values.at(column)));
            else if (item->text() != values.at(column)) item->setText(values.at(column));
        }
        m_table->setRowHeight(row, 22);
    }
    if (m_table->currentRow() < 0 && !entries.isEmpty()) m_table->setCurrentCell(qBound(0, selected, entries.size() - 1), 0);
    setWindowTitle(tr("User-Defined Macros: %1%2").arg(m_document->scopeName(), m_document->isModified() ? QStringLiteral(" *") : QString())); updateActions();
}
void MacroEditorWindow::updateActions()
{
    const int row = m_table->currentRow(), count = m_table->rowCount();
    m_deleteButton->setEnabled(row >= 0); m_upButton->setEnabled(row > 0); m_downButton->setEnabled(row >= 0 && row + 1 < count);
    m_clearButton->setEnabled(count > 0); m_sortButton->setEnabled(count > 1);
}
void MacroEditorWindow::addMacro(bool insert)
{
    commitEditing(); auto xml = m_document->xml(); auto root = xml.documentElement(); const auto entries = ActionXml::elements(root, QStringLiteral("constant"));
    const int row = insert && m_table->currentRow() >= 0 ? m_table->currentRow() : entries.size();
    auto entry = xml.createElement(QStringLiteral("constant")); entry.setAttribute(QStringLiteral("name"), QString()); entry.appendChild(xml.createTextNode(QStringLiteral("0")));
    if (row < entries.size()) root.insertBefore(entry, entries.at(row)); else root.appendChild(entry);
    m_document->edit(xml, tr("Add macro")); m_table->setCurrentCell(row, 0); m_table->editItem(m_table->item(row, 0));
}
void MacroEditorWindow::deleteMacro()
{
    commitEditing(); const int row = m_table->currentRow(); if (row < 0) return;
    auto xml = m_document->xml(); const auto entries = ActionXml::elements(xml.documentElement(), QStringLiteral("constant"));
    xml.documentElement().removeChild(entries.at(row)); m_document->edit(xml, tr("Delete macro"));
}
void MacroEditorWindow::clearMacros()
{
    commitEditing(); if (EditorMessageBox::question(this, tr("Clear Macros"), tr("Delete all macros in this list?"), EditorMessageBox::Yes | EditorMessageBox::No, EditorMessageBox::No) != EditorMessageBox::Yes) return;
    auto xml = m_document->xml(); auto root = xml.documentElement(); for (auto entry : ActionXml::elements(root, QStringLiteral("constant"))) root.removeChild(entry);
    m_document->edit(xml, tr("Clear macros"));
}
static void reorderMacros(QDomDocument &xml, const QList<QDomElement> &ordered)
{
    auto root = xml.documentElement(); const auto entries = ActionXml::elements(root, QStringLiteral("constant"));
    for (int i = 0; i < entries.size(); ++i) root.replaceChild(ordered.at(i).cloneNode(true), entries.at(i));
}
void MacroEditorWindow::moveMacro(int offset)
{
    commitEditing(); const int row = m_table->currentRow(); auto xml = m_document->xml(); auto entries = ActionXml::elements(xml.documentElement(), QStringLiteral("constant"));
    if (row < 0 || row + offset < 0 || row + offset >= entries.size()) return;
    qSwap(entries[row], entries[row + offset]); reorderMacros(xml, entries); m_document->edit(xml, tr("Move macro")); m_table->setCurrentCell(row + offset, 0);
}
void MacroEditorWindow::sortMacros()
{
    commitEditing(); auto xml = m_document->xml(); auto entries = ActionXml::elements(xml.documentElement(), QStringLiteral("constant"));
    std::stable_sort(entries.begin(), entries.end(), [](QDomElement a, QDomElement b) { return a.attribute(QStringLiteral("name")).compare(b.attribute(QStringLiteral("name")), Qt::CaseInsensitive) < 0; });
    reorderMacros(xml, entries); m_document->edit(xml, tr("Sort macros"));
}
void MacroEditorWindow::importMacros()
{
    commitEditing(); const QString path = QFileDialog::getOpenFileName(this, tr("Load Macros"), QString(), tr("Macro text files (*.txt);;All files (*)")); if (path.isEmpty()) return;
    QString text, error; if (!TextFileDocument::readText(path, text, error)) { EditorMessageBox::critical(this, tr("Cannot Load Macros"), error); return; }
    auto xml = m_document->xml(); auto root = xml.documentElement(); int lineNumber = 0;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")); text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    for (QString line : text.split(QLatin1Char('\n'))) {
        ++lineNumber; if (line.trimmed().isEmpty()) continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0) { EditorMessageBox::warning(this, tr("Cannot Load Macros"), tr("Line %1: expected NAME = VALUE.").arg(lineNumber)); return; }
        auto entry = xml.createElement(QStringLiteral("constant")); entry.setAttribute(QStringLiteral("name"), line.left(equals).trimmed()); entry.appendChild(xml.createTextNode(line.mid(equals + 1).trimmed())); root.appendChild(entry);
    }
    if (!MacroDocument::validate(xml, error)) { EditorMessageBox::warning(this, tr("Cannot Load Macros"), error); return; }
    m_document->edit(xml, tr("Load macros"));
}
void MacroEditorWindow::exportMacros()
{
    commitEditing(); const auto xml = m_document->xml(); QString error;
    if (!MacroDocument::validate(xml, error)) { EditorMessageBox::warning(this, tr("Cannot Export Macros"), error); return; }
    QString text;
    for (auto entry : ActionXml::elements(xml.documentElement(), QStringLiteral("constant"))) {
        if (entry.text().contains(QLatin1Char('\n')) || entry.text().contains(QLatin1Char('\r'))) { EditorMessageBox::warning(this, tr("Cannot Export Macros"), tr("Text exports require each macro value to fit on one line.")); return; }
        text += entry.attribute(QStringLiteral("name")) + QStringLiteral(" = ") + entry.text() + QStringLiteral("\r\n");
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Macros"), QStringLiteral("macros.txt"), tr("Macro text files (*.txt)")); if (path.isEmpty()) return;
    QSaveFile file(path); const QByteArray bytes = QByteArray::fromHex("efbbbf") + text.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) EditorMessageBox::critical(this, tr("Cannot Export Macros"), file.errorString());
}
bool MacroEditorWindow::save()
{
    commitEditing(); QString error; if (m_document->save(error)) return true;
    EditorMessageBox::warning(this, tr("Cannot Save Macros"), error); return false;
}
QString MacroEditorWindow::filePath() const { return m_document->filePath(); }
bool MacroEditorWindow::isModified() const { return m_document->isModified(); }
void MacroEditorWindow::relocate(const QString &oldDirectory, const QString &newDirectory) { m_document->relocate(oldDirectory, newDirectory); refresh(); }
void MacroEditorWindow::closeEvent(QCloseEvent *event)
{
    commitEditing();
    if (!m_discard && m_document->isModified()) {
        const auto answer = EditorMessageBox::question(this, tr("User-Defined Macros"), tr("Save changes to %1?").arg(m_document->scopeName()), EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel || (answer == EditorMessageBox::Save && !save())) { event->ignore(); return; }
    }
    event->accept();
}
