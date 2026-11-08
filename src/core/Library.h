#pragma once
#include "ISFScanner.h"
#include <QList>
#include <QMap>

struct ShaderEntry {
    QString path;
    QByteArray source;
    std::optional<QJsonObject> header;
    ISFType type() const { return header ? ISFScanner::classify(*header) : ISFType::Unknown; }
};

struct ExportReport {
    int copied = 0, skipped = 0, moved = 0;
    QMap<QString, QString> copiedFiles;
    QStringList errors;
};

QStringList shaderFiles(const QString &folder, bool skipOrganized = false);
ExportReport exportShaders(const QList<ShaderEntry> &entries, const QString &destination);
