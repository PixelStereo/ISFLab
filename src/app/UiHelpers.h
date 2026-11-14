#pragma once
#include <QLabel>

// A label that never interprets its text as rich text (file names and shader text are user data).
inline QLabel *plainLabel(const QString &text = {}) {
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    return label;
}
