#include "gmlconstant.h"
#include <cmath>
#include <limits>

static qint64 signedBits(quint64 bits)
{
    const quint64 maximum = quint64(std::numeric_limits<qint64>::max());
    return bits <= maximum ? qint64(bits) : -qint64(~bits) - 1;
}

static GmlConstant realConstant(double number, bool isBoolean = false)
{
    GmlConstant value;
    value.real = number;
    value.isBoolean = isBoolean;
    return value;
}

static GmlConstant integerConstant(qint64 number)
{
    GmlConstant value;
    value.isInteger = true;
    value.integer = number;
    return value;
}

static qint64 integerOperand(const GmlConstant &value)
{
    if (value.isInteger)
        return value.integer;
    const double number = std::trunc(value.real);
    // INT64_MAX rounds up to 2^63 as a double, so the upper bound is exclusive.
    if (!std::isfinite(number) || number < -9223372036854775808.0 || number >= 9223372036854775808.0)
        throw CompileError(QStringLiteral("Constant operand is outside the 64-bit integer range."));
    return qint64(number);
}

static qint32 int32Operand(const GmlConstant &value)
{
    const qint64 number = integerOperand(value);
    if (number < std::numeric_limits<qint32>::min() || number > std::numeric_limits<qint32>::max())
        throw CompileError(QStringLiteral("Constant operand is outside the 32-bit integer range."));
    return qint32(number);
}

static bool booleanOperand(const GmlConstant &value)
{
    return value.isInteger ? value.integer >= 1 : value.real >= 0.5;
}

static GmlConstant bitwiseConstant(quint64 bits, bool isInteger)
{
    const qint64 number = signedBits(bits);
    return isInteger ? integerConstant(number) : realConstant(double(number));
}

static quint64 shiftRight(quint64 bits, unsigned count)
{
    if (!count)
        return bits;
    const quint64 sign = quint64(1) << 63;
    return (bits >> count) | ((bits & sign) ? (~quint64(0) << (64 - count)) : 0);
}

bool evaluateGmlConstant(const GmlNodePtr &node, const GmlEnvironment &environment, GmlConstant &value)
{
    if (!node)
        return false;
    if (node->kind == GmlNodeKind::Number) {
        bool valid = false;
        if (node->isIntegerConstant) {
            value = integerConstant(node->text.toLongLong(&valid));
        } else if (node->text.startsWith(QLatin1Char('$')) || node->text.startsWith("0x", Qt::CaseInsensitive)) {
            const int prefix = node->text.startsWith(QLatin1Char('$')) ? 1 : 2;
            const qint64 number = signedBits(node->text.mid(prefix).toULongLong(&valid, 16));
            value = number < std::numeric_limits<qint32>::min() || number > std::numeric_limits<qint32>::max()
                ? integerConstant(number)
                : realConstant(double(number));
        } else {
            value = realConstant(node->text.toDouble(&valid));
        }
        value.isBoolean = node->isBooleanConstant;
        return valid;
    }
    if (node->kind == GmlNodeKind::Name || node->kind == GmlNodeKind::Member) {
        QString name = node->text;
        if (node->kind == GmlNodeKind::Member) {
            if (node->children.size() != 1 || node->children.first()->kind != GmlNodeKind::Name)
                return false;
            name = node->children.first()->text + QLatin1Char('.') + name;
        }
        const auto found = environment.constants.constFind(name);
        if (found == environment.constants.cend())
            return false;
        value = realConstant(found.value());
        return true;
    }
    if (node->kind == GmlNodeKind::Unary && node->children.size() == 1) {
        GmlConstant operand;
        if (!evaluateGmlConstant(node->children.first(), environment, operand))
            return false;
        if (node->text == QLatin1String("+")) {
            value = operand;
        } else if (node->text == QLatin1String("-")) {
            value = operand.isInteger ? integerConstant(signedBits(quint64(0) - quint64(operand.integer)))
                                      : realConstant(-operand.real);
        } else if (node->text == QLatin1String("~")) {
            value = bitwiseConstant(~quint64(integerOperand(operand)), operand.isInteger);
        } else if (node->text == QLatin1String("!") || node->text == QLatin1String("not")) {
            value = operand.isInteger ? integerConstant(!booleanOperand(operand))
                                      : realConstant(!booleanOperand(operand), operand.isBoolean);
        } else {
            return false;
        }
        // Unary folding copies the official constant value without its IsBool marker.
        value.isBoolean = false;
        return true;
    }
    if (node->kind != GmlNodeKind::Binary || node->children.size() != 2)
        return false;
    GmlConstant left, right;
    if (!evaluateGmlConstant(node->children.at(0), environment, left)
        || !evaluateGmlConstant(node->children.at(1), environment, right))
        return false;
    const QString &op = node->text;
    const bool integer = left.isInteger || right.isInteger;
    if (op == QLatin1String("&&") || op == QLatin1String("and")) {
        value = realConstant(booleanOperand(left) && booleanOperand(right));
    } else if (op == QLatin1String("||") || op == QLatin1String("or")) {
        value = realConstant(booleanOperand(left) || booleanOperand(right));
    } else if (op == QLatin1String("^^") || op == QLatin1String("xor")) {
        value = realConstant(booleanOperand(left) != booleanOperand(right));
    } else if (op == QLatin1String("=") || op == QLatin1String("==") || op == QLatin1String("!=")
        || op == QLatin1String("<>") || op == QLatin1String("<") || op == QLatin1String("<=")
        || op == QLatin1String(">") || op == QLatin1String(">=")) {
        // The official constant folder compares numeric constants exactly.
        const bool less = integer ? integerOperand(left) < integerOperand(right) : left.real < right.real;
        const bool equal = integer ? integerOperand(left) == integerOperand(right) : left.real == right.real;
        const bool greater = integer ? integerOperand(left) > integerOperand(right) : left.real > right.real;
        bool result;
        if (op == QLatin1String("=") || op == QLatin1String("=="))
            result = equal;
        else if (op == QLatin1String("!=") || op == QLatin1String("<>"))
            result = !equal;
        else if (op == QLatin1String("<"))
            result = less;
        else if (op == QLatin1String("<="))
            result = less || equal;
        else if (op == QLatin1String(">"))
            result = greater;
        else
            result = greater || equal;
        value = realConstant(result, true);
    } else if (op == QLatin1String("&") || op == QLatin1String("|") || op == QLatin1String("^")) {
        const quint64 a = quint64(integerOperand(left)), b = quint64(integerOperand(right));
        value = bitwiseConstant(op == QLatin1String("&") ? a & b : op == QLatin1String("|") ? a | b : a ^ b,
            integer);
    } else if (op == QLatin1String("<<") || op == QLatin1String(">>")) {
        if (!left.isInteger && right.isInteger) {
            // This mixed case is explicitly a 32-bit shift in the official folder.
            quint32 bits = quint32(int32Operand(left));
            const unsigned count = unsigned(quint64(right.integer) & 31);
            if (op == QLatin1String("<<"))
                bits <<= count;
            else if (count)
                bits = (bits >> count) | ((bits & (quint32(1) << 31)) ? (~quint32(0) << (32 - count)) : 0);
            const qint64 result = bits <= quint32(std::numeric_limits<qint32>::max())
                ? qint64(bits)
                : qint64(bits) - 4294967296LL;
            value = realConstant(double(result));
        } else {
            const quint64 bits = quint64(integerOperand(left));
            const unsigned count = right.isInteger ? unsigned(quint64(right.integer) & 63)
                                                   : unsigned(quint32(int32Operand(right)) & 63);
            value = bitwiseConstant(op == QLatin1String("<<") ? bits << count : shiftRight(bits, count), integer);
        }
    } else if (op == QLatin1String("+") || op == QLatin1String("-") || op == QLatin1String("*")) {
        if (integer) {
            const quint64 a = quint64(integerOperand(left)), b = quint64(integerOperand(right));
            value = integerConstant(signedBits(op == QLatin1String("+") ? a + b : op == QLatin1String("-") ? a - b
                                                                                                           : a * b));
        } else {
            value = realConstant(op == QLatin1String("+") ? left.real + right.real
                : op == QLatin1String("-")               ? left.real - right.real
                                                         : left.real * right.real);
        }
    } else if (op == QLatin1String("/") || op == QLatin1String("div") || op == QLatin1String("mod")
        || op == QLatin1String("%")) {
        const bool modulo = op == QLatin1String("mod") || op == QLatin1String("%");
        if (integer || op == QLatin1String("div")) {
            const qint64 a = integer ? integerOperand(left) : int32Operand(left);
            const qint64 b = integer ? integerOperand(right) : int32Operand(right);
            if (!b)
                throw CompileError(QStringLiteral("Division by zero in constant expression."));
            const qint64 minimum = integer ? std::numeric_limits<qint64>::min() : std::numeric_limits<qint32>::min();
            if (a == minimum && b == -1) {
                if (!modulo)
                    throw CompileError(QStringLiteral("Integer division overflow in constant expression."));
                value = integer ? integerConstant(0) : realConstant(0);
            } else {
                const qint64 result = modulo ? a % b : a / b;
                value = integer ? integerConstant(result) : realConstant(double(result));
            }
        } else {
            if (right.real == 0 || (modulo && int32Operand(right) == 0))
                throw CompileError(QStringLiteral("Division by zero in constant expression."));
            value = realConstant(modulo ? std::fmod(left.real, right.real) : left.real / right.real);
        }
    } else {
        return false;
    }
    return true;
}
