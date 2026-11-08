#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QStringList>
#include <optional>

enum class ISFType { Generator, FX, Transition, Unknown, NonISF, MadMapper };

class ISFScanner {
public:
    struct HeaderError {
        QString message;
        qsizetype byteOffset = 0;
    };
    static std::optional<QJsonObject> header(const QByteArray &source, HeaderError *error = nullptr);
    static QList<qsizetype> trailingCommaFix(const QByteArray &source);
    static std::optional<QJsonObject> materialHeader(const QByteArray &source);
    static bool isMadMapperMaterial(const QByteArray &source);
    static qsizetype headerStart(const QByteArray &source);
    static QString companionVertexPath(const QString &shaderPath);
    static ISFType classify(const QJsonObject &header);
    static QStringList categories(const QJsonObject &header);
    static QString typeToString(ISFType type);
    static QString typeFolder(ISFType type);
};
