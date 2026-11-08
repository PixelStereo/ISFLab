#pragma once
#include <QWidget>
#include <QGradient>

class QLineEdit;

// ISFLab's standalone implementation of the NyxMap SliderField interaction.
class SliderField : public QWidget {
    Q_OBJECT
public:
    explicit SliderField(QWidget *parent = nullptr);
    void setGradient(const QGradientStops &stops);
    void setRange(double lower, double upper);
    void setTypedRange(double lower, double upper);
    void setDecimals(int digits);
    void setSingleStep(double step);
    void setSuffix(const QString &suffix);
    void setValue(double value);
    double value() const { return current; }
    bool isEditing() const;
    QSize sizeHint() const override;
signals:
    void valueEdited(double value);
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    QRectF track() const;
    void arrange();
    void scrub(double x, bool fine);
    void edit(double value);
    void syncText();
    QString format(double value) const;

    QLineEdit *number;
    QString unit;
    QGradientStops gradientStops;
    double lower = 0, upper = 1, typedMin = 0, typedMax = 1;
    double current = 0, lastX = 0, dragValue = 0, step = 1;
    int decimals = 2;
    bool dragging = false;
};
