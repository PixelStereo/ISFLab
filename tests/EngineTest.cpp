// Engine logic that needs no GL context: pass size expressions and Material generators.
#include "DimensionExpression.h"
#include "MaterialGenerators.h"
#include <cmath>

namespace {
void require(bool condition, const char *message) {
    if (!condition) qFatal("%s", message);
}
bool near(double a, double b) { return std::abs(a - b) < 1e-9; }
}

int main() {
    const QHash<QString, double> variables{{"WIDTH", 1920}, {"HEIGHT", 1080}, {"scale", 0.5}};
    require(near(evaluateDimension("$WIDTH / 2", variables), 960), "Division of a variable");
    require(near(evaluateDimension("floor($HEIGHT * $scale + 0.7)", variables), 540), "floor() and precedence");
    require(near(evaluateDimension("max(16, min($WIDTH, 64)) - -2", variables), 66), "min, max and unary minus");
    require(near(evaluateDimension("(1 + 2) * 3", variables), 9), "Parentheses");
    require(near(evaluateDimension("1.5e2", variables), 150), "Exponent notation");
    for (const char *invalid : {"", "$MISSING", "2 +", "sqrt(4)", "(1", "1 2", "max(1)"})
        require(std::isnan(evaluateDimension(invalid, variables)), "Invalid expressions return NaN");

    const QHash<QString, QVariant> values{{"speed", 2.0}, {"shape", 1}};
    const QHash<QString, qint64> events{{"up", 3}, {"down", 1}};
    const QHash<QString, QJsonArray> choices{{"shape", QJsonArray{"Smooth", "In"}}};
    MaterialGenerators generators;
    generators.add("scaled", "multiplier", {{"value1", "phase"}, {"value2", 10}});
    generators.add("phase", "time_base", {{"speed", "speed"}});
    generators.add("count", "incrementer", {{"increment", "up"}, {"decrement", "down"}});
    generators.add("ramp", "animator", {{"speed", 0.25}, {"shape", "shape"}});
    require(generators.order().isEmpty(), "No cycle");
    const auto outputs = generators.evaluate({values, events, choices, 1.5, 0.0});
    QHash<QString, double> result;
    QStringList order;
    for (const auto &[name, value] : outputs) {
        result.insert(name, value);
        order << name;
    }
    require(order.indexOf("phase") < order.indexOf("scaled"), "A generator follows the generators it reads");
    require(near(result.value("phase"), 3.0), "time_base: time × speed");
    require(near(result.value("scaled"), 30.0), "multiplier reads another generator");
    require(near(result.value("count"), 2.0), "incrementer counts events");
    require(near(result.value("ramp"), 0.375), "animator In shape is the phase of time × speed");

    MaterialGenerators cyclic;
    cyclic.add("a", "multiplier", {{"value1", "b"}});
    cyclic.add("b", "multiplier", {{"value1", "a"}});
    require(!cyclic.order().isEmpty(), "Cycles are reported");
    require(MaterialGenerators::isSupportedType("pass_thru") && !MaterialGenerators::isSupportedType("lfo"),
            "Supported generator types");
    return 0;
}
