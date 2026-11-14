#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>

// Resource folder of the bundled MadMapper include files and noise texture.
inline const QString materialResourceRoot = QStringLiteral(":/madmapper/");

// Inlines MadMapper #include "file" directives (local files first, then the bundled MadCommon, MadNoise
// and MadSDF), with #line directives so compiler errors point at the right file. files lists the source
// ids: files[0] is the shader itself. On failure, returns an empty array and sets error.
QByteArray expandMaterialIncludes(const QByteArray &source, const QString &path, int firstLine,
                                  int sourceId, QStringList &files, QString &error, int depth = 0);
