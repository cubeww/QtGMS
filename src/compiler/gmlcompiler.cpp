#include "gmlcompiler.h"
#include "gmlparser.h"
#include "gmlconstant.h"
#include <cmath>
#include <QRegularExpression>

static int integerType(qint64 value)
{
    return value >= -2147483647 - 1 && value <= 2147483647 ? 2 : 3;
}

static int numberType(double value)
{
    if (value == std::floor(value) && value >= -9223372036854775808.0 && value < 9223372036854775808.0)
        return integerType(qint64(value));
    return 0;
}

static int binaryResultType(int left, int right)
{
    // VM16 retains the wider operand, and the lower type code breaks size ties.
    static const int TypeSizes[] = { 8, 4, 4, 8, 4, 16, 4 };
    return TypeSizes[left] == TypeSizes[right] ? qMin(left, right)
        : TypeSizes[left] > TypeSizes[right] ? left : right;
}

struct VmLoop
{
    QVector<int> breaks, continues;
    int environmentDepth = 0;
    bool acceptsContinue = true;
    bool hasEnvironment = false;
    int stackType = -1;
};
struct VmLocation
{
    QString name;
    int scope = -1;
    bool array = false;
    bool stacked = false;
};

static int bitwiseOpcode(const QString &op)
{
    if (op.size() == 1) {
        switch (op.at(0).unicode()) {
        case '&':
            return 14;
        case '|':
            return 15;
        case '^':
            return 16;
        }
    }
    if (op == QLatin1String("<<"))
        return 19;
    if (op == QLatin1String(">>"))
        return 20;
    return 0;
}

class GmlEmitter
{
public:
    GmlEmitter(VmCode &code, DataWriter &file, const GmlEnvironment &environment)
        : m_code(code)
        , m_file(file)
        , m_environment(environment)
        , m_globals(environment.globalVariables)
    {
    }
    void compile(const GmlNodePtr &root)
    {
        addLocal(QStringLiteral("arguments"));
        const auto lowered = lowerAccessors(root);
        declarations(lowered);
        statement(lowered);
        instruction(0x9d, 2);
    }

private:
    VmCode &m_code;
    DataWriter &m_file;
    const GmlEnvironment &m_environment;
    QSet<QString> m_globals;
    QHash<const GmlNode *, int> m_expressionTypes;
    QVector<VmLoop> m_loops;
    int m_environmentDepth = 0;
    DataWriter &out() { return m_code.bytecode; }
    void fail(const GmlNodePtr &node, const QString &message) const
    {
        throw CompileError(QStringLiteral("%1:%2: %3").arg(m_code.name).arg(node->line).arg(message));
    }
    void instruction(int opcode, int types = 0, quint16 extra = 0)
    {
        out().u32((quint32(opcode) << 24) | (quint32(types) << 16) | extra);
    }
    void convert(int from, int to)
    {
        if (from != to)
            instruction(7, from | (to << 4));
    }
    int integer(qint64 value, bool forceLong = false)
    {
        if (!forceLong && value >= -32768 && value <= 32767)
            instruction(0x84, 15, quint16(qint16(value)));
        else if (!forceLong && value >= -2147483647 - 1 && value <= 2147483647) {
            instruction(0xc0, 2);
            out().u32(value);
        } else {
            instruction(0xc0, 3);
            out().u64(quint64(value));
            convert(3, 5);
            return 3;
        }
        convert(2, 5);
        return 2;
    }
    int number(double value)
    {
        // The upper bound is exclusive: double(INT64_MAX) rounds up to 2^63.
        if (value == std::floor(value) && value >= -9223372036854775808.0 && value < 9223372036854775808.0)
            return integer(qint64(value));
        instruction(0xc0, 0);
        out().f64(value);
        convert(0, 5);
        return 0;
    }
    int jump(int opcode = 0xb6)
    {
        int offset = out().position();
        instruction(opcode);
        return offset;
    }
    void target(int offset, int address)
    {
        out().patch(offset, (out().at(offset) & 0xff000000u) | (quint32((address - offset) / 4) & 0x7fffffu));
    }
    void target(int offset) { target(offset, out().position()); }
    void branch(int address, int opcode = 0xb6) { target(jump(opcode), address); }
    void reference(const QString &name, int scope, bool function, quint32 flags, int opcode, int types, quint16 extra)
    {
        VmReference reference;
        reference.name = name;
        reference.scope = scope;
        reference.function = function;
        reference.offset = out().position();
        m_code.references.append(reference);
        instruction(opcode, types, extra);
        out().u32(flags | quint32(m_file.stringId(name)));
    }
    void variable(const QString &name, int scope, bool store = false)
    {
        reference(name, scope == -7 || scope == -5 ? scope : -1, false, 0xa0000000u,
            store ? 0x45
                  : (scope == -7                                                                 ? 0xc1
                            : scope == -5                                                        ? 0xc2
                            : scope == -1 && m_environment.builtInGlobalVariables.contains(name) ? 0xc3
                                                                                                 : 0xc0),
            store ? 0x55 : 5, quint16(scope));
    }
    void addLocal(const QString &name)
    {
        m_code.localIds.insert(name, m_code.locals.size());
        m_code.locals.append(name);
    }
    void declarations(const GmlNodePtr &node)
    {
        if (node->kind == GmlNodeKind::Declaration)
            for (const auto &variable : node->children) {
                if (node->text == QLatin1String("globalvar"))
                    m_globals.insert(variable->text);
                else if (!m_code.localIds.contains(variable->text))
                    addLocal(variable->text);
            }
        for (const auto &child : node->children)
            declarations(child);
    }
    int scope(const QString &name) const
    {
        return m_code.localIds.contains(name) ? -7 : m_globals.contains(name) ? -5 : -1;
    }
    bool isAccessor(const GmlNodePtr &node) const
    {
        return node->kind == GmlNodeKind::Index
            && (node->text == QLatin1String("|") || node->text == QLatin1String("?") || node->text == QLatin1String("#")
                || node->text == QLatin1String("@"));
    }
    bool isIncrement(const GmlNodePtr &node) const
    {
        return (node->kind == GmlNodeKind::Unary || node->kind == GmlNodeKind::Postfix)
            && (node->text == QLatin1String("++") || node->text == QLatin1String("--"));
    }
    GmlNodePtr accessorIndexValue(const GmlNodePtr &node)
    {
        // Official accessor lowering removes pre/post updates from the getter's
        // indices only. The setter retains the original index expressions.
        if (isIncrement(node))
            return node->children.first();
        auto result = node;
        for (int i = 0; i < node->children.size(); ++i) {
            const auto child = accessorIndexValue(node->children.at(i));
            if (child != node->children.at(i)) {
                if (result == node)
                    result = GmlNodePtr::create(*node);
                result->children[i] = child;
            }
        }
        return result;
    }
    GmlNodePtr accessorFunction(const GmlNodePtr &node, bool store)
    {
        const bool array = node->text == QLatin1String("@");
        const int count = array ? node->children.size() : node->text == QLatin1String("#") ? 3 : 2;
        if (node->children.size() != count || (array && count != 2 && count != 3))
            fail(node, QStringLiteral("Incorrect number of accessor indices"));
        auto result = GmlNodePtr::create(*node);
        result->kind = GmlNodeKind::Call;
        if (array) {
            result->text = store ? QStringLiteral("array_set") : QStringLiteral("array_get");
            if (count == 3)
                result->text += QStringLiteral("_2D");
            return result;
        }
        result->text = node->text == QLatin1String("|") ? (store ? "ds_list_set" : "ds_list_find_value")
            : node->text == QLatin1String("?")          ? (store ? "ds_map_set" : "ds_map_find_value")
                                                        : (store ? "ds_grid_set" : "ds_grid_get");
        return result;
    }
    GmlNodePtr lowerAccessors(const GmlNodePtr &node, bool destination = false)
    {
        const bool update = isIncrement(node);
        const bool assignment = node->kind == GmlNodeKind::Assign;
        auto result = node;
        for (int i = 0; i < node->children.size(); ++i) {
            const auto child = lowerAccessors(node->children.at(i), i == 0 && (update || assignment));
            if (child != node->children.at(i)) {
                if (result == node)
                    result = GmlNodePtr::create(*node);
                result->children[i] = child;
            }
        }
        if (isAccessor(result))
            return destination ? result : accessorFunction(result, false);
        if (!(update || assignment) || !isAccessor(result->children.first()))
            return result;

        const auto &left = result->children.first();
        auto setter = accessorFunction(left, true);
        if (assignment && (result->text == QLatin1String("=") || result->text == QLatin1String(":="))) {
            setter->children.append(result->children.at(1));
            return setter;
        }
        auto getter = accessorFunction(left, false);
        // Keep the container and ordinary index expressions in both calls,
        // including their evaluation order, as the official lowering does.
        for (int i = 1; i < getter->children.size(); ++i)
            getter->children[i] = accessorIndexValue(getter->children.at(i));
        auto value = GmlNodePtr::create();
        value->kind = GmlNodeKind::Binary;
        value->line = result->line;
        value->children.append(getter);
        if (update) {
            auto one = GmlNodePtr::create();
            one->kind = GmlNodeKind::Number;
            one->line = result->line;
            one->text = QStringLiteral("1");
            value->children.append(one);
            value->text = result->text == QLatin1String("++") ? "+" : "-";
            setter->text += result->kind == GmlNodeKind::Postfix ? "_post" : "_pre";
        } else {
            value->children.append(result->children.at(1));
            value->text = result->text.left(result->text.size() - 1);
        }
        setter->children.append(value);
        return setter;
    }
    VmLocation location(const GmlNodePtr &node)
    {
        VmLocation result;
        auto base = node;
        if (node->kind == GmlNodeKind::Index) {
            base = node->children.first();
            result.array = true;
        }
        if (base->kind == GmlNodeKind::Name) {
            result.name = base->text;
            result.scope = scope(result.name);
        } else if (base->kind == GmlNodeKind::Member) {
            result.name = base->text;
            const auto owner = base->children.first();
            if (owner->kind == GmlNodeKind::Name && owner->text == QLatin1String("global"))
                result.scope = -5;
            else if (owner->kind == GmlNodeKind::Name && owner->text == QLatin1String("self"))
                result.scope = -1;
            else if (owner->kind == GmlNodeKind::Name && owner->text == QLatin1String("other"))
                result.scope = -2;
            else {
                expression(owner);
                convert(5, 2);
                result.stacked = true;
            }
        } else
            fail(node, QStringLiteral("Expected an assignable variable"));
        if (result.array) {
            if (!result.stacked)
                instruction(0x84, 15, quint16(result.scope));
            result.stacked = true;
            expression(node->children.at(1));
            convert(5, 2);
            if (node->children.size() == 3) {
                instruction(0xff, 15, 65535);
                instruction(0xc0, 2);
                out().u32(32000);
                instruction(8, 0x22);
                expression(node->children.at(2));
                convert(5, 2);
                instruction(0xff, 15, 65535);
                instruction(12, 0x22);
            }
        }
        return result;
    }
    void duplicateLocation(const VmLocation &place)
    {
        // A dynamic member carries one int; an array carries owner + index.
        if (place.stacked)
            instruction(0x86, 2, place.array ? 1 : 0);
    }
    void duplicateResult(const VmLocation &place)
    {
        instruction(0x86, 5);
        // VM16 pop-swap moves the expression result below the retained address,
        // leaving the other copy on top for the update/store (GML2VM pre/post).
        if (place.stacked)
            instruction(0x45, 0x5f, place.array ? 6 : 5);
    }
    void access(const VmLocation &place, bool store = false, bool valueOnTop = false)
    {
        if (store && place.scope != -7 && place.scope != -5 && m_environment.readOnlyVariables.contains(place.name))
            throw CompileError(m_code.name + ": cannot assign to read-only variable " + place.name);
        if (!place.stacked) {
            variable(place.name, place.scope, store);
            return;
        }
        reference(place.name, place.scope == -5 || place.scope == -7 ? place.scope : -1, false,
            place.array ? 0 : 0x80000000u, store ? 0x45 : 0xc0, store ? (valueOnTop ? 0x52 : 0x55) : 5, 0);
    }
    void operation(const QString &op, int leftType = 5, int rightType = 5)
    {
        static const QHash<QString, int> compare
            = { { "<", 1 }, { "<=", 2 }, { "==", 3 }, { "=", 3 }, { "!=", 4 }, { "<>", 4 }, { ">=", 5 }, { ">", 6 } };
        const auto comparison = compare.constFind(op);
        if (comparison != compare.cend()) {
            instruction(0x15, rightType | (leftType << 4), quint16(comparison.value() << 8));
            convert(4, 5);
            return;
        }
        static const QHash<QString, int> binary = { { "*", 8 }, { "/", 9 }, { "div", 10 }, { "mod", 11 }, { "%", 11 },
            { "+", 12 }, { "-", 13 }, { "&", 14 }, { "|", 15 }, { "^", 16 }, { "<<", 19 }, { ">>", 20 }, { "and", 14 },
            { "&&", 14 }, { "or", 15 }, { "||", 15 }, { "xor", 16 }, { "^^", 16 } };
        const auto operation = binary.constFind(op);
        if (operation == binary.cend())
            throw CompileError(QStringLiteral("Unknown operator: ") + op);
        instruction(operation.value(), rightType | (leftType << 4));
        convert(binaryResultType(leftType, rightType), 5);
    }
    int arithmeticOperandType(const QString &op, int type) const
    {
        if (op == QLatin1String("/"))
            return type == 5 ? 5 : 0;
        return type == 4 ? 2 : type;
    }
    QPair<int, int> binaryOperandTypes(const GmlNodePtr &node)
    {
        int left = arithmeticOperandType(node->text, expressionType(node->children.at(0)));
        int right = arithmeticOperandType(node->text, expressionType(node->children.at(1)));
        return qMakePair(left, right);
    }
    int expressionType(const GmlNodePtr &node)
    {
        const auto found = m_expressionTypes.constFind(node.data());
        if (found != m_expressionTypes.cend())
            return found.value();
        int type = 5;
        if (node->kind == GmlNodeKind::Number) {
            GmlConstant value;
            if (!evaluateGmlConstant(node, m_environment, value))
                fail(node, QStringLiteral("Invalid numeric literal"));
            type = value.isBoolean ? 4 : value.isInteger ? 3 : numberType(value.real);
        } else if (node->kind == GmlNodeKind::Name || node->kind == GmlNodeKind::Member) {
            GmlConstant value;
            if (evaluateGmlConstant(node, m_environment, value))
                type = numberType(value.real);
        } else if (node->kind == GmlNodeKind::Binary) {
            if (bitwiseOpcode(node->text)) {
                type = 3;
            } else if (node->text == QLatin1String("+") || node->text == QLatin1String("-")
                || node->text == QLatin1String("*") || node->text == QLatin1String("/")
                || node->text == QLatin1String("div") || node->text == QLatin1String("mod")
                || node->text == QLatin1String("%")) {
                const auto operands = binaryOperandTypes(node);
                type = binaryResultType(operands.first, operands.second);
            } else {
                type = 4;
            }
        } else if (node->kind == GmlNodeKind::Unary) {
            const int operand = expressionType(node->children.first());
            if (node->text == QLatin1String("~"))
                type = operand == 3 ? 3 : 2;
            else if (node->text == QLatin1String("!") || node->text == QLatin1String("not"))
                type = 4;
            else if (node->text == QLatin1String("+"))
                type = operand;
            else if (node->text == QLatin1String("-"))
                type = operand == 4 ? 2 : operand;
        }
        m_expressionTypes.insert(node.data(), type);
        return type;
    }
    void checkAssignment(const GmlNodePtr &node) const
    {
        const QString name = node->kind == GmlNodeKind::Name ? node->text
            : node->kind == GmlNodeKind::Member && node->children.first()->kind == GmlNodeKind::Name
            ? node->children.first()->text + "." + node->text
            : QString();
        if (m_environment.constants.contains(name))
            fail(node, QStringLiteral("Cannot assign to a constant: ") + name);
    }
    void increment(const GmlNodePtr &node, bool keepResult)
    {
        checkAssignment(node->children.first());
        const auto place = location(node->children.first());
        duplicateLocation(place);
        access(place);
        if (keepResult && node->kind == GmlNodeKind::Postfix)
            duplicateResult(place);
        number(1);
        convert(5, 2);
        operation(node->text == QLatin1String("++") ? "+" : "-", 5, 2);
        if (keepResult && node->kind == GmlNodeKind::Unary)
            duplicateResult(place);
        access(place, true, true);
    }
    void expression(const GmlNodePtr &node)
    {
        if (node->kind == GmlNodeKind::Number) {
            GmlConstant value;
            if (!evaluateGmlConstant(node, m_environment, value))
                fail(node, QStringLiteral("Invalid numeric literal"));
            if (value.isBoolean) {
                instruction(0x84, 15, value.real >= 0.5 ? 1 : 0);
                convert(4, 5);
            } else if (value.isInteger) {
                integer(value.integer, true);
            } else {
                number(value.real);
            }
        } else if (node->kind == GmlNodeKind::String) {
            instruction(0xc0, 6);
            out().u32(m_file.stringId(node->text));
            convert(6, 5);
        } else if (node->kind == GmlNodeKind::Name
            && (node->text == QLatin1String("self") || node->text == QLatin1String("other"))) {
            // Match GML2VM: an instance value must survive leaving a with block.
            // Keep the scope selectors in location() for self.member/other.member.
            variable(QStringLiteral("id"), node->text == QLatin1String("self") ? -1 : -2);
        } else if (node->kind == GmlNodeKind::Name || node->kind == GmlNodeKind::Member
            || node->kind == GmlNodeKind::Index) {
            auto constant = m_environment.constants.cend();
            if (node->kind == GmlNodeKind::Name)
                constant = m_environment.constants.constFind(node->text);
            else if (node->kind == GmlNodeKind::Member && node->children.first()->kind == GmlNodeKind::Name)
                constant
                    = m_environment.constants.constFind(node->children.first()->text + QLatin1Char('.') + node->text);
            if (constant != m_environment.constants.cend())
                number(constant.value());
            else
                access(location(node));
        } else if (node->kind == GmlNodeKind::Call || node->kind == GmlNodeKind::ArrayLiteral) {
            // Official array literals call the Runner's internal constructor.
            const bool arrayLiteral = node->kind == GmlNodeKind::ArrayLiteral;
            const QString function = arrayLiteral ? QStringLiteral("@@NewGMLArray@@")
                                                  : m_environment.extensionFunctionNames.value(node->text, node->text);
            if (!arrayLiteral && !m_environment.functions.contains(function))
                fail(node, QStringLiteral("Unknown function or script: ") + node->text);
            const int expected = m_environment.functionArguments.value(function, -1);
            if (expected >= 0 && expected != node->children.size())
                fail(node,
                    QStringLiteral("%1 expects %2 arguments, got %3")
                        .arg(node->text)
                        .arg(expected)
                        .arg(node->children.size()));
            // VM16 stores the argument count in 16 bits. The official compiler
            // imposes no blanket 16-argument limit on scripts or variadic calls.
            if (node->children.size() > 65535)
                fail(node, arrayLiteral ? QStringLiteral("Array literal exceeds the VM16 element count limit")
                                        : QStringLiteral("Function call exceeds the VM16 argument count limit"));
            for (int i = node->children.size() - 1; i >= 0; --i)
                expression(node->children.at(i));
            reference(function, -1, true, 0, 0xd9, 2, quint16(node->children.size()));
        } else if (node->kind == GmlNodeKind::Conditional) {
            expression(node->children.at(0));
            convert(5, 4);
            int otherwise = jump(0xb8);
            expression(node->children.at(1));
            int end = jump();
            target(otherwise);
            expression(node->children.at(2));
            target(end);
        } else if (node->kind == GmlNodeKind::Binary) {
            const bool andOp = node->text == QLatin1String("&&") || node->text == QLatin1String("and"),
                       orOp = node->text == QLatin1String("||") || node->text == QLatin1String("or");
            const bool xorOp = node->text == QLatin1String("^^") || node->text == QLatin1String("xor");
            if ((andOp || orOp) && m_environment.shortCircuit) {
                expression(node->children.at(0));
                convert(5, 4);
                int shortcut = jump(andOp ? 0xb8 : 0xb7);
                expression(node->children.at(1));
                convert(5, 4);
                convert(4, 5);
                int end = jump();
                target(shortcut);
                number(orOp ? 1 : 0);
                target(end);
            } else if (andOp || orOp || xorOp) {
                expression(node->children.at(0));
                convert(5, 4);
                expression(node->children.at(1));
                convert(5, 4);
                instruction(andOp ? 14 : orOp ? 15 : 16, 0x44);
                convert(4, 5);
            } else if (const int opcode = bitwiseOpcode(node->text)) {
                expression(node->children.at(0));
                convert(5, 3);
                expression(node->children.at(1));
                convert(5, 3);
                instruction(opcode, 0x33);
                convert(3, 5);
            } else {
                const auto types = binaryOperandTypes(node);
                expression(node->children.at(0));
                convert(5, types.first);
                expression(node->children.at(1));
                convert(5, types.second);
                operation(node->text, types.first, types.second);
            }
        } else if (node->kind == GmlNodeKind::Unary || node->kind == GmlNodeKind::Postfix) {
            if (node->text == QLatin1String("++") || node->text == QLatin1String("--")) {
                increment(node, true);
            } else {
                expression(node->children.first());
                if (node->text == QLatin1String("!") || node->text == QLatin1String("not")) {
                    convert(5, 4);
                    instruction(18, 4);
                    convert(4, 5);
                } else if (node->text == QLatin1String("~")) {
                    const int type = expressionType(node->children.first()) == 3 ? 3 : 2;
                    convert(5, type);
                    instruction(18, type);
                    convert(type, 5);
                } else if (node->text == QLatin1String("-")) {
                    if (expressionType(node->children.first()) == 4) {
                        convert(5, 2);
                        convert(2, 5);
                    }
                    instruction(17, 5);
                }
            }
        } else
            fail(node, QStringLiteral("Unsupported expression: ") + QLatin1String(gmlNodeKindName(node->kind)));
    }
    void closeLoop(int continueAt, int breakAt)
    {
        const VmLoop loop = m_loops.takeLast();
        for (int offset : loop.breaks)
            target(offset, breakAt);
        for (int offset : loop.continues)
            target(offset, continueAt);
    }
    void cleanupEnvironments(int depth)
    {
        for (int i = depth; i < m_environmentDepth; ++i)
            instruction(0xbb, 0xf0);
    }
    void cleanupLoopValues(int first)
    {
        for (int i = m_loops.size() - 1; i >= first; --i)
            if (m_loops.at(i).stackType >= 0)
                instruction(0x9e, m_loops.at(i).stackType);
    }
    void statement(const GmlNodePtr &node)
    {
        const GmlNodeKind kind = node->kind;
        if (kind == GmlNodeKind::Enum)
            return;
        if (kind == GmlNodeKind::Block) {
            for (const auto &child : node->children)
                statement(child);
        } else if (kind == GmlNodeKind::Declaration) {
            for (const auto &variableNode : node->children)
                if (!variableNode->children.isEmpty()) {
                    expression(variableNode->children.first());
                    variable(variableNode->text, scope(variableNode->text), true);
                }
        } else if (kind == GmlNodeKind::Assign) {
            const auto &left = node->children.first();
            checkAssignment(left);
            const bool compound = node->text != QLatin1String("=") && node->text != QLatin1String(":=");
            // Plain assignment evaluates the RHS before its destination, as
            // in official GML2VM. Compound assignment retains the address.
            if (!compound) {
                expression(node->children.at(1));
                access(location(left), true);
                return;
            }
            const auto place = location(left);
            duplicateLocation(place);
            const bool bitwise = node->text == QLatin1String("&=") || node->text == QLatin1String("|=")
                || node->text == QLatin1String("^=");
            access(place);
            expression(node->children.at(1));
            int type = expressionType(node->children.at(1));
            if (bitwise) {
                if (type != 2 && type != 3)
                    type = 2;
            } else if (type == 4) {
                type = 2;
            }
            // Compound assignment keeps a Variable left operand; unlike a
            // binary / expression, /= retains the right operand's numeric type.
            convert(5, type);
            operation(node->text.left(1), 5, type);
            access(place, true, true);
        } else if (kind == GmlNodeKind::If) {
            expression(node->children.at(0));
            convert(5, 4);
            int otherwise = jump(0xb8);
            statement(node->children.at(1));
            if (node->children.size() == 3) {
                int end = jump();
                target(otherwise);
                statement(node->children.at(2));
                target(end);
            } else
                target(otherwise);
        } else if (kind == GmlNodeKind::While || kind == GmlNodeKind::For || kind == GmlNodeKind::Do
            || kind == GmlNodeKind::Repeat) {
            if (kind == GmlNodeKind::For)
                statement(node->children.at(0));
            if (kind == GmlNodeKind::Repeat) {
                expression(node->children.at(0));
                convert(5, 2);
            }
            int start = out().position(), end = -1;
            if (kind != GmlNodeKind::Do) {
                if (kind == GmlNodeKind::Repeat) {
                    instruction(0x86, 2);
                    instruction(0x84, 15, 0);
                    // Enter only while the remaining repeat count is positive.
                    instruction(0x15, 0x22, 6 << 8);
                } else {
                    expression(node->children.at(kind == GmlNodeKind::For ? 1 : 0));
                    convert(5, 4);
                }
                end = jump(0xb8);
            }
            VmLoop loop;
            loop.environmentDepth = m_environmentDepth;
            loop.stackType = kind == GmlNodeKind::Repeat ? 2 : -1;
            m_loops.append(loop);
            statement(node->children.at(kind == GmlNodeKind::For ? 3 : kind == GmlNodeKind::Do ? 0 : 1));
            int continueAt = out().position();
            if (kind == GmlNodeKind::For)
                statement(node->children.at(2));
            if (kind == GmlNodeKind::Repeat) {
                instruction(0x84, 15, 1);
                instruction(13, 0x22);
            }
            if (kind == GmlNodeKind::Do) {
                expression(node->children.at(1));
                convert(5, 4);
                branch(start, 0xb8);
            } else
                branch(start);
            if (end >= 0)
                target(end);
            closeLoop(continueAt, out().position());
            if (kind == GmlNodeKind::Repeat)
                instruction(0x9e, 2);
        } else if (kind == GmlNodeKind::With) {
            expression(node->children.at(0));
            convert(5, 2);
            int empty = jump(0xba);
            const int start = out().position();
            ++m_environmentDepth;
            VmLoop loop;
            loop.environmentDepth = m_environmentDepth;
            loop.hasEnvironment = true;
            m_loops.append(loop);
            statement(node->children.at(1));
            const int continueAt = out().position();
            target(empty, continueAt);
            branch(start, 0xbb);
            int finish = jump();
            const int breakAt = out().position();
            instruction(0xbb, 0xf0);
            target(finish);
            closeLoop(continueAt, breakAt);
            --m_environmentDepth;
        } else if (kind == GmlNodeKind::Switch) {
            expression(node->children.first());
            QVector<int> patches(node->children.size(), -1);
            int defaultIndex = -1;
            for (int i = 1; i < node->children.size(); ++i) {
                if (node->children.at(i)->kind == GmlNodeKind::Case) {
                    instruction(0x86, 5);
                    expression(node->children.at(i)->children.first());
                    operation("==");
                    convert(5, 4);
                    patches[i] = jump(0xb7);
                } else if (node->children.at(i)->kind == GmlNodeKind::Default) {
                    if (defaultIndex >= 0)
                        fail(node, QStringLiteral("Duplicate default label"));
                    defaultIndex = i;
                }
            }
            int noMatch = jump();
            VmLoop loop;
            loop.acceptsContinue = false;
            loop.environmentDepth = m_environmentDepth;
            loop.stackType = 5;
            m_loops.append(loop);
            for (int i = 1; i < node->children.size(); ++i) {
                if (patches.at(i) >= 0)
                    target(patches.at(i));
                else if (i == defaultIndex)
                    target(noMatch);
                else
                    statement(node->children.at(i));
            }
            if (defaultIndex < 0)
                target(noMatch);
            closeLoop(out().position(), out().position());
            instruction(0x9e, 5);
        } else if (kind == GmlNodeKind::Break || kind == GmlNodeKind::Continue) {
            int index = m_loops.size() - 1;
            if (kind == GmlNodeKind::Continue)
                while (index >= 0 && !m_loops.at(index).acceptsContinue)
                    --index;
            if (index < 0)
                fail(node, QStringLiteral("Loop control statement outside a loop"));
            // The destination loop retains its own value until its exit.
            // Continue can leave intervening switches, whose values must go.
            cleanupLoopValues(index + 1);
            cleanupEnvironments(m_loops.at(index).environmentDepth);
            if (kind == GmlNodeKind::Break)
                m_loops[index].breaks.append(jump());
            else
                m_loops[index].continues.append(jump());
        } else if (kind == GmlNodeKind::Return || kind == GmlNodeKind::Exit) {
            const bool returnsValue = kind == GmlNodeKind::Return && !node->children.isEmpty();
            bool retainedValues = m_environmentDepth > 0;
            for (const auto &loop : m_loops)
                retainedValues = retainedValues || loop.stackType >= 0;
            if (returnsValue) {
                expression(node->children.first());
                if (retainedValues) {
                    const QString returnLocal = QStringLiteral("$$$$temp$$$$");
                    if (!m_code.localIds.contains(returnLocal))
                        addLocal(returnLocal);
                    variable(returnLocal, -7, true);
                }
            }
            // Unwind nested with environments and retained loop/switch values
            // in reverse nesting order, just like the official return path.
            for (int i = m_loops.size() - 1; i >= 0; --i) {
                const auto &loop = m_loops.at(i);
                if (loop.hasEnvironment)
                    instruction(0xbb, 0xf0);
                if (loop.stackType >= 0)
                    instruction(0x9e, loop.stackType);
            }
            if (returnsValue && retainedValues)
                variable(QStringLiteral("$$$$temp$$$$"), -7);
            instruction(returnsValue ? 0x9c : 0x9d, returnsValue ? 5 : 2);
        } else if ((kind == GmlNodeKind::Unary || kind == GmlNodeKind::Postfix)
            && (node->text == QLatin1String("++") || node->text == QLatin1String("--"))) {
            increment(node, false);
        } else {
            expression(node);
            instruction(0x9e, 5);
        }
    }
};

void compileGml(VmCode &code, DataWriter &file, const GmlEnvironment &environment)
{
    GmlEmitter(code, file, environment).compile(code.syntax);
}

void prepareGml(QVector<VmCode> &codes, GmlEnvironment &environment,
    const std::function<void(const QString &)> &includeFunction)
{
    const QRegularExpression macro(QStringLiteral("^\\s*#macro\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+(.+)$"));
    const auto preprocess = [&](VmCode &code) {
        // Most code blocks have no directives; avoid splitting and rebuilding them.
        if (!code.source.contains(QLatin1Char('#')))
            return;
        QStringList lines = code.source.split('\n');
        bool inBlockComment = false;
        QChar stringQuote;
        for (auto &line : lines) {
            if (!inBlockComment && stringQuote.isNull() && line.contains(QLatin1Char('#'))) {
                auto match = macro.match(line);
                if (match.hasMatch()) {
                    environment.macros.insert(match.captured(1), match.captured(2));
                    line.clear();
                    continue;
                }
                if (line.trimmed().startsWith("#region") || line.trimmed().startsWith("#endregion")) {
                    line.clear();
                    continue;
                }
            }
            // Directives inside multiline strings or comments are ordinary text.
            // GMS 1.4 strings are literal, so backslashes do not escape quotes.
            for (int i = 0; i < line.size(); ++i) {
                const QChar character = line.at(i);
                const QChar next = i + 1 < line.size() ? line.at(i + 1) : QChar();
                if (!stringQuote.isNull()) {
                    if (character == stringQuote)
                        stringQuote = QChar();
                } else if (inBlockComment) {
                    if (character == QLatin1Char('*') && next == QLatin1Char('/')) {
                        inBlockComment = false;
                        ++i;
                    }
                } else if (character == QLatin1Char('/') && next == QLatin1Char('/')) {
                    break;
                } else if (character == QLatin1Char('/') && next == QLatin1Char('*')) {
                    inBlockComment = true;
                    ++i;
                } else if (character == QLatin1Char('\"') || character == QLatin1Char('\'')) {
                    stringQuote = character;
                }
            }
        }
        code.source = lines.join("\n");
    };
    std::function<void(const GmlNodePtr &)> collect = [&](const GmlNodePtr &node) {
        if (node->kind == GmlNodeKind::Call)
            includeFunction(node->text);
        if (node->kind == GmlNodeKind::Declaration && node->text == QStringLiteral("globalvar"))
            for (const auto &child : node->children)
                environment.globalVariables.insert(child->text);
        if (node->kind == GmlNodeKind::Enum) {
            double value = 0;
            for (const auto &child : node->children) {
                if (!child->children.isEmpty()) {
                    GmlConstant constant;
                    if (!evaluateGmlConstant(child->children.first(), environment, constant))
                        throw CompileError(QStringLiteral("Enum value is not a supported constant expression: ")
                            + node->text + QLatin1Char('.') + child->text);
                    value = constant.isInteger ? double(constant.integer) : std::trunc(constant.real);
                }
                if (!std::isfinite(value) || value < -2147483648.0 || value > 2147483647.0)
                    throw CompileError(QStringLiteral("Enum value is outside the 32-bit integer range: ")
                        + node->text + QLatin1Char('.') + child->text);
                environment.constants.insert(node->text + "." + child->text, value++);
            }
        }
        for (const auto &child : node->children)
            collect(child);
    };
    int preparedCodeCount = 0;
    for (int i = 0; i < codes.size(); ++i) {
        // Calls can append whole extension files. Process their directives
        // before parsing them, and keep no QVector references across imports.
        while (preparedCodeCount < codes.size())
            preprocess(codes[preparedCodeCount++]);
        const auto syntax = parseGml(codes.at(i).source, codes.at(i).name, environment);
        codes[i].syntax = syntax;
        collect(syntax);
    }
    std::function<void(GmlNodePtr &)> fold = [&](GmlNodePtr &node) {
        for (auto &child : node->children)
            fold(child);
        if (node->kind != GmlNodeKind::Binary && node->kind != GmlNodeKind::Unary)
            return;
        GmlConstant value;
        if (!evaluateGmlConstant(node, environment, value))
            return;
        // Fold expression nodes only. Assignable names/members/indices must
        // retain their identity for address generation and constant checks.
        auto constant = GmlNodePtr::create();
        constant->kind = GmlNodeKind::Number;
        constant->line = node->line;
        constant->isIntegerConstant = value.isInteger;
        constant->isBooleanConstant = value.isBoolean;
        constant->text = value.isInteger ? QString::number(value.integer) : QString::number(value.real, 'g', 17);
        node = constant;
    };
    for (auto &code : codes)
        fold(code.syntax);
}

void writeVmChunks(DataWriter &file, QVector<VmCode> &codes, const GmlEnvironment &environment)
{
    file.chunk("CODE", [&] {
        file.u32(codes.size());
        int table = file.position();
        for (int i = 0; i < codes.size(); ++i)
            file.u32(0);
        for (auto &code : codes) {
            code.address = file.position();
            file.append(code.bytecode.bytes());
        }
        for (int i = 0; i < codes.size(); ++i) {
            const auto &code = codes.at(i);
            file.patch(table + i * 4, file.position());
            file.string(code.name);
            file.u32(code.bytecode.position());
            file.u16(code.locals.size());
            file.u16(code.locals.isEmpty() ? 0x8000 : 0);
            file.u32(code.address - file.position());
            file.u32(0);
        }
    });
    // CODE now owns the serialized instructions. Linking only needs their
    // file addresses, references and local-variable metadata.
    for (auto &code : codes)
        code.bytecode = DataWriter();
    struct Symbol
    {
        QString name;
        int scope = -1;
        int id = 0;
        QVector<int> addresses;
    };
    QVector<Symbol> variables, functions;
    QHash<QString, int> variableIds, functionIds;
    int nextVariable = 0, maxLocals = 0;
    for (const QString &name : { QStringLiteral("prototype"), QStringLiteral("@@array@@") }) {
        Symbol symbol;
        symbol.name = name;
        symbol.id = nextVariable++;
        variableIds.insert(name + "|-1", variables.size());
        variables.append(symbol);
    }
    for (int c = 0; c < codes.size(); ++c) {
        const auto &code = codes.at(c);
        maxLocals = qMax(maxLocals, code.locals.size());
        for (int local = 0; local < code.locals.size(); ++local) {
            Symbol symbol;
            symbol.name = code.locals.at(local);
            symbol.scope = -7;
            symbol.id = local;
            variableIds.insert(symbol.name + "|-7|" + QString::number(c), variables.size());
            variables.append(symbol);
        }
        for (const auto &reference : code.references) {
            QString key = reference.name + QLatin1Char('|') + QString::number(reference.scope);
            if (reference.scope == -7)
                key += QLatin1Char('|') + QString::number(c);
            auto &ids = reference.function ? functionIds : variableIds;
            auto &symbols = reference.function ? functions : variables;
            if (!ids.contains(key)) {
                Symbol symbol;
                symbol.name = reference.name;
                symbol.scope = reference.scope;
                if (!reference.function)
                    symbol.id = reference.scope == -7 ? code.localIds.value(reference.name, -1)
                        : reference.scope == -1 && environment.builtInVariables.contains(reference.name)
                        ? -6
                        : nextVariable++;
                ids.insert(key, symbols.size());
                symbols.append(symbol);
            }
            symbols[ids.value(key)].addresses.append(code.address + reference.offset);
        }
    }
    const auto chain = [&](const Symbol &symbol) {
        for (int i = 0; i + 1 < symbol.addresses.size(); ++i) {
            int offset = symbol.addresses.at(i) + 4;
            file.patch(
                offset, (file.at(offset) & 0xf0000000u) | quint32(symbol.addresses.at(i + 1) - symbol.addresses.at(i)));
        }
    };
    file.chunk("VARI", [&] {
        file.u32(nextVariable);
        file.u32(nextVariable);
        file.u32(maxLocals);
        for (const auto &symbol : variables) {
            file.string(symbol.name);
            file.u32(symbol.scope);
            file.u32(symbol.id);
            file.u32(symbol.addresses.size());
            file.u32(symbol.addresses.isEmpty() ? -1 : symbol.addresses.first());
            chain(symbol);
        }
    });
    file.chunk("FUNC", [&] {
        file.u32(functions.size());
        for (const auto &symbol : functions) {
            file.string(symbol.name);
            file.u32(symbol.addresses.size());
            file.u32(symbol.addresses.first());
            chain(symbol);
        }
        file.u32(codes.size());
        for (const auto &code : codes) {
            file.u32(code.locals.size());
            file.string(code.name);
            for (int i = 0; i < code.locals.size(); ++i) {
                file.u32(i);
                file.string(code.locals.at(i));
            }
        }
    });
}
