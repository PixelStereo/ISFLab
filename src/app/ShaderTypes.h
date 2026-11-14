#pragma once
#include "ISFScanner.h"
#include <QColor>
#include <QList>
#include <QString>

// How each shader type is presented in the Media Bin: one row per type, in display order.
struct ShaderTypeInfo {
    ISFType type;
    QString filterLabel; // type filter button
    QString objectName;  // style sheet selector, also the persisted filter key
    QString badge;       // short tag in the shader list
    QString caption;     // footer statistics
    QString toolTip;
    QColor color;
    bool primary;        // first filter row (Generator, FX, Transition)
};

const QList<ShaderTypeInfo> &shaderTypes();
const ShaderTypeInfo &shaderTypeInfo(ISFType type);
