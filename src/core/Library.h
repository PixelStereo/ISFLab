#pragma once
#include "ISFScanner.h"
#include <QList>
#include <QMap>

// A shader file as read from disk. The parsed header and the type are derived once from the source:
// change the source through setSource() so that they stay consistent.
struct ShaderEntry {
    QString path;
    QByteArray source;
    std::optional<QJsonObject> header;
    bool madMapper = false;

    static ShaderEntry fromSource(const QString &path, const QByteArray &source);
    static std::optional<ShaderEntry> read(const QString &path);
    void setSource(const QByteArray &bytes);
    ISFType type() const { return kind; }

private:
    ISFType kind = ISFType::Unknown;
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
