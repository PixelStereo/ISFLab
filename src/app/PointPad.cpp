#include "PointPad.h"
#include <QMouseEvent>
#include <QPainter>

PointPad::PointPad(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(104);
    setCursor(Qt::CrossCursor);
    setAccessibleName("XY position");
    setToolTip("Drag to adjust X and Y.");
}

void PointPad::setPosition(const QPointF &value) {
    position = value;
    update();
}

void PointPad::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto area = plot();
    painter.setPen(palette().color(QPalette::Mid));
    painter.setBrush(palette().color(QPalette::Base));
    painter.drawRoundedRect(area, 6, 6);
    for (int i = 1; i < 4; ++i) {
        const auto x = area.left() + area.width() * i / 4;
        const auto y = area.top() + area.height() * i / 4;
        painter.drawLine(QPointF(x, area.top() + 3), QPointF(x, area.bottom() - 3));
        painter.drawLine(QPointF(area.left() + 3, y), QPointF(area.right() - 3, y));
    }
    const QPointF handle(area.left() + area.width() * position.x(), area.bottom() - area.height() * position.y());
    painter.setPen(QPen(palette().color(QPalette::Base), 2));
    painter.setBrush(palette().color(QPalette::Highlight));
    painter.drawEllipse(handle, 6, 6);
}

void PointPad::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) editPosition(event->position());
}

void PointPad::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) editPosition(event->position());
}

void PointPad::editPosition(const QPointF &point) {
    if (!positionEdited) return;
    const auto area = plot();
    positionEdited({qBound(0., (point.x() - area.left()) / area.width(), 1.),
                    qBound(0., (area.bottom() - point.y()) / area.height(), 1.)});
}
