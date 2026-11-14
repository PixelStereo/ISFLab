#pragma once
#include <QHash>
#include <QString>

// Evaluates an ISF pass WIDTH or HEIGHT expression such as "floor($WIDTH / 2)": + - * / and parentheses,
// numbers, $variables, and floor, ceil, round, abs, min, max. Returns NaN when the expression is invalid.
double evaluateDimension(const QString &expression, const QHash<QString, double> &variables);
