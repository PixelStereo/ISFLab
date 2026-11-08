#include "SliderField.h"
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

SliderField::SliderField(QWidget *parent) : QWidget(parent), number(new QLineEdit(this)) {
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumSize(100, 24);
    number->setObjectName("sliderNumber");
    number->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    number->setAccessibleName("Numeric value");
    number->installEventFilter(this);
    installEventFilter(this);
    setToolTip("Drag the bar or the number. Hold Shift for fine adjustment, or type a value.");
    connect(number, &QLineEdit::editingFinished, this, [this] {
        auto text = number->text().trimmed();
        const auto suffix = unit.trimmed();
        if (!suffix.isEmpty() && text.endsWith(suffix)) text.chop(suffix.size());
        bool ok = false;
        const auto typed = locale().toDouble(text.trimmed(), &ok);
        if (ok && std::isfinite(typed)) edit(typed);
        else syncText();
    });
    syncText();
}

void SliderField::setGradient(const QGradientStops &stops) {
    gradientStops = stops;
    update();
}

void SliderField::setRange(double min, double max) {
    lower = min;
    upper = std::max(min, max);
    setTypedRange(lower, upper);
}
void SliderField::setTypedRange(double min, double max) {
    typedMin = min;
    typedMax = std::max(min, max);
    setValue(current);
    arrange();
}
void SliderField::setDecimals(int digits) {
    decimals = qBound(0, digits, 12);
    setValue(current);
    arrange();
}
void SliderField::setSingleStep(double interval) { step = interval; }
void SliderField::setSuffix(const QString &suffix) { unit = suffix; syncText(); arrange(); }
void SliderField::setValue(double value) {
    const auto scale = std::pow(10., decimals);
    const auto bounded = std::clamp(value, typedMin, typedMax);
    current = std::clamp(std::round(bounded * scale) / scale, typedMin, typedMax);
    syncText();
    update();
}
void SliderField::edit(double value) {
    const double old = current;
    setValue(value);
    if (old != current) emit valueEdited(current);
}
QString SliderField::format(double value) const { return locale().toString(value, 'f', decimals) + unit; }
void SliderField::syncText() {
    const auto text = format(current);
    if (text != number->text()) number->setText(text);
}
bool SliderField::isEditing() const { return dragging || numberDragging || number->hasFocus(); }
QSize SliderField::sizeHint() const { return QSize(220, 24); }

void SliderField::arrange() {
    const auto metrics = number->fontMetrics();
    const int ideal = 18 + std::max(metrics.horizontalAdvance(format(typedMin)),
                                    metrics.horizontalAdvance(format(typedMax)));
    const int fieldWidth = qBound(55, ideal, qMax(55, width() - 36));
    number->setGeometry(width() - fieldWidth, 0, fieldWidth, height());
    update();
}

QRectF SliderField::track() const { return QRectF(0.5, 2.5, qMax(1, number->x() - 6), height() - 5); }
void SliderField::resizeEvent(QResizeEvent *) { arrange(); }

void SliderField::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto bar = track();
    const auto position = [&](double v) {
        return bar.left() + bar.width() * (upper > lower ? qBound(0., (v - lower) / (upper - lower), 1.) : 0.);
    };
    p.setPen(Qt::NoPen);
    p.setBrush(palette().color(QPalette::Base));
    p.drawRoundedRect(bar, 4, 4);
    if (!gradientStops.isEmpty()) {
        QLinearGradient gradient(bar.topLeft(), bar.topRight());
        gradient.setStops(gradientStops);
        p.setBrush(gradient);
        p.drawRoundedRect(bar, 4, 4);
        const auto x = position(value());
        p.setPen(QPen(Qt::black, 3));
        p.drawLine(QPointF(x, bar.top() + 2), QPointF(x, bar.bottom() - 2));
        p.setPen(Qt::white);
        p.drawLine(QPointF(x, bar.top() + 2), QPointF(x, bar.bottom() - 2));
        return;
    }
    const auto origin = position(0), current = position(value());
    p.setBrush(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::Highlight));
    p.drawRoundedRect(QRectF(qMin(origin, current), bar.top(), std::abs(current - origin), bar.height()), 4, 4);
    p.setBrush(Qt::NoBrush);
    p.setPen(palette().color(hasFocus() ? QPalette::Highlight : QPalette::Mid));
    p.drawRoundedRect(bar, 4, 4);
}

void SliderField::scrub(double x, bool fine) {
    const auto bar = track();
    if (fine) dragValue = qBound(lower, dragValue + (x - lastX) * (upper - lower) / bar.width() * .1, upper);
    else dragValue = lower + (upper - lower) * qBound(0., (x - bar.left()) / bar.width(), 1.);
    edit(dragValue);
    lastX = x;
}
void SliderField::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !track().contains(event->position())) return;
    setFocus(Qt::MouseFocusReason);
    dragging = true;
    lastX = event->position().x();
    dragValue = current;
    scrub(lastX, event->modifiers().testFlag(Qt::ShiftModifier));
}
void SliderField::mouseMoveEvent(QMouseEvent *event) {
    if (dragging) scrub(event->position().x(), event->modifiers().testFlag(Qt::ShiftModifier));
    else setCursor(track().contains(event->position()) ? Qt::SizeHorCursor : Qt::ArrowCursor);
}
void SliderField::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) dragging = false;
}
void SliderField::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
        edit(value() + step * (event->key() == Qt::Key_Left ? -1 : 1));
        event->accept();
    } else QWidget::keyPressEvent(event);
}
bool SliderField::eventFilter(QObject *object, QEvent *event) {
    if ((object == number || object == this) && event->type() == QEvent::Wheel) {
        event->ignore();
        return true;
    }
    if (object == number) {
        if (event->type() == QEvent::MouseButtonPress) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                numberPressed = true;
                numberDragStart = lastX = mouse->globalPosition().x();
                dragValue = current;
            }
        } else if (event->type() == QEvent::MouseMove && numberPressed) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            const double x = mouse->globalPosition().x();
            if (numberDragging || std::abs(x - numberDragStart) >= QApplication::startDragDistance()) {
                numberDragging = true;
                number->setCursor(Qt::SizeHorCursor);
                const double sensitivity = (upper - lower) / track().width();
                const double fine = mouse->modifiers().testFlag(Qt::ShiftModifier) ? 0.1 : 1.0;
                dragValue = qBound(typedMin, dragValue + (x - lastX) * sensitivity * fine, typedMax);
                lastX = x;
                edit(dragValue);
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::UngrabMouse) {
            numberPressed = false;
            if (numberDragging) {
                numberDragging = false;
                number->unsetCursor();
                number->deselect();
            }
        }
    }
    return QWidget::eventFilter(object, event);
}
