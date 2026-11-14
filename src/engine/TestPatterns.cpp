#include "TestPatterns.h"
#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPolygon>
#include <cmath>

namespace TestPatterns {
QImage testCard(double seconds) {
    QImage image(640, 360, QImage::Format_RGBA8888);
    const QColor ink("#111319"), panel("#191d27"), grid("#343a48"), white("#f4f1e9");
    const QColor accents[] = {QColor("#fa684f"), QColor("#f8cc63"), QColor("#a8dc9c"),
                              QColor("#69d9d7"), QColor("#839bff"), QColor("#db8be6")};
    image.fill(ink);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);

    p.setPen(white);
    p.setFont(QFont("Sans Serif", 13, QFont::DemiBold));
    p.drawText(QRect(20, 12, 600, 28), Qt::AlignLeft | Qt::AlignVCenter, "ISFLAB  /  PIXEL STEREO");
    p.drawText(QRect(230, 12, 275, 28), Qt::AlignCenter, "CHROMA  /  LUMA  /  MOTION");
    p.drawText(QRect(510, 12, 110, 28), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(seconds, 'f', 2) + " s");

    p.fillRect(QRect(20, 52, 356, 214), panel);
    p.setPen(grid);
    for (int x = 38; x < 376; x += 20) p.drawLine(x, 52, x, 265);
    for (int y = 59; y < 266; y += 20) p.drawLine(20, y, 375, y);
    const QPointF center(198, 159);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor("#7c869a"), 1));
    for (int radius : {28, 54, 80}) p.drawEllipse(center, radius, radius);
    p.drawLine(QPointF(102, 159), QPointF(294, 159));
    p.drawLine(QPointF(198, 63), QPointF(198, 255));
    for (int i = 0; i < 3; ++i) {
        const double phase = seconds * (1.4 + i * 0.55) + i * 2.1;
        const double radius = 80 - i * 26;
        const auto dot = center + QPointF(std::cos(phase) * radius, std::sin(phase) * radius);
        p.setBrush(i == 0 ? white : accents[i + 2]);
        p.setPen(QPen(ink, 2));
        p.drawEllipse(dot, 12 - i * 2, 12 - i * 2);
    }

    const QColor rgb[] = {Qt::red, Qt::green, Qt::blue};
    for (int i = 0; i < 3; ++i) {
        const QRect tile(388 + i * 79, 52, 74, 94);
        p.fillRect(tile, rgb[i]);
        p.setPen(ink);
        p.drawText(tile.adjusted(8, 0, 0, -6), Qt::AlignLeft | Qt::AlignBottom,
                   QString(QChar("RGB"[i])));
    }

    const QRect gradientArea(388, 156, 232, 110);
    QLinearGradient cmyk(gradientArea.topLeft(), gradientArea.bottomRight());
    cmyk.setColorAt(0.0, QColor::fromCmyk(255, 0, 0, 0));
    cmyk.setColorAt(1.0 / 3.0, QColor::fromCmyk(0, 255, 0, 0));
    cmyk.setColorAt(2.0 / 3.0, QColor::fromCmyk(0, 0, 255, 0));
    cmyk.setColorAt(1.0, QColor::fromCmyk(0, 0, 0, 255));
    p.fillRect(gradientArea, cmyk);

    const QRect track(0, 304, image.width(), image.height() - 304);
    p.fillRect(track, panel);
    p.setClipRect(track);
    const int travel = int(std::fmod(seconds * 110, 32.0));
    for (int x = track.left() - 96 + travel; x < track.right(); x += 32) {
        QPolygon stripe;
        stripe << QPoint(x + 76, track.top()) << QPoint(x + 86, track.top())
               << QPoint(x + 10, track.bottom() + 1) << QPoint(x, track.bottom() + 1);
        p.setPen(Qt::NoPen);
        p.setBrush(white);
        p.drawPolygon(stripe);
    }
    p.setClipping(false);

    for (int i = 0; i < 16; ++i) {
        const int level = i * 255 / 15;
        const int x = 20 + i * 600 / 16;
        p.fillRect(x, 281, 20 + (i + 1) * 600 / 16 - x, 19, QColor(level, level, level));
    }
    p.end();
    return image;
}

QImage transitionCard(bool after) {
    QImage image(640, 360, QImage::Format_RGBA8888);
    QPainter painter(&image);
    QLinearGradient gradient(0, 0, image.width(), image.height());
    gradient.setColorAt(0, QColor(after ? "#813e73" : "#294297"));
    gradient.setColorAt(1, QColor(after ? "#e68556" : "#319fa6"));
    painter.fillRect(image.rect(), gradient);
    painter.setPen(QColor(255, 255, 255, 150));
    painter.drawRect(image.rect().adjusted(18, 18, -19, -19));
    painter.setPen(Qt::white);
    painter.setFont(QFont("Sans Serif", 58, QFont::Bold));
    painter.drawText(image.rect(), Qt::AlignCenter, after ? "AFTER" : "BEFORE");
    return image;
}
}
