#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QStringList>
#include <optional>

enum class ISFType { Generator, FX, Transition, Unknown, NonISF, MadMapper };

// Parsing and classification of ISF sources. Stateless: every function works on the bytes it is given.
namespace ISFScanner {
struct HeaderError {
    QString message;
    qsizetype byteOffset = 0;
};

// Byte range of the JSON header comment: start points at "/*", end at the closing "*/".
struct HeaderRange {
    qsizetype start = -1, end = -1;
    bool isValid() const { return start >= 0 && end > start; }
    qsizetype jsonStart() const { return start + 2; }
    qsizetype jsonLength() const { return end - start - 2; }
    qsizetype bodyStart() const { return end + 2; }
};

std::optional<QJsonObject> header(const QByteArray &source, HeaderError *error = nullptr);
QList<qsizetype> trailingCommaFix(const QByteArray &source);
std::optional<QJsonObject> materialHeader(const QByteArray &source);
bool isMadMapperMaterial(const QByteArray &source);
qsizetype headerStart(const QByteArray &source);
HeaderRange headerRange(const QByteArray &source);
QString companionVertexPath(const QString &shaderPath);
ISFType classify(const QJsonObject &header);
QStringList categories(const QJsonObject &header);
QString typeToString(ISFType type);
QString typeFolder(ISFType type);

// Shader files the library imports, and the matching file dialog filter.
bool isShaderFile(const QString &path);
QString shaderFileFilter();

// ISF input TYPE values the preview understands, and those whose current value can be saved as DEFAULT.
const QStringList &inputTypes();
const QStringList &defaultableInputTypes();
}
