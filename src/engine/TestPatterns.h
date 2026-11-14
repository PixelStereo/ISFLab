#pragma once
#include <QImage>

// Built-in preview sources, 640 × 360.
namespace TestPatterns {
// Animated card shown as the input of FX shaders: color bars, gradients and moving elements.
QImage testCard(double seconds);
// The BEFORE and AFTER images of a transition.
QImage transitionCard(bool after);
}
