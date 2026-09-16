#ifndef QTGMS_CODEBLOCKDATA_H
#define QTGMS_CODEBLOCKDATA_H

#include <QTextBlockUserData>
#include <QVector>
#include <QString>

// Lexical metadata shared by highlighting and editing; offsets are block-local.
class CodeBlockData : public QTextBlockUserData
{
public:
    struct Bracket { int position; QChar character; };
    struct Token { int position; int length; QChar kind; };
    struct ExcludedRange { int start; int end; bool includesStart; bool includesEnd; bool string; };
    QVector<Bracket> brackets;
    QVector<Token> tokens;
    QVector<ExcludedRange> excludedRanges;

    void exclude(int start, int end, bool includesStart, bool includesEnd, bool string = false)
    { excludedRanges.append({start, end, includesStart, includesEnd, string}); }

    bool isCodePosition(int position) const
    {
        for (const ExcludedRange &range : excludedRanges)
            if ((position > range.start && position < range.end)
                || (position == range.start && range.includesStart)
                || (position == range.end && range.includesEnd)) return false;
        return true;
    }

    void collectTokens(const QString &text)
    {
        int rangeIndex = 0;
        for (int i = 0; i < text.size(); ++i) {
            while (rangeIndex < excludedRanges.size() && i >= excludedRanges.at(rangeIndex).end) ++rangeIndex;
            if (rangeIndex < excludedRanges.size() && i >= excludedRanges.at(rangeIndex).start) {
                const auto &range = excludedRanges.at(rangeIndex);
                if (range.string) tokens.append({i, range.end - i, QLatin1Char('s')});
                i = range.end - 1; continue;
            }
            const QChar character = text.at(i);
            if (character.isSpace()) continue;
            if (QStringLiteral("()[]{}").contains(character)) brackets.append({i, character});
            if (character.isLetterOrNumber() || character == QLatin1Char('_')) {
                const int start = i;
                while (i + 1 < text.size() && (text.at(i + 1).isLetterOrNumber() || text.at(i + 1) == QLatin1Char('_'))) ++i;
                tokens.append({start, i - start + 1, character.isDigit() ? QLatin1Char('n') : QLatin1Char('i')});
            } else tokens.append({i, 1, character});
        }
    }
};

#endif
