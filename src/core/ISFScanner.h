#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <optional>

enum class ISFType { Generator, FX, Transition, Unknown };

class ISFScanner {
public:
    static std::optional<QJsonObject> header(const QByteArray &source);
    static QString companionVertexPath(const QString &shaderPath);
    static ISFType classify(const QJsonObject &header);
    static QStringList categories(const QJsonObject &header);
    static QString typeToString(ISFType type);
    static QString typeFolder(ISFType type);
};
