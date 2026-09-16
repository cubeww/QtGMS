#include "codecompletion.h"
#include "codeeditor.h"
#include "codeblockdata.h"
#include "codeeditorcolors.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSet>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTextBlock>
#include <QTextLayout>
#include <QTimer>
#include <algorithm>
#include <limits>

static const int ItemIndexRole = Qt::UserRole + 1;
static const int MatchPositionsRole = Qt::UserRole + 2;
static const int ItemNameRole = Qt::UserRole + 3;

static bool completionWordCharacter(QChar character)
{ return character.isLetterOrNumber() || character == QLatin1Char('_'); }

static bool completionWordBoundary(const QString &name, int position)
{
    return position == 0 || name.at(position - 1) == QLatin1Char('_')
        || (name.at(position).isUpper() && name.at(position - 1).isLower());
}

struct CompletionMatch
{
    int itemIndex;
    int score;
    QVector<int> positions;
};

// Ordered-subsequence scoring with prefix, word-boundary and consecutive-letter
// bonuses. Dynamic programming finds the best alignment, including repeated letters.
static bool scoreCompletion(const QString &query, const QString &name, CompletionMatch &match)
{
    match.score = 0;
    if (query.isEmpty()) return true;
    if (query.size() > name.size()) return false;
    const QString lowerQuery = query.toLower(), lowerName = name.toLower();
    int probe = 0;
    for (QChar character : lowerName) if (probe < lowerQuery.size() && character == lowerQuery.at(probe)) ++probe;
    if (probe != lowerQuery.size()) return false;
    const int width = name.size();
    const int NoMatch = std::numeric_limits<int>::min() / 4;
    QVector<int> previous(width, NoMatch), current(width, NoMatch);
    QVector<int> parents(query.size() * width, -1);
    for (int i = 0; i < query.size(); ++i) {
        current.fill(NoMatch);
        int bestGap = NoMatch, bestIndex = -1;
        for (int j = 0; j < width; ++j) {
            if (j > 0 && previous.at(j - 1) != NoMatch && previous.at(j - 1) + j - 1 > bestGap) {
                bestGap = previous.at(j - 1) + j - 1; bestIndex = j - 1;
            }
            if (lowerQuery.at(i) != lowerName.at(j)) continue;
            int score = i == 0 ? -j : bestGap == NoMatch ? NoMatch : bestGap - j;
            int parent = i == 0 ? -1 : bestIndex;
            if (i > 0 && j > 0 && previous.at(j - 1) != NoMatch && previous.at(j - 1) + 24 > score) {
                score = previous.at(j - 1) + 24; parent = j - 1;
            }
            if (score == NoMatch) continue;
            score += completionWordBoundary(name, j) ? 32 : 1;
            if (query.at(i) == name.at(j)) score += 2;
            current[j] = score; parents[i * width + j] = parent;
        }
        previous.swap(current);
    }
    int best = -1;
    for (int j = 0; j < width; ++j) if (best < 0 || previous.at(j) > previous.at(best)) best = j;
    match.score = previous.at(best);
    if (lowerName == lowerQuery) match.score += 2000;
    else if (lowerName.startsWith(lowerQuery)) match.score += 1000;
    else if (lowerName.contains(lowerQuery)) match.score += 150;
    match.positions.resize(query.size());
    for (int i = query.size() - 1; i >= 0; --i) {
        match.positions[i] = best; best = parents.at(i * width + best);
    }
    return true;
}

class CompletionItemDelegate : public QStyledItemDelegate
{
public:
    explicit CompletionItemDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(23, option.fontMetrics.height() + 6)); return size;
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem item(option); initStyleOption(&item, index);
        const QString text = item.text; item.text.clear();
        const QStyle *style = item.widget ? item.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);
        const QRect rect = style->subElementRect(QStyle::SE_ItemViewItemText, &item, item.widget).adjusted(4, 0, -4, 0);
        const bool selected = item.state & QStyle::State_Selected;
        QTextLayout layout(text, item.font);
        QList<QTextLayout::FormatRange> formats;
        QTextLayout::FormatRange base; base.start = 0; base.length = text.size();
        base.format.setForeground(selected ? item.palette.highlightedText() : item.palette.text());
        formats.append(base);
        for (const QVariant &position : index.data(MatchPositionsRole).toList()) {
            QTextLayout::FormatRange hit; hit.start = position.toInt(); hit.length = 1;
            hit.format.setForeground(CodeEditorColors::Comment); hit.format.setFontWeight(QFont::Bold); formats.append(hit);
        }
        layout.setAdditionalFormats(formats);
        layout.beginLayout(); QTextLine line = layout.createLine();
        if (line.isValid()) line.setLineWidth(100000);
        layout.endLayout();
        painter->save(); painter->setClipRect(rect);
        layout.draw(painter, QPointF(rect.left(), rect.top() + (rect.height() - layout.boundingRect().height()) / 2));
        painter->restore();
    }
};

static QString completionKindName(CodeCompletionItem::Kind kind)
{
    switch (kind) {
    case CodeCompletionItem::Kind::Keyword: return QObject::tr("Keyword");
    case CodeCompletionItem::Kind::Function: return QObject::tr("Function");
    case CodeCompletionItem::Kind::Constant: return QObject::tr("Constant");
    case CodeCompletionItem::Kind::Script: return QObject::tr("Script");
    case CodeCompletionItem::Kind::Resource: return QObject::tr("Resource");
    default: return QObject::tr("Variable");
    }
}

CodeCompletion::CodeCompletion(CodeEditor *editor)
    : QObject(editor), m_editor(editor), m_completer(new QCompleter(this)),
      m_model(new QStandardItemModel(this)), m_timer(new QTimer(this))
{
    m_completer->setWidget(editor); m_completer->setModel(m_model);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setCompletionRole(ItemNameRole); m_completer->setMaxVisibleItems(10);
    auto *popup = m_completer->popup(); popup->setObjectName(QStringLiteral("codeCompletionPopup"));
    popup->setItemDelegate(new CompletionItemDelegate(popup));
    popup->setStyleSheet(QStringLiteral("QAbstractItemView { background:#222222; color:#c0c0c0; border:1px solid #777; selection-background-color:#3a4057; selection-color:white; padding:1px; }"));
    popup->setTextElideMode(Qt::ElideNone);
    popup->installEventFilter(this);
    m_timer->setSingleShot(true); m_timer->setInterval(60);
    connect(m_timer, &QTimer::timeout, this, [this] { updatePopup(false); });
    connect(m_completer, static_cast<void (QCompleter::*)(const QModelIndex &)>(&QCompleter::activated),
            this, [this](const QModelIndex &index) { accept(index); });
    connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { dismiss(); });
    connect(editor->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { dismiss(); });
    editor->installEventFilter(this);
    editor->viewport()->installEventFilter(this);
}

void CodeCompletion::setItems(const QVector<CodeCompletionItem> &items)
{
    if (m_items == items) return;
    dismiss(); m_items.clear(); QSet<QString> names;
    for (const auto &item : items) if (!item.name.isEmpty() && !names.contains(item.name)) {
        names.insert(item.name); m_items.append(item);
    }
}

bool CodeCompletion::token(QString &query, int &start, int &end) const
{
    if (m_editor->isReadOnly() || m_editor->textCursor().hasSelection()) return false;
    const QTextCursor cursor = m_editor->textCursor(); const QTextBlock block = cursor.block();
    const auto *data = dynamic_cast<const CodeBlockData *>(block.userData());
    if (!data || !data->isCodePosition(cursor.positionInBlock())) return false;
    const QString text = block.text(); int left = cursor.positionInBlock(), right = left;
    while (left > 0 && completionWordCharacter(text.at(left - 1))) --left;
    while (right < text.size() && completionWordCharacter(text.at(right))) ++right;
    if (left < right && text.at(left).isDigit()) return false;
    query = text.mid(left, cursor.positionInBlock() - left);
    start = block.position() + left; end = block.position() + right;
    return true;
}

void CodeCompletion::request()
{ m_editor->setFocus(); m_timer->stop(); updatePopup(true); }

void CodeCompletion::dismiss()
{ m_timer->stop(); m_completer->popup()->hide(); }

void CodeCompletion::updatePopup(bool manual)
{
    QString query; int start, end;
    if (!m_editor->hasFocus() || !token(query, start, end) || (!manual && query.isEmpty())) { dismiss(); return; }
    QVector<CompletionMatch> matches;
    for (int i = 0; i < m_items.size(); ++i) {
        CompletionMatch match; match.itemIndex = i;
        if (!scoreCompletion(query, m_items.at(i).name, match)) continue;
        if (m_items.at(i).obsolete) match.score -= 40;
        matches.append(match);
    }
    const int count = qMin(150, matches.size());
    if (!count) { dismiss(); return; }
    std::partial_sort(matches.begin(), matches.begin() + count, matches.end(), [this](const CompletionMatch &left, const CompletionMatch &right) {
        if (left.score != right.score) return left.score > right.score;
        const QString &a = m_items.at(left.itemIndex).name, &b = m_items.at(right.itemIndex).name;
        if (a.size() != b.size()) return a.size() < b.size();
        const int order = QString::compare(a, b, Qt::CaseInsensitive);
        return order ? order < 0 : a < b;
    });
    m_query = query; m_start = start; m_model->clear();
    for (int i = 0; i < count; ++i) {
        const auto &match = matches.at(i); const auto &item = m_items.at(match.itemIndex);
        const QString kind = completionKindName(item.kind);
        QString label = item.detail.startsWith(item.name) ? item.detail.section(QLatin1Char('\n'), 0, 0) : item.name;
        label += QStringLiteral("    ") + kind;
        if (item.readOnly) label += tr(" (read-only)");
        if (item.obsolete) label += tr(" (obsolete)");
        auto *row = new QStandardItem(label);
        row->setData(match.itemIndex, ItemIndexRole); row->setData(item.name, ItemNameRole);
        QVariantList positions; for (int position : match.positions) positions.append(position);
        row->setData(positions, MatchPositionsRole); row->setToolTip(kind + QLatin1Char('\n') + item.detail);
        m_model->appendRow(row);
    }
    m_completer->setCompletionPrefix(QString()); m_completer->setCurrentRow(0);
    m_completer->popup()->setFont(m_editor->font());
    QRect rect = m_editor->cursorRect();
    rect.setWidth(qBound(320, m_completer->popup()->sizeHintForColumn(0) + 28, 720));
    m_completer->complete(rect);
    m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
}

void CodeCompletion::accept(const QModelIndex &index)
{
    if (!index.isValid()) return;
    QString query; int start, end;
    if (!token(query, start, end) || query != m_query || start != m_start) { dismiss(); return; }
    const int itemIndex = index.data(ItemIndexRole).toInt();
    if (itemIndex < 0 || itemIndex >= m_items.size()) return;
    const QString name = m_items.at(itemIndex).name;
    dismiss();
    QTextCursor cursor = m_editor->textCursor(); cursor.beginEditBlock();
    cursor.setPosition(start); cursor.setPosition(end, QTextCursor::KeepAnchor); cursor.insertText(name);
    cursor.endEditBlock(); m_editor->setTextCursor(cursor); m_editor->ensureCursorVisible();
}

bool CodeCompletion::handleKeyPress(QKeyEvent *event)
{
    const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if (event->key() == Qt::Key_Space && modifiers == Qt::ControlModifier) { request(); return true; }
    if (m_completer->popup()->isVisible()) {
        if (event->key() == Qt::Key_Escape) { dismiss(); return true; }
        if (modifiers == Qt::NoModifier && (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
            if (m_timer->isActive()) { m_timer->stop(); updatePopup(false); }
            if (m_completer->popup()->isVisible()) { accept(m_completer->popup()->currentIndex()); return true; }
            return false;
        }
    }
    const bool typing = !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        && event->text().size() == 1 && completionWordCharacter(event->text().at(0));
    const bool deleting = modifiers == Qt::NoModifier && m_completer->popup()->isVisible()
        && (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete);
    if (typing || deleting) m_timer->start();
    else if (event->key() != Qt::Key_Shift) dismiss();
    return false;
}

bool CodeCompletion::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_completer->popup() && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        const auto modifiers = key->modifiers() & ~Qt::KeypadModifier;
        if (key->key() == Qt::Key_Delete && modifiers == Qt::ShiftModifier) {
            dismiss(); m_editor->deleteLines(); event->accept(); return true;
        }
        if ((key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)
            && (modifiers == Qt::AltModifier || modifiers == (Qt::AltModifier | Qt::ShiftModifier))) {
            // QCompleter otherwise consumes arrow keys as list navigation,
            // regardless of Alt/Shift. These shortcuts belong to the source.
            dismiss();
            if (modifiers & Qt::ShiftModifier) m_editor->duplicateLines(key->key() == Qt::Key_Down);
            else m_editor->moveLines(key->key() == Qt::Key_Down);
            event->accept(); return true;
        }
    }
    if (watched == m_completer->popup() && event->type() == QEvent::Hide) m_timer->stop();
    if (watched == m_completer->popup() && event->type() == QEvent::KeyPress && m_timer->isActive()) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
            m_timer->stop(); updatePopup(false);
        }
    }
    if ((watched == m_editor || watched == m_completer->popup()) && event->type() == QEvent::ShortcutOverride) {
        const auto *key = static_cast<QKeyEvent *>(event);
        const auto modifiers = key->modifiers() & ~Qt::KeypadModifier;
        if (key->key() == Qt::Key_Delete && modifiers == Qt::ShiftModifier) { event->accept(); return true; }
        if ((key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)
            && (modifiers == Qt::AltModifier || modifiers == (Qt::AltModifier | Qt::ShiftModifier))) {
            event->accept(); return true;
        }
        if ((key->key() == Qt::Key_Space && key->modifiers() == Qt::ControlModifier)
            || (m_completer->popup()->isVisible() && (key->key() == Qt::Key_Escape || key->key() == Qt::Key_Tab
                || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter))) { event->accept(); return true; }
    }
    if ((watched == m_editor || watched == m_editor->viewport())
        && (event->type() == QEvent::Hide || event->type() == QEvent::MouseButtonPress)) dismiss();
    return QObject::eventFilter(watched, event);
}
