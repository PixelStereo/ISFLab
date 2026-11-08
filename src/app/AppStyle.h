#pragma once
#include "ISFScanner.h"
#include <QColor>
#include <QIcon>

class QApplication;
class QAbstractItemDelegate;
class QObject;

namespace AppStyle {
inline constexpr int PathRole = Qt::UserRole;
inline constexpr int KindRole = Qt::UserRole + 1;
inline constexpr int ActiveRole = Qt::UserRole + 2;
inline constexpr int GroupRole = Qt::UserRole + 3;
inline constexpr int OriginRole = Qt::UserRole + 4;
enum class Icon { File, Folder, Search, Export };
void apply(QApplication &app);
QColor typeColor(ISFType type);
QIcon icon(Icon kind, const QColor &color = QColor("#b5b5bf"));
QAbstractItemDelegate *libraryDelegate(QObject *parent);
}
