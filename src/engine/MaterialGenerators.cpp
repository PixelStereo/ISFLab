#include "MaterialGenerators.h"
#include <QSet>
#include <cmath>

bool MaterialGenerators::isSupportedType(const QString &type) {
    static const QStringList types{"time_base", "animator", "incrementer", "multiplier", "pass_thru"};
    return types.contains(type);
}

void MaterialGenerators::clear() {
    generators.clear();
    evaluationOrder.clear();
    resetPending = true;
}

void MaterialGenerators::add(const QString &name, const QString &type, const QJsonObject &parameters) {
    generators.insert(name, {parameters, type});
}

QString MaterialGenerators::order() {
    evaluationOrder.clear();
    QSet<QString> visiting, ordered;
    const auto visit = [&](const auto &self, const QString &name) -> bool {
        if (ordered.contains(name)) return true;
        if (visiting.contains(name)) return false;
        visiting.insert(name);
        for (const auto &parameter : generators.constFind(name)->parameters) {
            const auto dependency = parameter.toString();
            if (generators.contains(dependency) && !self(self, dependency)) return false;
        }
        visiting.remove(name);
        ordered.insert(name);
        evaluationOrder.append(name);
        return true;
    };
    for (auto it = generators.cbegin(); it != generators.cend(); ++it)
        if (!visit(visit, it.key())) return it.key();
    return {};
}

QList<QPair<QString, double>> MaterialGenerators::evaluate(const Inputs &inputs) {
    QHash<QString, double> generatorOutputs;
    QList<QPair<QString, double>> outputs;
    const auto &values = inputs.values;
    const double time = inputs.time, delta = inputs.delta;
    for (const auto &name : evaluationOrder) {
        auto it = generators.find(name);
        const auto publish = [&](double output) {
            generatorOutputs.insert(name, output);
            outputs.append({name, output});
        };
        const auto &parameters = it->parameters;
        const auto parameter = [&](const char *key, const QVariant &fallback) {
            const auto entry = parameters.value(key);
            if (entry.isString()) {
                const auto reference = entry.toString();
                if (generatorOutputs.contains(reference)) return QVariant(generatorOutputs.value(reference));
                return values.value(reference, fallback);
            }
            return entry.isUndefined() ? fallback : entry.toVariant();
        };
        if (it->type == "incrementer") {
            const auto increments = inputs.eventCounts.value(parameters.value("increment").toString());
            const auto decrements = inputs.eventCounts.value(parameters.value("decrement").toString());
            publish(double(increments - decrements));
            continue;
        }
        if (it->type == "multiplier") {
            publish(parameter("value1", 1.).toDouble() * parameter("value2", 1.).toDouble() *
                    parameter("value3", 1.).toDouble() * parameter("value4", 1.).toDouble());
            continue;
        }
        if (it->type == "pass_thru") {
            const auto channel = parameters.value("input_value").toString();
            if (channel == "/custom/BPM/bpm") publish(120.);
            else if (channel == "/custom/BPM/bpmPos") publish(time * 2.);
            else publish(parameter("input_value", 0.).toDouble());
            continue;
        }
        double speed = parameter("speed", 1.0).toDouble();
        const double curve = parameter("speed_curve", 1.0).toDouble();
        if (speed != 0 && curve > 0) speed = std::copysign(std::pow(std::abs(speed), curve), speed);
        if (parameter("reverse", false).toBool()) speed = -speed;
        const bool bpmSync = parameter("bpm_sync", false).toBool();
        constexpr double beatsPerSecond = 2.0; // Preview clock at 120 BPM.
        if (bpmSync && speed != 0)
            speed = std::copysign(std::exp2(std::round(std::log2(std::abs(speed)))), speed) * beatsPerSecond;
        it->elapsed = resetPending ? time * speed : it->elapsed + delta * speed;
        const double strob = parameter("strob", 0.0).toDouble() / (bpmSync ? beatsPerSecond : 1.0);
        if (strob <= 0) it->sampled = it->elapsed;
        else if (resetPending || std::floor(time / strob) != std::floor((time - delta) / strob))
            it->sampled = it->elapsed - std::fmod(time, strob) * speed;
        double output = it->sampled;
        if (it->type == "animator") {
            const auto shapeRef = parameters.value("shape").toString("Smooth");
            const auto choices = inputs.choices.value(shapeRef);
            const int index = values.value(shapeRef, -1).toInt();
            const auto shape = index >= 0 && index < choices.size() ? choices.at(index).toString() : shapeRef;
            const double cycle = std::floor(output);
            const double phase = output - cycle;
            if (shape == "In") output = phase;
            else if (shape == "Out") output = 1.0 - phase;
            else if (shape == "Linear") output = 1.0 - std::abs(2.0 * phase - 1.0);
            else if (shape == "Cut") output = phase >= 0.5 ? 1.0 : 0.0;
            else if (shape == "Random" || shape == "Noise") {
                const auto random = [](double cell) {
                    const double hash = std::sin(cell * 127.1 + 311.7) * 43758.5453;
                    return hash - std::floor(hash);
                };
                const double blend = phase * phase * (3.0 - 2.0 * phase);
                output = random(cycle);
                if (shape == "Noise") output += blend * (random(cycle + 1.0) - output);
            } else {
                constexpr double tau = 6.283185307179586;
                output = 0.5 - 0.5 * std::cos(tau * phase);
            }
        }
        publish(output);
    }
    resetPending = false;
    return outputs;
}
