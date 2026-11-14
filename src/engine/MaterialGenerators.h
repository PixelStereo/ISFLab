#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QStringList>
#include <QVariant>

// MadMapper Material GENERATORS: float uniforms computed each frame from time, inputs and other
// generators (time_base, animator, incrementer, multiplier, pass_thru). The preview clock runs at 120 BPM.
class MaterialGenerators {
public:
    struct Inputs {
        const QHash<QString, QVariant> &values;
        const QHash<QString, qint64> &eventCounts;   // presses of each event input
        const QHash<QString, QJsonArray> &choices;   // labels of long inputs
        double time = 0, delta = 0;
    };

    static bool isSupportedType(const QString &type);
    void clear();
    void add(const QString &name, const QString &type, const QJsonObject &parameters);
    // Orders generators so each one follows the generators it reads. Returns the name of a generator in
    // a dependency cycle, or an empty string.
    QString order();
    // Restarts accumulated phases from the current time on the next evaluation (after a seek).
    void reset() { resetPending = true; }
    // Generator values in evaluation order.
    QList<QPair<QString, double>> evaluate(const Inputs &inputs);

private:
    struct Generator {
        QJsonObject parameters;
        QString type;
        double elapsed = 0, sampled = 0;
    };
    QHash<QString, Generator> generators;
    QStringList evaluationOrder;
    bool resetPending = true;
};
