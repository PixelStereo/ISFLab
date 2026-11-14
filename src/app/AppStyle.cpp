#include "AppStyle.h"
#include "ShaderTypes.h"
#include <QApplication>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QStyleFactory>

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
    }
    p.end();
    image.setDevicePixelRatio(2);
    return QIcon(image);
}

void apply(QApplication &app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#28282c"));
    palette.setColor(QPalette::WindowText, QColor("#e1e1e4"));
    palette.setColor(QPalette::Base, QColor("#1e1e21"));
    palette.setColor(QPalette::AlternateBase, QColor("#28282c"));
    palette.setColor(QPalette::Text, QColor("#e1e1e4"));
    palette.setColor(QPalette::Button, QColor("#343439"));
    palette.setColor(QPalette::ButtonText, QColor("#e1e1e4"));
    palette.setColor(QPalette::Highlight, QColor("#8394b3"));
    palette.setColor(QPalette::HighlightedText, QColor("#1a1a1d"));
    palette.setColor(QPalette::Mid, QColor("#4b4b51"));
    palette.setColor(QPalette::PlaceholderText, QColor("#929298"));
    palette.setColor(QPalette::ToolTipBase, QColor("#303036"));
    palette.setColor(QPalette::ToolTipText, QColor("#ececef"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#78787d"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#78787d"));
    app.setPalette(palette);
    app.setStyleSheet(R"(
QMainWindow::separator { background:#3b3b41; width:1px; height:1px; }
QToolBar#commandBar { background:#303034; border:0; border-bottom:1px solid #424249; spacing:6px; padding:7px 11px; }
QLabel#brand { color:#c9cdd6; font-size:17px; font-weight:700; padding-right:7px; }
QLabel#version { color:#bdbdc4; background:#38383e; border:1px solid #4b4b51; border-radius:5px; padding:3px 6px; }
QLabel#sectionTitle { color:#ccccd1; font-size:11px; font-weight:700; letter-spacing:1px; }
QLabel#quiet { color:#a7a7af; font-size:11px; }
QWidget#libraryPanel { background:#28282c; }
QWidget#categoryPanel { background:#2d2d32; border-radius:7px; }
QWidget#libraryFooter { background:#303034; border:1px solid #404046; border-radius:6px; }
QPushButton, QToolButton {
    background:#343439; color:#e1e1e4; border:1px solid #505057;
    border-radius:6px; padding:5px 9px; min-height:16px;
}
QPushButton:hover, QToolButton:hover { background:#414147; border-color:#73737e; }
QPushButton:pressed, QToolButton:pressed { background:#46464e; border-color:#81818d; }
QPushButton:focus, QToolButton:focus { border-color:#a0a4af; }
QPushButton:disabled, QToolButton:disabled { color:#78787d; border-color:#3b3b41; }
QPushButton:checked, QToolButton:checked { background:#48484f; border-color:#84848d; }
QPushButton#primary { background:#8394b3; color:#1a1a1d; border-color:#9aabc6; font-weight:600; }
QPushButton#testCard:checked { background:#8394b3; color:#1a1a1d; border-color:#9aabc6; font-weight:600; }
QPushButton#primary:hover { background:#96a7c3; }
QToolButton#command { padding:4px 7px; background:transparent; border-color:transparent; }
QToolButton#command:hover { background:#3a3a40; border-color:#4f4f57; }
QPushButton[filterTab="true"] { padding:4px 5px; font-weight:600; background:#29292e; border-bottom:2px solid #494951; }
QPushButton[filterTab="true"]:checked { background:#45454c; border-color:#747481; }
QPushButton#allTypes:checked { background:#8394b3; color:#1a1a1d; border-color:#9aabc6; }
QPushButton#generatorType { color:#84cfab; }
QPushButton#effectType { color:#94bdf4; }
QPushButton#transitionType { color:#caadeb; }
QPushButton#madMapperType { color:#a5c9d6; }
QPushButton#madMapperType:checked { background:#393940; border-bottom-color:#a5c9d6; }
QPushButton#nonIsfType { color:#e6bd8d; }
QPushButton#nonIsfType:checked { background:#393940; border-bottom-color:#e6bd8d; }
QPushButton#errorType { color:#e5a19b; }
QPushButton#errorType:checked { background:#393940; border-bottom-color:#e5a19b; }
QPushButton#generatorType:checked { background:#393940; border-bottom-color:#84cfab; }
QPushButton#effectType:checked { background:#393940; border-bottom-color:#94bdf4; }
QPushButton#transitionType:checked { background:#393940; border-bottom-color:#caadeb; }
QLineEdit, QAbstractSpinBox, QPlainTextEdit, QComboBox {
    background:#1e1e21; color:#e1e1e4; border:1px solid #47474e; border-radius:5px;
}
QLineEdit, QComboBox { padding:4px 6px; }
QLineEdit#sliderNumber { padding:0 6px; }
QLineEdit:focus, QAbstractSpinBox:focus, QPlainTextEdit:focus, QComboBox:focus { border-color:#9a9eaa; }
QCheckBox { spacing:7px; }
QCheckBox::indicator { width:13px; height:13px; border:1px solid #666670; border-radius:3px; background:#242428; }
QCheckBox::indicator:checked { background:#8394b3; border:3px solid #46464c; width:9px; height:9px; }
QTreeWidget#shaderTree { background:#1e1e21; border:1px solid #3b3b41; border-radius:6px; padding:4px 3px; outline:0; }
QTreeWidget#shaderTree::item { border:0; }
QTabWidget::pane { border:1px solid #55555e; border-radius:6px; }
QTabBar::tab { background:#25252b; padding:5px 11px; margin-right:3px;
               font-weight:600; border:1px solid #62626e; border-bottom:3px solid #44444e;
               border-top-left-radius:4px; border-top-right-radius:4px; }
QTabBar::tab:selected { background:#4c4c59; border-color:#9494a3; border-bottom-color:#8394b3; }
QTabBar::tab:hover:!selected { background:#383841; }
QDockWidget { font-weight:600; color:#d9d9de; }
QDockWidget::title { background:#2c2c32; padding:6px 10px; }
QMenu { background:#2d2d32; color:#e1e1e4; border:1px solid #55555e; }
QMenu::item { padding:5px 20px; }
QMenu::item:selected { background:#48484f; }
)");
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
