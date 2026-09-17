#include "gmlparser.h"

enum class GmlTokenKind { EndOfInput, Number, String, Name, Symbol };

enum class GmlSymbol {
    None,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Semicolon,
    Comma,
    Dot,
    Question,
    Colon,
    Hash,
    At,
    Not,
    BitNot,
    Plus,
    Minus,
    Multiply,
    Divide,
    Modulo,
    BitOr,
    BitXor,
    BitAnd,
    Assign,
    Less,
    Greater,
    Equal,
    NotEqual,
    LessEqual,
    GreaterEqual,
    AlternateNotEqual,
    And,
    Or,
    Xor,
    ShiftLeft,
    ShiftRight,
    AddAssign,
    SubtractAssign,
    MultiplyAssign,
    DivideAssign,
    ModuloAssign,
    AndAssign,
    OrAssign,
    XorAssign,
    Increment,
    Decrement,
    AlternateAssign,
    LogicalOr,
    LogicalXor,
    LogicalAnd,
    LogicalNot,
    IntegerDivide,
    IntegerModulo,
    Var,
    GlobalVar,
    Enum,
    If,
    Then,
    Else,
    While,
    Repeat,
    With,
    Do,
    Until,
    For,
    Switch,
    Case,
    Default,
    Break,
    Continue,
    Exit,
    Return,
    Count,
};

struct GmlSymbolInfo
{
    QString text;
    int precedence;
    bool assignment;
};

// From lowest to highest, GMS orders logical operators as OR, AND, XOR.
// Comparisons share one level, as do bitwise operators, and associate left.
static const GmlSymbolInfo GmlSymbols[] = {
    { QStringLiteral(""), 0, false },
    { QStringLiteral("("), 0, false },
    { QStringLiteral(")"), 0, false },
    { QStringLiteral("{"), 0, false },
    { QStringLiteral("}"), 0, false },
    { QStringLiteral("["), 0, false },
    { QStringLiteral("]"), 0, false },
    { QStringLiteral(";"), 0, false },
    { QStringLiteral(","), 0, false },
    { QStringLiteral("."), 0, false },
    { QStringLiteral("?"), 0, false },
    { QStringLiteral(":"), 0, false },
    { QStringLiteral("#"), 0, false },
    { QStringLiteral("@"), 0, false },
    { QStringLiteral("!"), 0, false },
    { QStringLiteral("~"), 0, false },
    { QStringLiteral("+"), 7, false },
    { QStringLiteral("-"), 7, false },
    { QStringLiteral("*"), 8, false },
    { QStringLiteral("/"), 8, false },
    { QStringLiteral("%"), 8, false },
    { QStringLiteral("|"), 5, false },
    { QStringLiteral("^"), 5, false },
    { QStringLiteral("&"), 5, false },
    { QStringLiteral("="), 4, true },
    { QStringLiteral("<"), 4, false },
    { QStringLiteral(">"), 4, false },
    { QStringLiteral("=="), 4, false },
    { QStringLiteral("!="), 4, false },
    { QStringLiteral("<="), 4, false },
    { QStringLiteral(">="), 4, false },
    { QStringLiteral("<>"), 4, false },
    { QStringLiteral("&&"), 2, false },
    { QStringLiteral("||"), 1, false },
    { QStringLiteral("^^"), 3, false },
    { QStringLiteral("<<"), 6, false },
    { QStringLiteral(">>"), 6, false },
    { QStringLiteral("+="), 0, true },
    { QStringLiteral("-="), 0, true },
    { QStringLiteral("*="), 0, true },
    { QStringLiteral("/="), 0, true },
    { QStringLiteral("%="), 0, true },
    { QStringLiteral("&="), 0, true },
    { QStringLiteral("|="), 0, true },
    { QStringLiteral("^="), 0, true },
    { QStringLiteral("++"), 0, false },
    { QStringLiteral("--"), 0, false },
    { QStringLiteral(":="), 0, true },
    { QStringLiteral("or"), 1, false },
    { QStringLiteral("xor"), 3, false },
    { QStringLiteral("and"), 2, false },
    { QStringLiteral("not"), 0, false },
    { QStringLiteral("div"), 8, false },
    { QStringLiteral("mod"), 8, false },
    { QStringLiteral("var"), 0, false },
    { QStringLiteral("globalvar"), 0, false },
    { QStringLiteral("enum"), 0, false },
    { QStringLiteral("if"), 0, false },
    { QStringLiteral("then"), 0, false },
    { QStringLiteral("else"), 0, false },
    { QStringLiteral("while"), 0, false },
    { QStringLiteral("repeat"), 0, false },
    { QStringLiteral("with"), 0, false },
    { QStringLiteral("do"), 0, false },
    { QStringLiteral("until"), 0, false },
    { QStringLiteral("for"), 0, false },
    { QStringLiteral("switch"), 0, false },
    { QStringLiteral("case"), 0, false },
    { QStringLiteral("default"), 0, false },
    { QStringLiteral("break"), 0, false },
    { QStringLiteral("continue"), 0, false },
    { QStringLiteral("exit"), 0, false },
    { QStringLiteral("return"), 0, false },
};

static_assert(
    sizeof(GmlSymbols) / sizeof(GmlSymbols[0]) == int(GmlSymbol::Count), "Every GML symbol must have metadata");

static bool isReturnBoundary(GmlSymbol symbol)
{
    switch (symbol) {
    case GmlSymbol::Semicolon:
    case GmlSymbol::LeftBrace:
    case GmlSymbol::RightBrace:
    case GmlSymbol::Var:
    case GmlSymbol::GlobalVar:
    case GmlSymbol::If:
    case GmlSymbol::Then:
    case GmlSymbol::Else:
    case GmlSymbol::While:
    case GmlSymbol::Repeat:
    case GmlSymbol::With:
    case GmlSymbol::Do:
    case GmlSymbol::Until:
    case GmlSymbol::For:
    case GmlSymbol::Switch:
    case GmlSymbol::Case:
    case GmlSymbol::Default:
    case GmlSymbol::Break:
    case GmlSymbol::Continue:
    case GmlSymbol::Exit:
    case GmlSymbol::Return:
        return true;
    default:
        return false;
    }
}

static GmlSymbol keywordSymbol(const QString &text)
{
    static const QHash<QString, GmlSymbol> Keywords = {
        { QStringLiteral("or"), GmlSymbol::LogicalOr },
        { QStringLiteral("xor"), GmlSymbol::LogicalXor },
        { QStringLiteral("and"), GmlSymbol::LogicalAnd },
        { QStringLiteral("not"), GmlSymbol::LogicalNot },
        { QStringLiteral("div"), GmlSymbol::IntegerDivide },
        { QStringLiteral("mod"), GmlSymbol::IntegerModulo },
        { QStringLiteral("var"), GmlSymbol::Var },
        { QStringLiteral("globalvar"), GmlSymbol::GlobalVar },
        { QStringLiteral("enum"), GmlSymbol::Enum },
        { QStringLiteral("if"), GmlSymbol::If },
        { QStringLiteral("then"), GmlSymbol::Then },
        { QStringLiteral("else"), GmlSymbol::Else },
        { QStringLiteral("while"), GmlSymbol::While },
        { QStringLiteral("repeat"), GmlSymbol::Repeat },
        { QStringLiteral("with"), GmlSymbol::With },
        { QStringLiteral("do"), GmlSymbol::Do },
        { QStringLiteral("until"), GmlSymbol::Until },
        { QStringLiteral("for"), GmlSymbol::For },
        { QStringLiteral("switch"), GmlSymbol::Switch },
        { QStringLiteral("case"), GmlSymbol::Case },
        { QStringLiteral("default"), GmlSymbol::Default },
        { QStringLiteral("break"), GmlSymbol::Break },
        { QStringLiteral("continue"), GmlSymbol::Continue },
        { QStringLiteral("exit"), GmlSymbol::Exit },
        { QStringLiteral("return"), GmlSymbol::Return },
        { QStringLiteral("begin"), GmlSymbol::LeftBrace },
        { QStringLiteral("end"), GmlSymbol::RightBrace },
    };
    return Keywords.value(text, GmlSymbol::None);
}

static GmlSymbol operatorSymbol(ushort first, ushort second)
{
    switch ((uint(first) << 16) | second) {
    case (uint('=') << 16) | '=':
        return GmlSymbol::Equal;
    case (uint('!') << 16) | '=':
        return GmlSymbol::NotEqual;
    case (uint('<') << 16) | '=':
        return GmlSymbol::LessEqual;
    case (uint('>') << 16) | '=':
        return GmlSymbol::GreaterEqual;
    case (uint('<') << 16) | '>':
        return GmlSymbol::AlternateNotEqual;
    case (uint('&') << 16) | '&':
        return GmlSymbol::And;
    case (uint('|') << 16) | '|':
        return GmlSymbol::Or;
    case (uint('^') << 16) | '^':
        return GmlSymbol::Xor;
    case (uint('<') << 16) | '<':
        return GmlSymbol::ShiftLeft;
    case (uint('>') << 16) | '>':
        return GmlSymbol::ShiftRight;
    case (uint('+') << 16) | '=':
        return GmlSymbol::AddAssign;
    case (uint('-') << 16) | '=':
        return GmlSymbol::SubtractAssign;
    case (uint('*') << 16) | '=':
        return GmlSymbol::MultiplyAssign;
    case (uint('/') << 16) | '=':
        return GmlSymbol::DivideAssign;
    case (uint('%') << 16) | '=':
        return GmlSymbol::ModuloAssign;
    case (uint('&') << 16) | '=':
        return GmlSymbol::AndAssign;
    case (uint('|') << 16) | '=':
        return GmlSymbol::OrAssign;
    case (uint('^') << 16) | '=':
        return GmlSymbol::XorAssign;
    case (uint('+') << 16) | '+':
        return GmlSymbol::Increment;
    case (uint('-') << 16) | '-':
        return GmlSymbol::Decrement;
    case (uint(':') << 16) | '=':
        return GmlSymbol::AlternateAssign;
    default:
        return GmlSymbol::None;
    }
}

static GmlSymbol operatorSymbol(ushort character)
{
    switch (character) {
    case '(':
        return GmlSymbol::LeftParen;
    case ')':
        return GmlSymbol::RightParen;
    case '{':
        return GmlSymbol::LeftBrace;
    case '}':
        return GmlSymbol::RightBrace;
    case '[':
        return GmlSymbol::LeftBracket;
    case ']':
        return GmlSymbol::RightBracket;
    case ';':
        return GmlSymbol::Semicolon;
    case ',':
        return GmlSymbol::Comma;
    case '.':
        return GmlSymbol::Dot;
    case '?':
        return GmlSymbol::Question;
    case ':':
        return GmlSymbol::Colon;
    case '#':
        return GmlSymbol::Hash;
    case '@':
        return GmlSymbol::At;
    case '!':
        return GmlSymbol::Not;
    case '~':
        return GmlSymbol::BitNot;
    case '+':
        return GmlSymbol::Plus;
    case '-':
        return GmlSymbol::Minus;
    case '*':
        return GmlSymbol::Multiply;
    case '/':
        return GmlSymbol::Divide;
    case '%':
        return GmlSymbol::Modulo;
    case '|':
        return GmlSymbol::BitOr;
    case '^':
        return GmlSymbol::BitXor;
    case '&':
        return GmlSymbol::BitAnd;
    case '=':
        return GmlSymbol::Assign;
    case '<':
        return GmlSymbol::Less;
    case '>':
        return GmlSymbol::Greater;
    default:
        return GmlSymbol::None;
    }
}

struct GmlToken
{
    QString text;
    GmlTokenKind kind = GmlTokenKind::EndOfInput;
    GmlSymbol symbol = GmlSymbol::None;
    int line = 1;
};

class GmlParser
{
public:
    GmlParser(const QString &source, const QString &name, const GmlEnvironment &environment)
        : m_name(name)
        , m_environment(environment)
    {
        m_tokens.reserve(qMin(source.size() / 4 + 1, 65536));
        lex(source, 1, QSet<QString>());
        GmlToken end;
        end.kind = GmlTokenKind::EndOfInput;
        end.line = m_tokens.isEmpty() ? 1 : m_tokens.last().line;
        m_tokens.append(end);
    }
    GmlNodePtr parse()
    {
        auto root = node(GmlNodeKind::Block);
        while (!isEnd())
            root->children.append(statement());
        return root;
    }

private:
    QString m_name;
    const GmlEnvironment &m_environment;
    QVector<GmlToken> m_tokens;
    int m_index = 0;
    int m_depth = 0;
    void fail(const QString &message, int line) const
    {
        throw CompileError(QStringLiteral("%1:%2: %3").arg(m_name).arg(line).arg(message));
    }
    void lex(const QString &source, int line, QSet<QString> expanding)
    {
        int i = 0;
        while (i < source.size()) {
            const QChar c = source.at(i);
            const QChar next = i + 1 < source.size() ? source.at(i + 1) : QChar();
            if (c.isSpace()) {
                if (c == QLatin1Char('\n'))
                    ++line;
                ++i;
                continue;
            }
            if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
                while (i < source.size() && source.at(i) != QLatin1Char('\n'))
                    ++i;
                continue;
            }
            if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
                i += 2;
                while (i < source.size()
                    && !(source.at(i) == QLatin1Char('*') && i + 1 < source.size()
                        && source.at(i + 1) == QLatin1Char('/'))) {
                    if (source.at(i++) == QLatin1Char('\n'))
                        ++line;
                }
                // GMS 1.4 accepts a trailing block comment through end of
                // source, commonly used to disable an old script body.
                if (i == source.size())
                    return;
                i += 2;
                continue;
            }
            GmlToken token;
            token.line = line;
            const int start = i;
            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                token.kind = GmlTokenKind::String;
                ++i;
                // GMS 1.4 strings are literal: backslashes are not C escapes.
                while (i < source.size() && source.at(i) != c) {
                    if (source.at(i) == QLatin1Char('\n'))
                        ++line;
                    ++i;
                }
                if (i == source.size())
                    fail(QStringLiteral("Unterminated string"), token.line);
                token.text = source.mid(start + 1, i - start - 1);
                ++i;
            } else if (c.isDigit() || c == QLatin1Char('$')
                || (c == QLatin1Char('.') && i + 1 < source.size() && source.at(i + 1).isDigit())) {
                token.kind = GmlTokenKind::Number;
                if (c == QLatin1Char('$')
                    || (c == QLatin1Char('0') && (next == QLatin1Char('x') || next == QLatin1Char('X')))) {
                    i += c == QLatin1Char('$') ? 1 : 2;
                    while (i < source.size()
                        && (source.at(i).isDigit()
                            || (source.at(i) >= QLatin1Char('a') && source.at(i) <= QLatin1Char('f'))
                            || (source.at(i) >= QLatin1Char('A') && source.at(i) <= QLatin1Char('F'))))
                        ++i;
                } else {
                    while (i < source.size() && source.at(i).isDigit())
                        ++i;
                    if (i < source.size() && source.at(i) == QLatin1Char('.')) {
                        ++i;
                        while (i < source.size() && source.at(i).isDigit())
                            ++i;
                    }
                    if (i < source.size() && source.at(i).toLower() == QLatin1Char('e')) {
                        ++i;
                        if (i < source.size() && (source.at(i) == QLatin1Char('+') || source.at(i) == QLatin1Char('-')))
                            ++i;
                        while (i < source.size() && source.at(i).isDigit())
                            ++i;
                    }
                }
                token.text = source.mid(start, i - start);
            } else if (c.isLetter() || c == QLatin1Char('_')) {
                token.kind = GmlTokenKind::Name;
                while (i < source.size() && (source.at(i).isLetterOrNumber() || source.at(i) == QLatin1Char('_')))
                    ++i;
                token.text = source.mid(start, i - start);
                const auto macro = m_environment.macros.constFind(token.text);
                if (macro != m_environment.macros.cend()) {
                    if (expanding.contains(token.text) || expanding.size() >= 64)
                        fail(QStringLiteral("Recursive macro: ") + token.text, line);
                    auto nested = expanding;
                    nested.insert(token.text);
                    lex(macro.value(), line, nested);
                    continue;
                }
                token.symbol = keywordSymbol(token.text);
                if (token.symbol == GmlSymbol::LeftBrace || token.symbol == GmlSymbol::RightBrace)
                    token.text = GmlSymbols[int(token.symbol)].text;
            } else {
                token.kind = GmlTokenKind::Symbol;
                token.symbol = operatorSymbol(c.unicode(), next.unicode());
                if (token.symbol != GmlSymbol::None)
                    i += 2;
                else {
                    token.symbol = operatorSymbol(c.unicode());
                    ++i;
                }
                token.text = token.symbol == GmlSymbol::None ? QString(c) : GmlSymbols[int(token.symbol)].text;
            }
            m_tokens.append(token);
        }
    }
    const GmlToken &current() const { return m_tokens.at(m_index); }
    bool isEnd() const { return current().kind == GmlTokenKind::EndOfInput; }
    bool at(GmlSymbol symbol) const { return current().symbol == symbol; }
    bool take(GmlSymbol symbol)
    {
        if (!at(symbol))
            return false;
        ++m_index;
        return true;
    }
    void expect(GmlSymbol symbol)
    {
        if (!take(symbol))
            fail(QStringLiteral("Expected '%1', found '%2'").arg(GmlSymbols[int(symbol)].text, current().text),
                current().line);
    }
    GmlNodePtr node(GmlNodeKind kind, const QString &text = QString()) const
    {
        GmlNodePtr result = GmlNodePtr::create();
        result->kind = kind;
        result->text = text;
        result->line = current().line;
        return result;
    }
    GmlNodePtr expression(int minimum = 1)
    {
        if (++m_depth > 256)
            fail(QStringLiteral("Expression nesting is too deep"), current().line);
        auto left = prefix();
        while (GmlSymbols[int(current().symbol)].precedence >= minimum) {
            const QString op = current().text;
            const int level = GmlSymbols[int(current().symbol)].precedence;
            auto result = node(GmlNodeKind::Binary, op);
            ++m_index;
            result->children.append(left);
            result->children.append(expression(level + 1));
            left = result;
        }
        if (minimum == 1 && take(GmlSymbol::Question)) {
            auto result = node(GmlNodeKind::Conditional);
            result->children.append(left);
            result->children.append(expression());
            expect(GmlSymbol::Colon);
            result->children.append(expression());
            left = result;
        }
        --m_depth;
        return left;
    }
    GmlNodePtr prefix()
    {
        if (at(GmlSymbol::Not) || at(GmlSymbol::LogicalNot) || at(GmlSymbol::BitNot) || at(GmlSymbol::Minus)
            || at(GmlSymbol::Plus) || at(GmlSymbol::Increment) || at(GmlSymbol::Decrement)) {
            auto result = node(GmlNodeKind::Unary, current().text);
            ++m_index;
            result->children.append(prefix());
            return result;
        }
        GmlNodePtr result;
        bool canIncrement = false;
        if (take(GmlSymbol::LeftParen)) {
            result = expression();
            expect(GmlSymbol::RightParen);
        } else if (at(GmlSymbol::LeftBracket)) {
            result = node(GmlNodeKind::ArrayLiteral);
            ++m_index;
            while (!at(GmlSymbol::RightBracket) && !isEnd()) {
                result->children.append(expression());
                if (!take(GmlSymbol::Comma))
                    break;
            }
            expect(GmlSymbol::RightBracket);
        } else if (current().kind == GmlTokenKind::Number || current().kind == GmlTokenKind::String
            || current().kind == GmlTokenKind::Name) {
            result = node(current().kind == GmlTokenKind::Number ? GmlNodeKind::Number
                    : current().kind == GmlTokenKind::String     ? GmlNodeKind::String
                                                                 : GmlNodeKind::Name,
                current().text);
            canIncrement = result->kind == GmlNodeKind::Name;
            ++m_index;
        } else
            fail(QStringLiteral("Expected expression, found '%1'").arg(current().text), current().line);
        for (;;) {
            if (take(GmlSymbol::LeftParen)) {
                if (result->kind != GmlNodeKind::Name)
                    fail(QStringLiteral("GMS 1.4 requires a function name"), result->line);
                result->kind = GmlNodeKind::Call;
                canIncrement = false;
                // GMS permits a trailing comma without adding another argument.
                while (!at(GmlSymbol::RightParen) && !isEnd()) {
                    result->children.append(expression());
                    if (!take(GmlSymbol::Comma))
                        break;
                }
                expect(GmlSymbol::RightParen);
            } else if (take(GmlSymbol::Dot)) {
                if (current().kind != GmlTokenKind::Name)
                    fail(QStringLiteral("Expected member name"), current().line);
                auto member = node(GmlNodeKind::Member, current().text);
                ++m_index;
                member->children.append(result);
                result = member;
                canIncrement = true;
            } else if (take(GmlSymbol::LeftBracket)) {
                auto index = node(GmlNodeKind::Index);
                index->children.append(result);
                if (at(GmlSymbol::BitOr) || at(GmlSymbol::Question) || at(GmlSymbol::Hash) || at(GmlSymbol::At)) {
                    index->text = current().text;
                    ++m_index;
                }
                index->children.append(expression());
                if (take(GmlSymbol::Comma))
                    index->children.append(expression());
                expect(GmlSymbol::RightBracket);
                result = index;
                canIncrement = true;
            } else if (canIncrement && (at(GmlSymbol::Increment) || at(GmlSymbol::Decrement))) {
                // Official GML only attaches postfix updates to variable access,
                // not grouped expressions or calls: if (condition) ++counter;
                // starts a new prefix-update statement after the closing ')'.
                auto post = node(GmlNodeKind::Postfix, current().text);
                ++m_index;
                post->children.append(result);
                result = post;
                canIncrement = false;
            } else
                break;
        }
        return result;
    }
    GmlNodePtr assignment()
    {
        if (at(GmlSymbol::Var) || at(GmlSymbol::GlobalVar)) {
            auto result = node(GmlNodeKind::Declaration, current().text);
            ++m_index;
            // GMS 1.4 permits empty declarations such as "var;".
            if (at(GmlSymbol::Semicolon) || at(GmlSymbol::RightBrace) || isEnd())
                return result;
            do {
                if (current().kind != GmlTokenKind::Name)
                    fail(QStringLiteral("Expected variable name"), current().line);
                auto variable = node(GmlNodeKind::Name, current().text);
                ++m_index;
                if (take(GmlSymbol::Assign) || take(GmlSymbol::AlternateAssign))
                    variable->children.append(expression());
                result->children.append(variable);
            } while (take(GmlSymbol::Comma));
            return result;
        }
        auto left = prefix();
        if (GmlSymbols[int(current().symbol)].assignment) {
            auto result = node(GmlNodeKind::Assign, current().text);
            ++m_index;
            result->children.append(left);
            result->children.append(expression());
            return result;
        }
        if (left->kind != GmlNodeKind::Call && left->kind != GmlNodeKind::Postfix
            && !(left->kind == GmlNodeKind::Unary
                && (left->text == QStringLiteral("++") || left->text == QStringLiteral("--"))))
            fail(QStringLiteral("Expected assignment or function call"), left->line);
        return left;
    }
    GmlNodePtr statement()
    {
        if (++m_depth > 256)
            fail(QStringLiteral("Statement nesting is too deep"), current().line);
        auto result = node(GmlNodeKind::Block);
        if (take(GmlSymbol::Semicolon)) {
        } else if (take(GmlSymbol::LeftBrace)) {
            while (!at(GmlSymbol::RightBrace) && !isEnd())
                result->children.append(statement());
            expect(GmlSymbol::RightBrace);
        } else if (take(GmlSymbol::Enum)) {
            if (current().kind != GmlTokenKind::Name)
                fail(QStringLiteral("Expected enum name"), current().line);
            result->kind = GmlNodeKind::Enum;
            result->text = current().text;
            ++m_index;
            expect(GmlSymbol::LeftBrace);
            while (!at(GmlSymbol::RightBrace)) {
                if (current().kind != GmlTokenKind::Name)
                    fail(QStringLiteral("Expected enum member"), current().line);
                auto member = node(GmlNodeKind::Name, current().text);
                ++m_index;
                if (take(GmlSymbol::Assign))
                    member->children.append(expression());
                result->children.append(member);
                if (!take(GmlSymbol::Comma))
                    break;
            }
            expect(GmlSymbol::RightBrace);
            take(GmlSymbol::Semicolon);
        } else if (take(GmlSymbol::If)) {
            result->kind = GmlNodeKind::If;
            result->children.append(expression());
            take(GmlSymbol::Then);
            result->children.append(statement());
            if (take(GmlSymbol::Else))
                result->children.append(statement());
        } else if (at(GmlSymbol::While) || at(GmlSymbol::Repeat) || at(GmlSymbol::With)) {
            result->kind = at(GmlSymbol::While) ? GmlNodeKind::While
                : at(GmlSymbol::Repeat)         ? GmlNodeKind::Repeat
                                                : GmlNodeKind::With;
            ++m_index;
            result->children.append(expression());
            take(GmlSymbol::Do);
            result->children.append(statement());
        } else if (take(GmlSymbol::Do)) {
            result->kind = GmlNodeKind::Do;
            result->children.append(statement());
            expect(GmlSymbol::Until);
            result->children.append(expression());
            take(GmlSymbol::Semicolon);
        } else if (take(GmlSymbol::For)) {
            result->kind = GmlNodeKind::For;
            expect(GmlSymbol::LeftParen);
            result->children.append(at(GmlSymbol::Semicolon) ? node(GmlNodeKind::Block) : assignment());
            expect(GmlSymbol::Semicolon);
            result->children.append(
                at(GmlSymbol::Semicolon) ? node(GmlNodeKind::Number, QStringLiteral("1")) : expression());
            // GMS requires the initializer separator but permits omitting the
            // separator after the condition: for (i = n; i > 0 i -= step).
            take(GmlSymbol::Semicolon);
            // GMS parses the update clause as a statement and accepts trailing
            // semicolons inside the parentheses: for (i = 0; i < n; i += 1;).
            result->children.append(at(GmlSymbol::RightParen) ? node(GmlNodeKind::Block) : statement());
            while (take(GmlSymbol::Semicolon)) {}
            expect(GmlSymbol::RightParen);
            result->children.append(statement());
        } else if (take(GmlSymbol::Switch)) {
            result->kind = GmlNodeKind::Switch;
            result->children.append(expression());
            expect(GmlSymbol::LeftBrace);
            while (!at(GmlSymbol::RightBrace) && !isEnd()) {
                if (take(GmlSymbol::Case)) {
                    auto label = node(GmlNodeKind::Case);
                    label->children.append(expression());
                    expect(GmlSymbol::Colon);
                    result->children.append(label);
                } else if (take(GmlSymbol::Default)) {
                    expect(GmlSymbol::Colon);
                    result->children.append(node(GmlNodeKind::Default));
                } else
                    result->children.append(statement());
            }
            expect(GmlSymbol::RightBrace);
        } else if (at(GmlSymbol::Break) || at(GmlSymbol::Continue) || at(GmlSymbol::Exit)) {
            result->kind = at(GmlSymbol::Break) ? GmlNodeKind::Break
                : at(GmlSymbol::Continue)       ? GmlNodeKind::Continue
                                                : GmlNodeKind::Exit;
            ++m_index;
            take(GmlSymbol::Semicolon);
        } else if (take(GmlSymbol::Return)) {
            result->kind = GmlNodeKind::Return;
            // Semicolons are optional; a following statement keyword must not
            // be consumed as a variable in the return expression.
            if (!isEnd() && !isReturnBoundary(current().symbol))
                result->children.append(expression());
            take(GmlSymbol::Semicolon);
        } else {
            result = assignment();
            take(GmlSymbol::Semicolon);
        }
        --m_depth;
        return result;
    }
};

GmlNodePtr parseGml(const QString &source, const QString &name, const GmlEnvironment &environment)
{
    return GmlParser(source, name, environment).parse();
}

const char *gmlNodeKindName(GmlNodeKind kind)
{
    switch (kind) {
    case GmlNodeKind::Block:
        return "block";
    case GmlNodeKind::Number:
        return "number";
    case GmlNodeKind::String:
        return "string";
    case GmlNodeKind::Name:
        return "name";
    case GmlNodeKind::Member:
        return "member";
    case GmlNodeKind::Index:
        return "index";
    case GmlNodeKind::ArrayLiteral:
        return "array literal";
    case GmlNodeKind::Call:
        return "call";
    case GmlNodeKind::Conditional:
        return "conditional";
    case GmlNodeKind::Binary:
        return "binary";
    case GmlNodeKind::Unary:
        return "unary";
    case GmlNodeKind::Postfix:
        return "postfix";
    case GmlNodeKind::Declaration:
        return "declaration";
    case GmlNodeKind::Assign:
        return "assign";
    case GmlNodeKind::Enum:
        return "enum";
    case GmlNodeKind::If:
        return "if";
    case GmlNodeKind::While:
        return "while";
    case GmlNodeKind::Repeat:
        return "repeat";
    case GmlNodeKind::With:
        return "with";
    case GmlNodeKind::Do:
        return "do";
    case GmlNodeKind::For:
        return "for";
    case GmlNodeKind::Switch:
        return "switch";
    case GmlNodeKind::Case:
        return "case";
    case GmlNodeKind::Default:
        return "default";
    case GmlNodeKind::Break:
        return "break";
    case GmlNodeKind::Continue:
        return "continue";
    case GmlNodeKind::Exit:
        return "exit";
    case GmlNodeKind::Return:
        return "return";
    }
    return "unknown";
}
