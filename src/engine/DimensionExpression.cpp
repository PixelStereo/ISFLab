#include "DimensionExpression.h"
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
// Recursive descent parser: sum := product (('+'|'-') product)*, product := operand (('*'|'/') operand)*.
class DimensionExpression {
public:
    DimensionExpression(QString expression, const QHash<QString, double> &variables)
        : expression(std::move(expression)), variables(variables) {}

    double evaluate() {
        if (expression.size() > 512) return invalid;
        const double result = sum();
        skipSpace();
        return position == expression.size() ? result : invalid;
    }

private:
    static constexpr double invalid = std::numeric_limits<double>::quiet_NaN();

    void skipSpace() {
        while (position < expression.size() && expression.at(position).isSpace()) ++position;
    }

    bool consume(QChar character) {
        skipSpace();
        if (position == expression.size() || expression.at(position) != character) return false;
        ++position;
        return true;
    }

    double sum() {
        double result = product();
        for (;;) {
            if (consume('+')) result += product();
            else if (consume('-')) result -= product();
            else return result;
        }
    }

    double product() {
        double result = operand();
        for (;;) {
            if (consume('*')) result *= operand();
            else if (consume('/')) result /= operand();
            else return result;
        }
    }

    double operand() {
        if (consume('+')) return operand();
        if (consume('-')) return -operand();
        if (consume('(')) {
            const double result = sum();
            return consume(')') ? result : invalid;
        }

        const bool variable = consume('$');
        static const QRegularExpression token(
            R"rx([A-Za-z_]\w*|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)rx");
        const auto match = token.match(expression, position, QRegularExpression::NormalMatch,
                                       QRegularExpression::AnchorAtOffsetMatchOption);
        if (!match.hasMatch()) return invalid;
        position = match.capturedEnd();
        const auto name = match.captured();
        if (variable) return variables.value(name, invalid);

        bool numeric = false;
        const double value = name.toDouble(&numeric);
        if (numeric) return value;
        if (!consume('(')) return invalid;
        const double first = sum();
        if (name == "min" || name == "max") {
            if (!consume(',')) return invalid;
            const double second = sum();
            if (!consume(')') || std::isnan(first) || std::isnan(second)) return invalid;
            return name == "min" ? std::min(first, second) : std::max(first, second);
        }
        if (!consume(')')) return invalid;
        if (name == "floor") return std::floor(first);
        if (name == "ceil") return std::ceil(first);
        if (name == "round") return std::round(first);
        if (name == "abs") return std::abs(first);
        return invalid;
    }

    QString expression;
    const QHash<QString, double> &variables;
    qsizetype position = 0;
};
}

double evaluateDimension(const QString &expression, const QHash<QString, double> &variables) {
    return DimensionExpression(expression, variables).evaluate();
}
