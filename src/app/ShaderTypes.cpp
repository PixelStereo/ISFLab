#include "ShaderTypes.h"
#include <algorithm>

const QList<ShaderTypeInfo> &shaderTypes() {
    static const QString toggle = "Toggle this shader type. Several types can be active.";
    static const QList<ShaderTypeInfo> types{
        {ISFType::Generator, "Generator", "generatorType", "GEN", "Gen", toggle, QColor("#84cfab"), true},
        {ISFType::FX, "Filter", "effectType", "FX", "FX", toggle, QColor("#94bdf4"), true},
        {ISFType::Transition, "Transition", "transitionType", "TRANS", "Trans", toggle, QColor("#caadeb"), true},
        {ISFType::Unknown, "ERR", "errorType", "ERR", "ERR",
         "Invalid headers and errors detected in preview.", QColor("#e5a19b"), false},
        {ISFType::NonISF, "Non-ISF", "nonIsfType", "NON-ISF", "Non-ISF",
         "Fragment files without an ISF JSON header.", QColor("#e6bd8d"), false},
        {ISFType::MadMapper, "MadMapper", "madMapperType", "MAD", "MadMapper",
         "Materials with a MadMapper fragment entry point.", QColor("#a5c9d6"), false},
    };
    return types;
}

const ShaderTypeInfo &shaderTypeInfo(ISFType type) {
    const auto &types = shaderTypes();
    const auto found = std::find_if(types.cbegin(), types.cend(), [type](const auto &info) { return info.type == type; });
    return found == types.cend() ? types.at(3) : *found;
}
