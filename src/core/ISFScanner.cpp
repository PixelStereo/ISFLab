#include <QDir>
#include <QFileInfo>
#include "ISFScanner.h"
#include <QJsonArray>
#include <QJsonDocument>

std::optional<QJsonObject> ISFScanner::header(const QByteArray &source) {
    auto data = source;
    if (data.startsWith("\xef\xbb\xbf")) data.remove(0, 3);
    data = data.trimmed();
    const auto end = data.indexOf("*/", 2);
    if (!data.startsWith("/*") || end < 0) return std::nullopt;
    const auto doc = QJsonDocument::fromJson(data.mid(2, end - 2));
    if (!doc.isObject()) return std::nullopt;
    const auto object = doc.object();
    if (object.contains("INPUTS") && !object.value("INPUTS").isArray()) return std::nullopt;
    return object;
}

ISFType ISFScanner::classify(const QJsonObject &header) {
    bool input = false, start = false, end = false, progress = false;
    for (const auto &value : header.value("INPUTS").toArray()) {
        const auto entry = value.toObject();
        const auto name = entry.value("NAME").toString();
        const auto type = entry.value("TYPE").toString();
        input |= name == "inputImage" && type == "image";
        start |= name == "startImage" && type == "image";
        end |= name == "endImage" && type == "image";
        progress |= name == "progress" && type == "float";
    }
    if (start && end && progress) return ISFType::Transition;
    return input ? ISFType::FX : ISFType::Generator;
}

QStringList ISFScanner::categories(const QJsonObject &header) {
    QStringList result;
    for (const auto &value : header.value("CATEGORIES").toArray()) {
        const auto name = value.toString().trimmed();
        if (!name.isEmpty() && !result.contains(name, Qt::CaseInsensitive)) result << name;
    }
    return result;
}

QString ISFScanner::typeToString(ISFType type) {
    switch (type) {
    case ISFType::Generator: return "Generator";
    case ISFType::FX: return "FX";
    case ISFType::Transition: return "Transition";
    default: return "Invalid header";
    }
}

QString ISFScanner::typeFolder(ISFType type) {
    switch (type) {
    case ISFType::Generator: return "Generators";
    case ISFType::FX: return "FX";
    case ISFType::Transition: return "Transitions";
    default: return {};
    }
}

QString ISFScanner::companionVertexPath(const QString &shaderPath) {
    if (shaderPath.isEmpty()) return {};
    const QFileInfo shader(shaderPath);
    const auto stem = shader.completeBaseName();
    const auto directory = shader.dir();
    const auto conventional = directory.filePath(stem + ".vs");
    if (QFileInfo(conventional).isFile()) return conventional;
    for (const auto &name : directory.entryList(QDir::Files | QDir::Hidden, QDir::Name)) {
        const QFileInfo candidate(name);
        if (candidate.completeBaseName() == stem &&
            candidate.suffix().compare("vs", Qt::CaseInsensitive) == 0)
            return directory.filePath(name);
    }
    return {};
}
