#include "MaterialSource.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>

QByteArray expandMaterialIncludes(const QByteArray &source, const QString &path, int firstLine,
                                  int sourceId, QStringList &files, QString &error, int depth) {
    static const QRegularExpression directive(
        R"rx(^[ \t]*#[ \t]*include[ \t]+"([^"\r\n]+)"[ \t]*(?://[^\r\n]*)?\r?$)rx");
    QByteArray result;
    int lineNumber = firstLine;
    for (const auto &line : source.split('\n')) {
        const auto match = directive.match(QString::fromUtf8(line));
        if (!match.hasMatch()) {
            result += line + '\n';
        } else {
            const auto name = match.captured(1);
            const auto local = QFileInfo(path).dir().filePath(name);
            static const QStringList bundledIncludes{"MadCommon.glsl", "MadNoise.glsl", "MadSDF.glsl"};
            const auto bundled = std::find_if(bundledIncludes.cbegin(), bundledIncludes.cend(),
                [&](const QString &candidate) { return candidate.compare(name, Qt::CaseInsensitive) == 0; });
            const auto resource = bundled == bundledIncludes.cend() ? QString{} : materialResourceRoot + *bundled;
            const auto includePath = QDir::isRelativePath(name) && QFileInfo(local).isFile() ? local : resource;
            QFile file(includePath);
            if (depth >= 16 || files.size() >= 64 || !QDir::isRelativePath(name) ||
                !file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
                error = QString("%1:%2: cannot expand Material include \"%3\".")
                            .arg(path).arg(lineNumber).arg(name);
                return {};
            }
            files.append(includePath);
            const int includeId = files.size() - 1;
            result += "#line 1 " + QByteArray::number(includeId) + "\n";
            result += expandMaterialIncludes(file.readAll(), includePath, 1, includeId, files, error, depth + 1);
            if (!error.isEmpty()) return {};
            result += "#line " + QByteArray::number(lineNumber + 1) + " " + QByteArray::number(sourceId) + "\n";
        }
        ++lineNumber;
    }
    return result;
}
