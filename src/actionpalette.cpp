#include "editorstandarddialogs.h"
#include "actionpalette.h"
#include "actionlibrary.h"
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTabBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

static const QString LibraryActionMime = QStringLiteral("application/x-qtgms-library-action");
class ActionLibraryTabs : public QTabWidget
{
    class TabBar : public QTabBar {
    public:
        QSize tabSizeHint(int index) const override {
            // East-facing text needs its font height across the strip, plus
            // both borders and the narrow horizontal padding in editor.qss.
            return QSize(qMax(18, fontMetrics().height() + 7), fontMetrics().width(tabText(index)) + 10);
        }
        QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }
    };
public:
    ActionLibraryTabs() {
        setTabBar(new TabBar); tabBar()->setObjectName(QStringLiteral("actionLibraryTabBar"));
        tabBar()->setExpanding(false); tabBar()->setUsesScrollButtons(true);
        tabBar()->setElideMode(Qt::ElideNone); setTabPosition(QTabWidget::East);
    }
};
class ActionPaletteButton : public QToolButton
{
public:
    explicit ActionPaletteButton(const LibraryAction &action, QWidget *parent) : QToolButton(parent), m_libraryId(action.libraryId), m_actionId(action.id)
    { setIcon(action.icon); setIconSize(QSize(24, 24)); setFixedSize(24, 24); setToolTip(action.description.isEmpty() ? action.name : action.description); }
protected:
    void mousePressEvent(QMouseEvent *event) override { m_press = event->pos(); QToolButton::mousePressEvent(event); }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (!(event->buttons() & Qt::LeftButton) || (event->pos() - m_press).manhattanLength() < QApplication::startDragDistance()) { QToolButton::mouseMoveEvent(event); return; }
        auto *drag = new QDrag(this); auto *mime = new QMimeData;
        mime->setData(LibraryActionMime, QByteArray::number(m_libraryId) + ':' + QByteArray::number(m_actionId)); drag->setMimeData(mime); drag->setPixmap(icon().pixmap(24, 24));
        setDown(false); drag->exec(Qt::CopyAction); delete drag;
    }
private:
    int m_libraryId, m_actionId;
    QPoint m_press;
};
ActionPalette::ActionPalette(ActionLibraryManager *libraries, QWidget *parent)
    : QWidget(parent), m_libraries(libraries), m_tabs(new ActionLibraryTabs)
{
    setObjectName(QStringLiteral("actionPalette")); setFixedWidth(100 + qMax(18, m_tabs->fontMetrics().height() + 7));
    setStyleSheet(QStringLiteral("QTabWidget::pane { border: none; } QToolButton { border: none; padding: 0; background: transparent; } QToolButton:hover { border: none; background: transparent; }"));
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0); layout->addWidget(m_tabs);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        QMenu menu(this);
        menu.addAction(tr("Open Library Folder..."), this, [this] {
            if (!QDir().mkpath(ActionLibraryManager::directory()) || !QDesktopServices::openUrl(QUrl::fromLocalFile(ActionLibraryManager::directory())))
                EditorMessageBox::warning(this, tr("Action Libraries"), tr("Cannot open %1").arg(ActionLibraryManager::directory()));
        });
        menu.addAction(tr("Reload Libraries"), m_libraries, &ActionLibraryManager::reload);
        if (!m_libraries->warnings().isEmpty()) menu.addAction(tr("Library Warnings..."), this, [this] { EditorMessageBox::information(this, tr("Action Libraries"), m_libraries->warnings().join(QLatin1Char('\n'))); });
        menu.exec(mapToGlobal(position));
    });
    connect(libraries, &ActionLibraryManager::changed, this, &ActionPalette::rebuild); rebuild();
}
void ActionPalette::rebuild()
{
    const int selected = m_tabs->currentIndex(); while (m_tabs->count()) delete m_tabs->widget(0);
    for (const ActionLibrary &library : m_libraries->libraries()) {
        auto *scroll = new QScrollArea; scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *body = new QWidget; auto *grid = new QGridLayout(body); grid->setContentsMargins(2, 0, 2, 2); grid->setHorizontalSpacing(8); grid->setVerticalSpacing(5);
        for (int column = 0; column < 3; ++column) grid->setColumnMinimumWidth(column, 24);
        int row = 0, column = 0;
        for (int actionIndex = 0; actionIndex < library.actions.size(); ++actionIndex) {
            const LibraryAction &action = library.actions.at(actionIndex);
            if (action.hidden) continue;
            if (action.kind == 9 || action.kind == 10) {
                if (column) { ++row; column = 0; }
                if (action.kind == 10) {
                    auto *heading = new QWidget; heading->setFixedHeight(13); auto *headingLayout = new QHBoxLayout(heading); headingLayout->setContentsMargins(0, 0, 0, 0); headingLayout->setSpacing(3);
                    headingLayout->addWidget(new QLabel(action.name)); auto *line = new QFrame; line->setFrameShape(QFrame::HLine); headingLayout->addWidget(line, 1); grid->addWidget(heading, row++, 0, 1, 3);
                } else if (actionIndex + 1 == library.actions.size() || library.actions.at(actionIndex + 1).kind != 10) {
                    auto *line = new QFrame; line->setFrameShape(QFrame::HLine); line->setFixedHeight(2); grid->addWidget(line, row++, 0, 1, 3);
                }
                continue;
            }
            if (action.kind != 8) {
                auto *button = new ActionPaletteButton(action, body); grid->addWidget(button, row, column, Qt::AlignLeft | Qt::AlignTop);
                const int libraryId = library.id, id = action.id;
                connect(button, &QToolButton::clicked, this, [this, libraryId, id] { emit actionRequested(libraryId, id); });
            }
            if (++column == 3) { column = 0; ++row; }
        }
        grid->setRowStretch(row + 1, 1); scroll->setWidget(body); m_tabs->addTab(scroll, library.caption);
        m_tabs->setTabToolTip(m_tabs->count() - 1, library.author + QLatin1Char('\n') + library.information + QLatin1Char('\n') + library.filePath);
    }
    if (m_tabs->count()) m_tabs->setCurrentIndex(qBound(0, selected, m_tabs->count() - 1));
    setToolTip(m_libraries->warnings().isEmpty() ? tr("Right-click to manage action libraries.") : m_libraries->warnings().join(QLatin1Char('\n')));
}
void ActionPalette::setHasActionContainer(bool available)
{ m_tabs->setToolTip(available ? QString() : tr("Select an event or moment before adding actions.")); }
ActionList::ActionList(QWidget *parent) : QListWidget(parent)
{
    setIconSize(QSize(24, 24)); setSelectionMode(QAbstractItemView::ExtendedSelection); setDragEnabled(true); setAcceptDrops(true);
    setDropIndicatorShown(true); setDragDropMode(QAbstractItemView::DragDrop); setDefaultDropAction(Qt::MoveAction);
}
void ActionList::dragEnterEvent(QDragEnterEvent *event)
{ if (event->source() == this || event->mimeData()->hasFormat(LibraryActionMime)) event->acceptProposedAction(); else event->ignore(); }
void ActionList::dragMoveEvent(QDragMoveEvent *event)
{ if (event->source() == this || event->mimeData()->hasFormat(LibraryActionMime)) event->acceptProposedAction(); else event->ignore(); }
void ActionList::dropEvent(QDropEvent *event)
{
    QListWidgetItem *target = itemAt(event->pos()); int before = target ? row(target) : count();
    if (target && event->pos().y() > visualItemRect(target).center().y()) ++before;
    if (event->source() == this) {
        QList<int> rows; for (QListWidgetItem *item : selectedItems()) rows.append(row(item)); std::sort(rows.begin(), rows.end());
        // Complete our own model edit; do not let the Qt drag source delete rows.
        event->setDropAction(Qt::CopyAction); event->accept(); emit actionsMoved(rows, before);
    } else if (event->mimeData()->hasFormat(LibraryActionMime)) {
        const auto ids = event->mimeData()->data(LibraryActionMime).split(':'); bool a = false, b = false;
        if (ids.size() != 2) { event->ignore(); return; }
        const int libraryId = ids.at(0).toInt(&a), actionId = ids.at(1).toInt(&b);
        if (!a || !b) { event->ignore(); return; }
        event->acceptProposedAction(); emit actionDropped(libraryId, actionId, before);
    } else event->ignore();
}
