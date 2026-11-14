#pragma once
#include <QPointF>
#include <QWidget>
#include <functional>

// Pad that edits a normalized 2D position (0..1 on each axis, Y up) by clicking or dragging.
class PointPad final : public QWidget {
public:
    explicit PointPad(QWidget *parent = nullptr);
    void setPosition(const QPointF &value);
    std::function<void(const QPointF &)> positionEdited;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QRectF plot() const { return QRectF(rect()).adjusted(8, 8, -8, -8); }
    void editPosition(const QPointF &point);

    QPointF position;
};
