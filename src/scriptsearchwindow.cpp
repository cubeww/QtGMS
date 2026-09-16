#include "scriptsearchwindow.h"
#include "editorstandarddialogs.h"
#include <QAction>
#include <QCheckBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>

static QListWidget *createSearchChecklist(QGridLayout *layout, int column,
    const QString &title, const QStringList &labels, quint32 checked)
{
    auto *group = new QGroupBox(title);
    auto *box = new QVBoxLayout(group);
    box->setContentsMargins(5, 9, 5, 5);
    box->setSpacing(5);
    auto *list = new QListWidget;
    list->setStyleSheet(QStringLiteral("QListWidget { background: #202020; }"));
    list->setMinimumSize(150, 145);
    for (int i = 0; i < labels.size(); ++i) {
        auto *item = new QListWidgetItem(labels.at(i), list);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(checked & (1u << i) ? Qt::Checked : Qt::Unchecked);
    }
    auto *all = new QCheckBox(QObject::tr("Everything"));
    box->addWidget(list);
    box->addWidget(all);
    const auto update = [list, all] {
        int count = 0;
        for (int i = 0; i < list->count(); ++i) count += list->item(i)->checkState() == Qt::Checked;
        QSignalBlocker blocker(all);
        all->setTristate(count > 0 && count < list->count());
        all->setCheckState(count == 0 ? Qt::Unchecked : count == list->count() ? Qt::Checked : Qt::PartiallyChecked);
    };
    QObject::connect(list, &QListWidget::itemChanged, list, [update] { update(); });
    QObject::connect(all, &QCheckBox::clicked, list, [list, update](bool checked) {
        {
            QSignalBlocker blocker(list);
            for (int i = 0; i < list->count(); ++i) list->item(i)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        }
        update();
    });
    update();
    layout->addWidget(group, 4, column, 1, 2);
    return list;
}

ScriptSearchDialog::ScriptSearchDialog(const ScriptSearchOptions &options, QWidget *parent)
    : EditorDialog(parent)
{
    setWindowTitle(tr("Search in Scripts"));
    auto *layout = new QGridLayout(bodyWidget());
    layout->setContentsMargins(18, 22, 18, 14);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(12);
    m_searchEdit = new QLineEdit(options.text);
    auto *label = new QLabel(tr("Search:"));
    label->setBuddy(m_searchEdit);
    layout->addWidget(label, 0, 0);
    layout->addWidget(m_searchEdit, 0, 1, 1, 3);
    m_caseCheck = new QCheckBox(tr("Case Sensitive Search"));
    m_commentsCheck = new QCheckBox(tr("Ignore Comments"));
    m_wordCheck = new QCheckBox(tr("Whole Word Only"));
    m_caseCheck->setChecked(options.caseSensitive);
    m_commentsCheck->setChecked(options.ignoreComments);
    m_wordCheck->setChecked(options.wholeWord);
    layout->addWidget(m_caseCheck, 1, 1, 1, 3);
    layout->addWidget(m_commentsCheck, 2, 1, 1, 3);
    layout->addWidget(m_wordCheck, 3, 1, 1, 3);
    m_scopeList = createSearchChecklist(layout, 0, tr("Search In:"),
        {tr("Shaders"), tr("Scripts"), tr("Objects"), tr("Rooms"), tr("Instances"), tr("Time Lines"), tr("Macros")}, options.scopes);
    m_filterList = createSearchChecklist(layout, 2, tr("Filter By:"),
        {tr("Variables"), tr("Constants"), tr("Functions"), tr("Script Names"), tr("Sound Names"),
         tr("Sprite Names"), tr("Shader Names"), tr("Time Line Names"), tr("Room Names"),
         tr("Object Names"), tr("Path Names"), tr("Font Names"), tr("Extension Names")}, options.filters);
    auto *ok = new QPushButton(tr("OK"));
    auto *cancel = new QPushButton(tr("Cancel"));
    ok->setDefault(true);
    ok->setFixedSize(85, 26);
    cancel->setFixedSize(85, 26);
    layout->addWidget(ok, 5, 0, 1, 2, Qt::AlignCenter);
    layout->addWidget(cancel, 5, 2, 1, 2, Qt::AlignCenter);
    const auto update = [this, ok] {
        const auto selected = this->options();
        ok->setEnabled(!selected.text.isEmpty() && selected.scopes && selected.filters);
    };
    connect(m_searchEdit, &QLineEdit::textChanged, this, [update] { update(); });
    connect(m_scopeList, &QListWidget::itemChanged, this, [update] { update(); });
    connect(m_filterList, &QListWidget::itemChanged, this, [update] { update(); });
    // Everything changes the checklist with signals blocked, so also update on clicks.
    for (auto *check : findChildren<QCheckBox *>()) connect(check, &QCheckBox::clicked, this, [update] { update(); });
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    update();
    resize(390, 415);
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

ScriptSearchOptions ScriptSearchDialog::options() const
{
    ScriptSearchOptions result;
    result.text = m_searchEdit->text();
    result.caseSensitive = m_caseCheck->isChecked();
    result.ignoreComments = m_commentsCheck->isChecked();
    result.wholeWord = m_wordCheck->isChecked();
    result.scopes = result.filters = 0;
    for (int i = 0; i < m_scopeList->count(); ++i)
        if (m_scopeList->item(i)->checkState() == Qt::Checked) result.scopes |= 1u << i;
    for (int i = 0; i < m_filterList->count(); ++i)
        if (m_filterList->item(i)->checkState() == Qt::Checked) result.filters |= 1u << i;
    return result;
}

ScriptSearchWindow::ScriptSearchWindow(const QString &query, const QVector<ScriptSearchMatch> &matches,
                                     const QStringList &warnings, QWidget *parent)
    : EditorWindow(parent), m_matches(matches), m_table(new QTableWidget(matches.size(), 3))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Searching Scripts for \"%1\"").arg(query));
    editorMenuBar()->hide();
    resize(960, 540);
    auto *toolbar = addToolBar(tr("Search Results"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->addAction(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("Close"), this, &QWidget::close);
    toolbar->addSeparator();
    auto *save = toolbar->addAction(QIcon(QStringLiteral(":/images/save.png")), tr("Save Results..."), this, &ScriptSearchWindow::saveReport);
    save->setShortcut(QKeySequence::Save);
    auto *print = toolbar->addAction(QIcon(QStringLiteral(":/images/code/print.png")), tr("Print Results..."), this, &ScriptSearchWindow::printReport);
    print->setShortcut(QKeySequence::Print);
    toolbar->addSeparator();
    auto *sort = toolbar->addAction(QIcon(QStringLiteral(":/images/code/sort.svg")), tr("Sort"));
    sort->setToolTip(tr("Sort by the selected column; click again to reverse the order"));
    connect(sort, &QAction::triggered, this, [this] {
        auto *header = m_table->horizontalHeader();
        m_table->sortItems(qMax(0, m_table->currentColumn()), header->sortIndicatorOrder() == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder);
    });
    m_table->setHorizontalHeaderLabels({tr("Found"), tr("Search Description"), tr("Search Context")});
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(25);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setWordWrap(false);
    m_table->setColumnWidth(0, 110);
    m_table->setColumnWidth(1, 430);
    m_table->setColumnWidth(2, 600);
    m_table->setStyleSheet(QStringLiteral("QTableWidget { background: #dddddd; color: #182378; gridline-color: #bbbbbb; selection-background-color: #b9c8df; selection-color: #101850; } QHeaderView::section { background: #dddddd; color: #202020; padding: 4px; border: 1px solid #aaaaaa; }"));
    for (int row = 0; row < matches.size(); ++row) {
        const auto &match = matches.at(row);
        const QStringList values = {match.found, match.description, match.context};
        for (int column = 0; column < 3; ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            item->setData(Qt::UserRole, row);
            item->setToolTip(values.at(column));
            m_table->setItem(row, column, item);
        }
    }
    m_table->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    m_table->setSortingEnabled(true);
    const auto activate = [this](int row) {
        const auto *item = m_table->item(row, 0);
        if (!item) return;
        const int index = item->data(Qt::UserRole).toInt();
        // Opening an action runs a nested event loop; defer until the table has
        // finished dispatching the mouse/key event.
        QTimer::singleShot(0, this, [this, index] { emit matchActivated(m_matches.at(index)); });
    };
    connect(m_table, &QTableWidget::cellClicked, this, [activate](int row, int) { activate(row); });
    connect(m_table, &QTableWidget::cellActivated, this, [activate](int row, int) { activate(row); });
    auto *body = new QWidget;
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_table, 1);
    if (!warnings.isEmpty()) {
        auto *warning = new QLabel(tr("%1 file(s) could not be searched. Hover here for details.").arg(warnings.size()));
        warning->setToolTip(warnings.join(QLatin1Char('\n')));
        warning->setMargin(5);
        layout->addWidget(warning);
    }
    setCentralWidget(body);
    statusBar()->showMessage(matches.isEmpty() ? tr("No matches found.")
        : tr("%1 match(es). Click on a line to open the script.").arg(matches.size()));
}

QString ScriptSearchWindow::reportHtml() const
{
    QString html = QStringLiteral("<html><head><meta charset=\"utf-8\"></head><body><h2>%1</h2><table border=\"1\" cellspacing=\"0\" cellpadding=\"4\"><tr><th>Found</th><th>Search Description</th><th>Search Context</th></tr>").arg(windowTitle().toHtmlEscaped());
    for (int row = 0; row < m_table->rowCount(); ++row) {
        html += QStringLiteral("<tr>");
        for (int column = 0; column < 3; ++column)
            html += QStringLiteral("<td>%1</td>").arg(m_table->item(row, column)->text().toHtmlEscaped());
        html += QStringLiteral("</tr>");
    }
    return html + QStringLiteral("</table></body></html>");
}

void ScriptSearchWindow::saveReport()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Search Results"), QStringLiteral("search-results.html"), tr("HTML files (*.html)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    const QByteArray bytes = reportHtml().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        EditorMessageBox::warning(this, tr("Cannot Save Results"), file.errorString());
}

void ScriptSearchWindow::printReport()
{
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QTextDocument document;
    document.setHtml(reportHtml());
    document.print(&printer);
}
