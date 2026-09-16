#include "editordialog.h"
#include "editorstandarddialogs.h"
#include "timelinepropertieswindow.h"
#include "actioneditorpanel.h"
#include "actionxml.h"
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPersistentModelIndex>
#include <QTimer>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <limits>

TimelinePropertiesWindow::TimelinePropertiesWindow(TimelineDocument *document, Project *project, ActionLibraryManager *libraries, QWidget *parent)
    : ResourceEditorWindow(parent), m_document(document), m_libraries(libraries), m_moments(new QListWidget),
      m_actionEditor(new ActionEditorPanel(project, libraries, document->undoStack())), m_changeButton(new QPushButton(tr("&Change")))
{
    document->setParent(this); setAttribute(Qt::WA_DeleteOnClose); editorMenuBar()->hide(); resize(820, 378); setMinimumSize(700, 376);
    auto *body = new QWidget; auto *layout = new QHBoxLayout(body); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(3);
    auto *controls = new QWidget; controls->setFixedWidth(169); controls->setMinimumHeight(342);
    const auto place = [controls](QWidget *widget, int x, int y, int width, int height) { widget->setParent(controls); widget->setGeometry(x, y, width, height); };
    auto *name = new QLineEdit(document->name()); enableResourceRenaming(name, ResourceType::Timeline); name->setStyleSheet(QStringLiteral("border: 1px solid #777773; padding: 0;"));
    auto *nameLabel = new QLabel(tr("&Name:")); nameLabel->setBuddy(name); place(nameLabel, 8, 8, 100, 17); place(name, 8, 32, 155, 19);
    auto *add = new QPushButton(tr("&Add")); place(add, 8, 82, 75, 23); place(m_changeButton, 89, 82, 75, 23);
    auto *remove = new QPushButton(tr("&Delete")); auto *clear = new QPushButton(tr("C&lear")); place(remove, 8, 113, 75, 23); place(clear, 89, 113, 75, 23);
    auto *shift = new QPushButton(tr("&Shift")); auto *duplicate = new QPushButton(tr("D&uplicate")); auto *spread = new QPushButton(tr("S&pread")); auto *merge = new QPushButton(tr("&Merge"));
    place(shift, 8, 162, 75, 23); place(duplicate, 89, 162, 75, 23); place(spread, 8, 193, 75, 23); place(merge, 89, 193, 75, 23);
    m_rangeButtons = {remove, clear, shift, duplicate, spread, merge};
    const QList<QPushButton *> iconButtons = {add, m_changeButton, remove, clear, shift, duplicate, spread, merge};
    const QStringList icons = {QStringLiteral("add"), QStringLiteral("change"), QStringLiteral("delete"), QStringLiteral("clear"), QStringLiteral("shift"), QStringLiteral("duplicate"), QStringLiteral("spread"), QStringLiteral("merge")};
    for (int i = 0; i < iconButtons.size(); ++i) { iconButtons.at(i)->setIcon(QIcon(QStringLiteral(":/object/controls/%1.png").arg(icons.at(i)))); iconButtons.at(i)->setIconSize(QSize(16, 16)); iconButtons.at(i)->setProperty("leftAligned", true); }
    auto *info = new QPushButton(QIcon(QStringLiteral(":/object/controls/information.png")), tr("Show &Information")); place(info, 17, 249, 137, 23);
    auto *ok = new QPushButton(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("&OK")); place(ok, 40, 304, 89, 23); layout->addWidget(controls);
    auto *splitter = new QSplitter; splitter->setHandleWidth(3); splitter->setChildrenCollapsible(false);
    auto *moments = new QWidget; auto *momentLayout = new QVBoxLayout(moments); momentLayout->setContentsMargins(4, 9, 0, 4); momentLayout->setSpacing(5);
    momentLayout->addWidget(new QLabel(tr("Moments:"))); momentLayout->addWidget(m_moments, 1); moments->setFixedWidth(85); m_moments->setMinimumWidth(80); m_moments->setStyleSheet(QStringLiteral("QListWidget { background: #141414; }")); m_actionEditor->setStyleSheet(QStringLiteral("QListWidget { background: #141414; }")); m_actionEditor->setListMargins(3, 9, 3, 4);
    splitter->addWidget(moments); splitter->addWidget(m_actionEditor); splitter->setSizes({85, 550}); splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1); layout->addWidget(splitter, 1); setCentralWidget(body);
    connect(add, &QPushButton::clicked, this, [this] { editMoment(false); }); connect(m_changeButton, &QPushButton::clicked, this, [this] { editMoment(true); });
    connect(remove, &QPushButton::clicked, this, [this] { transformMoments(MomentOperation::Delete); }); connect(clear, &QPushButton::clicked, this, &TimelinePropertiesWindow::clearMoments);
    connect(shift, &QPushButton::clicked, this, [this] { transformMoments(MomentOperation::Shift); }); connect(duplicate, &QPushButton::clicked, this, [this] { transformMoments(MomentOperation::Duplicate); });
    connect(spread, &QPushButton::clicked, this, [this] { transformMoments(MomentOperation::Spread); }); connect(merge, &QPushButton::clicked, this, [this] { transformMoments(MomentOperation::Merge); });
    connect(info, &QPushButton::clicked, this, &TimelinePropertiesWindow::showInformation); connect(ok, &QPushButton::clicked, this, [this] { if (save()) close(); });
    connect(m_moments, &QListWidget::currentRowChanged, this, [this] { if (!m_refreshing) selectMoment(); });
    m_moments->setToolTip(tr("Double-click a moment to edit its first code action, or add one if none exists."));
    connect(m_moments, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const QPersistentModelIndex index(m_moments->model()->index(m_moments->row(item), 0));
        // Committing code rebuilds the moment list, so finish the mouse event first.
        QTimer::singleShot(0, this, [this, index] {
            if (!index.isValid() || m_refreshing) return;
            m_moments->setCurrentIndex(index); selectMoment();
            m_actionEditor->openFirstCodeAction();
        });
    });
    connect(m_actionEditor, &ActionEditorPanel::actionsEdited, this, [this](const QDomElement &container, const QString &description) {
        QDomDocument xml = m_document->xml(); QDomElement moment = TimelineDocument::moment(xml, m_document->selectedStep()); if (moment.isNull()) return;
        QDomElement event = moment.firstChildElement(QStringLiteral("event"));
        if (event.isNull()) moment.appendChild(xml.importNode(container, true)); else moment.replaceChild(xml.importNode(container, true), event);
        m_document->edit(xml, description, m_document->selectedStep());
    });
    connect(document, &TimelineDocument::changed, this, &TimelinePropertiesWindow::refresh);
    connect(document->undoStack(), &QUndoStack::cleanChanged, this, [this] { setWindowTitle(tr("Time Line Properties: %1%2").arg(m_document->name(), m_document->isModified() ? QStringLiteral(" *") : QString())); });
    connect(document, &TimelineDocument::saved, this, [this] { emit resourceSaved(ResourceType::Timeline, filePath(), QString()); });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this); connect(saveShortcut, &QShortcut::activated, this, &TimelinePropertiesWindow::saveProjectRequested);
    auto *undo = new QShortcut(QKeySequence::Undo, this); auto *redo = new QShortcut(QKeySequence::Redo, this); connect(undo, &QShortcut::activated, document->undoStack(), &QUndoStack::undo); connect(redo, &QShortcut::activated, document->undoStack(), &QUndoStack::redo);
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, m_moments); deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut); connect(deleteShortcut, &QShortcut::activated, remove, &QPushButton::click);
    m_moments->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_moments, &QWidget::customContextMenuRequested, this, [this, add](const QPoint &position) {
        QMenu menu(this); QList<QPushButton *> buttons = {add, m_changeButton}; buttons.append(m_rangeButtons);
        for (QPushButton *button : buttons) { QAction *action = menu.addAction(button->text(), button, &QPushButton::click); action->setEnabled(button->isEnabled()); }
        menu.addSeparator(); menu.addAction(m_document->undoStack()->createUndoAction(&menu)); menu.addAction(m_document->undoStack()->createRedoAction(&menu)); menu.exec(m_moments->viewport()->mapToGlobal(position));
    });
    refresh();
}
void TimelinePropertiesWindow::refresh()
{
    m_refreshing = true; setWindowTitle(tr("Time Line Properties: %1%2").arg(m_document->name(), m_document->isModified() ? QStringLiteral(" *") : QString()));
    m_moments->clear(); QDomDocument xml = m_document->xml(); int selected = -1;
    for (QDomElement moment : TimelineDocument::moments(xml)) {
        const int step = ActionXml::text(moment, QStringLiteral("step")).toInt(); auto *item = new QListWidgetItem(tr("Step %1").arg(step), m_moments); item->setData(Qt::UserRole, step);
        item->setToolTip(tr("Moment %1: %2 action(s)").arg(step).arg(ActionXml::elements(moment.firstChildElement(QStringLiteral("event")), QStringLiteral("action")).size()));
        if (step == m_document->selectedStep()) selected = m_moments->count() - 1;
    }
    if (selected < 0 && m_moments->count()) selected = 0;
    m_moments->setCurrentRow(selected); for (QPushButton *button : m_rangeButtons) button->setEnabled(m_moments->count() > 0);
    m_refreshing = false; selectMoment();
}
void TimelinePropertiesWindow::selectMoment()
{
    const auto *item = m_moments->currentItem(); const int step = item ? item->data(Qt::UserRole).toInt() : -1; m_document->setSelectedStep(step); m_changeButton->setEnabled(item != nullptr);
    QDomDocument xml = m_document->xml(); QDomElement moment = TimelineDocument::moment(xml, step);
    m_actionEditor->setActions(moment.isNull() ? QDomElement() : ActionXml::child(moment, QStringLiteral("event")),
        m_document->name() + QStringLiteral("_Step%1").arg(step));
}
void TimelinePropertiesWindow::showAction(int step, int actionIndex, int offset, int length, int argument)
{
    for (int row = 0; row < m_moments->count(); ++row) {
        if (m_moments->item(row)->data(Qt::UserRole).toInt() != step) continue;
        m_moments->setCurrentRow(row);
        selectMoment();
        m_actionEditor->openAction(actionIndex, offset, length, argument);
        return;
    }
}
void TimelinePropertiesWindow::editMoment(bool change)
{
    const int oldStep = m_document->selectedStep(); if (change && oldStep < 0) return;
    int initial = change ? oldStep : oldStep < 0 ? 0 : oldStep < std::numeric_limits<int>::max() ? oldStep + 1 : oldStep;
    QDomDocument xml = m_document->xml(); if (!change) while (initial < std::numeric_limits<int>::max() && !TimelineDocument::moment(xml, initial).isNull()) ++initial;
    bool accepted = false; const int step = EditorInputDialog::getInt(this, change ? tr("Changing a Moment") : tr("Adding a Moment"), tr("Indicate the moment:"), initial, 0, std::numeric_limits<int>::max(), 1, &accepted);
    if (!accepted) return; QString error;
    const bool success = change ? m_document->changeMoment(oldStep, step, error) : m_document->addMoment(step, error);
    if (!success) EditorMessageBox::warning(this, tr("Timeline Moment"), error);
}
void TimelinePropertiesWindow::transformMoments(MomentOperation operation)
{
    if (!m_moments->count()) return;
    const QStringList titles = {tr("Delete Moments"), tr("Shift Moments"), tr("Duplicate Moments"), tr("Spread Moments"), tr("Merge Moments")};
    EditorDialog dialog(this); dialog.setWindowTitle(titles.at(static_cast<int>(operation))); auto *layout = new QVBoxLayout(dialog.bodyWidget()); auto *form = new QFormLayout;
    auto *first = new QSpinBox; auto *last = new QSpinBox; auto *target = new QSpinBox; const int maximum = std::numeric_limits<int>::max();
    for (QSpinBox *spin : {first, last, target}) spin->setRange(0, maximum);
    const int selected = qMax(0, m_document->selectedStep()); first->setValue(selected); last->setValue(selected); target->setValue(selected < maximum ? selected + 1 : selected);
    form->addRow(tr("From step:"), first); form->addRow(tr("Through step:"), last);
    auto *percentage = new QDoubleSpinBox; percentage->setRange(0, 1000000); percentage->setDecimals(2); percentage->setValue(100); percentage->setSuffix(QStringLiteral(" %"));
    if (operation == MomentOperation::Shift || operation == MomentOperation::Duplicate) form->addRow(tr("New start step:"), target); else { delete target; target = nullptr; }
    if (operation == MomentOperation::Spread) form->addRow(tr("Spread:"), percentage); else { delete percentage; percentage = nullptr; }
    layout->addLayout(form);
    auto *explanation = new QLabel(operation == MomentOperation::Delete ? tr("Deletes every moment in this range, including its actions.")
        : operation == MomentOperation::Spread ? tr("100% keeps the timing. Steps are rounded to the nearest integer; moments that overlap are merged.")
        : operation == MomentOperation::Merge ? tr("Moves all actions in the range to its first step, in chronological order.")
        : tr("The range starts at the new step. Actions that overlap an existing moment are appended to it."));
    explanation->setWordWrap(true); layout->addWidget(explanation); auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [this, &dialog, operation, first, last, target, percentage] {
        const double value = operation == MomentOperation::Spread ? percentage->value()
            : operation == MomentOperation::Shift || operation == MomentOperation::Duplicate ? target->value() : 0;
        QString error; if (!m_document->transform(operation, first->value(), last->value(), value, error)) { EditorMessageBox::warning(&dialog, tr("Timeline Moments"), error); return; } dialog.accept();
    });
    dialog.resize(360, 220); dialog.exec();
}
void TimelinePropertiesWindow::clearMoments()
{
    if (!m_moments->count()) return;
    if (EditorMessageBox::question(this, tr("Clear Time Line"), tr("Delete all moments and their actions?"), EditorMessageBox::Yes | EditorMessageBox::No, EditorMessageBox::No) == EditorMessageBox::Yes) m_document->clear();
}
void TimelinePropertiesWindow::showInformation()
{
    EditorDialog dialog(this); dialog.setWindowTitle(tr("Time Line Information: %1").arg(m_document->name())); auto *layout = new QVBoxLayout(dialog.bodyWidget()); auto *text = new QTextBrowser;
    QString html = QStringLiteral("<h2>%1</h2>").arg(m_document->name().toHtmlEscaped()); QDomDocument xml = m_document->xml();
    for (QDomElement moment : TimelineDocument::moments(xml)) {
        html += QStringLiteral("<h3>%1</h3><ol>").arg(tr("Moment %1").arg(ActionXml::text(moment, QStringLiteral("step"))).toHtmlEscaped());
        for (QDomElement action : ActionXml::elements(moment.firstChildElement(QStringLiteral("event")), QStringLiteral("action"))) html += QStringLiteral("<li>%1</li>").arg(ActionXml::actionText(action, *m_libraries).toHtmlEscaped()); html += QStringLiteral("</ol>");
    }
    text->setHtml(html); layout->addWidget(text); auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(buttons); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); dialog.resize(600, 500); dialog.exec();
}
QString TimelinePropertiesWindow::filePath() const { return m_document->filePath(); }
void TimelinePropertiesWindow::relocate(const QString &oldDirectory, const QString &newDirectory) { m_document->relocate(oldDirectory, newDirectory); }
bool TimelinePropertiesWindow::save()
{
    QString error; if (!m_document->save(error)) { EditorMessageBox::critical(this, tr("Cannot Save Time Line"), error); return false; } return true;
}
void TimelinePropertiesWindow::closeEvent(QCloseEvent *event)
{
    if (m_document->isModified()) {
        const auto answer = EditorMessageBox::question(this, tr("Time Line Properties"), tr("Save changes to %1?").arg(m_document->name()), EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel || (answer == EditorMessageBox::Save && !save())) { event->ignore(); return; }
    }
    event->accept();
}
