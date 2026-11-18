#include "AppStyle.h"
#include "ShaderTypes.h"
#include <QApplication>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QStyleFactory>
#include <pxs/Theme.h>

namespace AppStyle {
QColor typeColor(ISFType type) { return shaderTypeInfo(type).color; }

QIcon icon(Icon kind, const QColor &color) {
    QPixmap image(40, 40);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(2, 2);
    p.setPen(QPen(color, 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const auto line = [&](std::initializer_list<QPointF> points) { p.drawPolyline(QPolygonF(points)); };
    switch (kind) {
    case Icon::Folder:
        line({{2.5,15.5},{2.5,5},{7.5,5},{9.5,7},{17.5,7},{17.5,15.5},{2.5,15.5}});
        break;
    case Icon::File:
        line({{5,17},{5,3},{12,3},{15.5,6.5},{15.5,17},{5,17}});
        line({{12,3},{12,7},{15,7}});
        line({{9,9.5},{7.7,11},{9,12.5}});
        line({{11.5,9.5},{12.8,11},{11.5,12.5}});
        break;
    case Icon::Search:
        p.drawEllipse(QPointF(8.5,8.5), 4.5, 4.5);
        p.drawLine(QPointF(12,12), QPointF(16.5,16.5));
        break;
    case Icon::Export:
        line({{3,12},{3,17},{17,17},{17,12}});
        line({{10,14},{10,3}});
        line({{6,7},{10,3},{14,7}});
        break;
    case Icon::Reload: // a circular arrow
        p.drawArc(QRectF(4, 4, 12, 12), 40 * 16, 270 * 16);
        line({{14.5,2.5},{15.5,6.5},{11.5,7}});
        break;
    }
    p.end();
    image.setDevicePixelRatio(2);
    return QIcon(image);
}

void apply(QApplication &app) {
    // The graphite look shared by every Pixel Stereo application (palette, rounded sections, toggles); what follows is
    // this application's own structure, written with the same colour tokens
    pxs::apply(app, pxs::graphite(), QStringLiteral(R"(
QMainWindow::separator { background:@midlight; width:1px; height:1px; }
QToolBar#commandBar { background:@button; border:0; border-bottom:1px solid @midlight; spacing:6px; padding:7px 11px; }
QLabel#brand { color:@muted; font-size:17px; font-weight:700; padding-right:7px; }
QLabel#version { color:@muted; background:@midlight; border:1px solid @border; border-radius:5px; padding:3px 6px; }
QLabel#sectionTitle { color:@muted; font-size:11px; font-weight:700; letter-spacing:1px; }
QLabel#quiet { color:@muted; font-size:11px; }
QWidget#libraryPanel { background:@window; }
QWidget#categoryPanel { background:@button; border-radius:7px; }
QWidget#libraryFooter { background:@button; border:1px solid @midlight; border-radius:6px; }
QPushButton, QToolButton {
    background:@button; color:@text; border:1px solid @border;
    border-radius:6px; padding:5px 9px; min-height:16px;
}
QPushButton:hover, QToolButton:hover { background:@midlight; border-color:@border; }
QPushButton:pressed, QToolButton:pressed { background:@light; border-color:@border; }
QPushButton:focus, QToolButton:focus { border-color:@focus; }
QPushButton:disabled, QToolButton:disabled { color:@border; border-color:@midlight; }
QPushButton:checked, QToolButton:checked { background:@light; border-color:@border; }
QPushButton#primary { background:@fill; color:@border; border-color:@link; font-weight:600; }
QPushButton#testCard:checked { background:@fill; color:@border; border-color:@link; font-weight:600; }
QPushButton#primary:hover { background:@fill; }
QToolButton#command { padding:4px 7px; background:transparent; border-color:transparent; }
QToolButton#command:hover { background:@midlight; border-color:@border; }
QPushButton[filterTab="true"] { padding:4px 5px; font-weight:600; background:@button; border-bottom:2px solid @border; }
QPushButton[filterTab="true"]:checked { background:@light; border-color:@border; }
QPushButton#allTypes:checked { background:@fill; color:@border; border-color:@link; }
QPushButton#generatorType { color:#84cfab; }
QPushButton#effectType { color:#94bdf4; }
QPushButton#transitionType { color:#caadeb; }
QPushButton#madMapperType { color:@link; }
QPushButton#madMapperType:checked { background:@midlight; border-bottom-color:@link; }
QPushButton#nonIsfType { color:#e6bd8d; }
QPushButton#nonIsfType:checked { background:@midlight; border-bottom-color:#e6bd8d; }
QPushButton#errorType { color:#e5a19b; }
QPushButton#errorType:checked { background:@midlight; border-bottom-color:#e5a19b; }
QPushButton#generatorType:checked { background:@midlight; border-bottom-color:#84cfab; }
QPushButton#effectType:checked { background:@midlight; border-bottom-color:#94bdf4; }
QPushButton#transitionType:checked { background:@midlight; border-bottom-color:#caadeb; }
QLineEdit, QAbstractSpinBox, QPlainTextEdit, QComboBox {
    background:@base; color:@text; border:1px solid @midlight; border-radius:5px;
}
QLineEdit, QComboBox { padding:4px 6px; }
QLineEdit#sliderNumber { padding:0 6px; }
QLineEdit:focus, QAbstractSpinBox:focus, QPlainTextEdit:focus, QComboBox:focus { border-color:@focus; }
QTreeWidget#shaderTree { background:@base; border:1px solid @midlight; border-radius:6px; padding:4px 3px; outline:0; }
QTreeWidget#shaderTree::item { border:0; }
QDockWidget { font-weight:600; color:@text; }
QDockWidget::title { background:@button; padding:6px 10px; }
QMenu { background:@button; color:@text; border:1px solid @border; }
QMenu::item { padding:5px 20px; }
QMenu::item:selected { background:@light; }
)"));
}

namespace {
class LibraryRows final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &o, const QModelIndex &i) const override {
        return {QStyledItemDelegate::sizeHint(o, i).width(), i.data(GroupRole).toBool() ? 31 : (i.data(OriginRole).toString().isEmpty() ? 30 : 43)};
    }
    void paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &i) const override {
        const bool group = i.data(GroupRole).toBool();
        const bool active = i.data(ActiveRole).toBool();
        const auto kind = static_cast<ISFType>(i.data(KindRole).toInt());
        const auto rect = o.rect.adjusted(1, 1, -1, -1);
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const bool selected = o.state.testFlag(QStyle::State_Selected);
        if (active || selected || o.state.testFlag(QStyle::State_MouseOver)) {
            p->setPen(Qt::NoPen);
            p->setBrush(selected ? QColor("#45536b") : active ? QColor("#48484f") : QColor("#343439"));
            p->drawRoundedRect(rect, 5, 5);
        }
        auto font = o.font;
        font.setWeight(group || active ? QFont::DemiBold : QFont::Normal);
        p->setFont(font);
        if (i.column() == 0) {
            const auto symbol = icon(group ? Icon::Folder : Icon::File, group ? QColor("#8394b3") : QColor("#a5a5ae"));
            symbol.paint(p, QRect(rect.left() + 4, rect.center().y() - 8, 16, 16));
            p->setPen(group ? QColor("#c9c9d1") : kind == ISFType::Unknown ? QColor("#ff9292") : QColor("#eeeeef"));
            const auto label = rect.adjusted(25, 0, -3, 0);
            const auto origin = i.data(OriginRole).toString();
            const auto nameRect = origin.isEmpty() ? label : label.adjusted(0, 0, 0, -15);
            p->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                        p->fontMetrics().elidedText(i.data().toString(), Qt::ElideRight, label.width()));
            if (!origin.isEmpty()) {
                font.setWeight(QFont::Normal);
                font.setPixelSize(11);
                p->setFont(font);
                p->setPen(QColor("#b5b5bf"));
                p->drawText(label.adjusted(0, 19, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                            p->fontMetrics().elidedText(origin, Qt::ElideMiddle, label.width()));
            }
        } else {
            const auto label = i.data().toString();
            if (!label.isEmpty()) {
                const auto color = group ? QColor("#bdbdc6") : typeColor(kind);
                const int width = p->fontMetrics().horizontalAdvance(label) + 14;
                const QRect chip(rect.right() - width - 5, rect.center().y() - 9, width, 19);
                QColor tint(color);
                tint.setAlpha(32);
                p->setBrush(tint);
                tint.setAlpha(100);
                p->setPen(tint);
                p->drawRoundedRect(chip, 5, 5);
                p->setPen(color);
                p->drawText(chip, Qt::AlignCenter, label);
            }
        }
        p->restore();
    }
};
}

QAbstractItemDelegate *libraryDelegate(QObject *parent) { return new LibraryRows(parent); }
}
