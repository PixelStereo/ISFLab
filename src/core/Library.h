#pragma once
#include "ISFScanner.h"
#include <QList>
#include <QMap>

struct ShaderEntry {
    QString path;
    QByteArray source;
    std::optional<QJsonObject> header;
    bool madMapper = ISFScanner::isMadMapperMaterial(source);
    ISFType type() const {
        if (madMapper) return ISFType::MadMapper;
        if (header) return ISFScanner::classify(*header);
        if (path.endsWith(".fs", Qt::CaseInsensitive) && ISFScanner::headerStart(source) < 0)
            return ISFType::NonISF;
        return ISFType::Unknown;
    }
};

struct ExportReport {
    int copied = 0, skipped = 0, moved = 0;
    QMap<QString, QString> copiedFiles;
    QStringList errors;
};

QStringList shaderFiles(const QString &folder, bool skipOrganized = false);
ExportReport exportShaders(const QList<ShaderEntry> &entries, const QString &destination);
QString copyShaderToFolder(const ShaderEntry &entry, const QString &destination, QString &created);
QByteArray shaderComparisonSource(const ShaderEntry &entry);
std::optional<QByteArray> shaderFingerprint(const ShaderEntry &entry);
