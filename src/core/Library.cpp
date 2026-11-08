#include "Library.h"
#include <QCryptographicHash>
#include <QDir>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>

namespace {
const QStringList folders{"Generators", "FX", "Transitions"};
void collect(const QDir &dir, QStringList &paths, bool skipOrganized) {
    for (const auto &info : dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                               QDir::Name | QDir::IgnoreCase)) {
        if (info.isDir()) {
            if (!skipOrganized || !folders.contains(info.fileName(), Qt::CaseInsensitive))
                collect(QDir(info.absoluteFilePath()), paths, false);
        } else if (QStringList{"fs", "isf"}.contains(info.suffix(), Qt::CaseInsensitive)) {
            paths << info.absoluteFilePath();
        }
    }
}

QString uniquePath(const QDir &dir, const QString &stem, const QString &suffix) {
    for (int n = 1; ; ++n) {
        const auto path = dir.filePath(stem + (n == 1 ? QString() : QString("_%1").arg(n)) + suffix);
        const QFileInfo info(path);
        if (!info.exists() && !info.isSymLink()) return path;
    }
}

bool writeNew(const QString &path, const QByteArray &source) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
    if (file.write(source) == source.size() && file.flush()) return true;
    file.close();
    file.remove();
    return false;
}

QString copyEntry(const ShaderEntry &entry, QDir out, QString &created) {
    const QFileInfo info(entry.path);
    const auto stem = info.completeBaseName();
    const auto suffix = "." + info.suffix();
    const auto vertex = ISFScanner::companionVertexPath(entry.path);
    const auto imports = entry.header ? entry.header->value("IMPORTED") : QJsonValue(QJsonValue::Undefined);
    if (!imports.isUndefined() && !imports.isObject()) return "Invalid IMPORTED image declarations.";
    const auto imported = imports.toObject();
    if (vertex.isEmpty() && imported.isEmpty()) {
        const auto target = uniquePath(out, stem, suffix);
        if (!writeNew(target, entry.source)) return "Cannot write shader.";
        created = target;
        return {};
    }

    QStringList imagePaths;
    int parentDepth = 0;
    for (const auto &value : imported) {
        const auto relative = QDir::fromNativeSeparators(value.toObject().value("PATH").toString());
        if (relative.isEmpty() || !QDir::isRelativePath(relative)) return "Invalid imported image path.";
        imagePaths << relative;
        int depth = 0;
        for (const auto &part : relative.split('/')) {
            if (part == "..") {
                --depth;
                parentDepth = qMax(parentDepth, -depth);
            }
            else if (!part.isEmpty() && part != ".") ++depth;
        }
    }
    QStringList shaderDirectories;
    auto sourceDirectory = info.dir();
    for (int i = 0; i < parentDepth; ++i) {
        if (sourceDirectory.dirName().isEmpty()) return "Imported image path escapes the source filesystem.";
        shaderDirectories.prepend(sourceDirectory.dirName());
        if (!sourceDirectory.cdUp()) return "Invalid imported image path.";
    }

    // Stage the original relative layout and publish the whole bundle at once.
    QTemporaryDir staging(out.filePath(".isflab-XXXXXX"));
    if (!staging.isValid()) return "Cannot create staging directory.";
    const QDir bundle(staging.path());
    const auto shaderSubdirectory = shaderDirectories.join('/');
    const QDir shaderDirectory(shaderSubdirectory.isEmpty() ? bundle : QDir(bundle.filePath(shaderSubdirectory)));
    if (!QDir().mkpath(shaderDirectory.absolutePath())) return "Cannot create shader directory.";
    if (!writeNew(shaderDirectory.filePath(info.fileName()), entry.source)) return "Cannot write shader.";
    if (!vertex.isEmpty() && !QFile::copy(vertex, shaderDirectory.filePath(stem + ".vs")))
        return "Cannot copy vertex shader.";

    QMap<QString, QString> copiedImages;
    for (const auto &relative : imagePaths) {
        const QFileInfo image(info.dir().filePath(relative));
        const auto source = image.canonicalFilePath();
        const auto target = QDir::cleanPath(shaderDirectory.filePath(relative));
        if (!target.startsWith(bundle.absolutePath() + "/") || !image.isFile())
            return "Cannot copy imported image: " + relative;
        // The unchanged PATH must resolve even when it walks through a directory
        // and back out of it (for example, textures/../asset.png).
        auto directory = shaderDirectory.absolutePath();
        const auto components = relative.split('/');
        for (qsizetype i = 0; i + 1 < components.size(); ++i) {
            directory = QDir::cleanPath(directory + "/" + components[i]);
            if ((directory != bundle.absolutePath() && !directory.startsWith(bundle.absolutePath() + "/"))
                || !QDir().mkpath(directory))
                return "Cannot reproduce imported image path: " + relative;
        }
        if (copiedImages.contains(target) && copiedImages.value(target) == source) continue;
        if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(source, target))
            return "Cannot copy imported image: " + relative;
        copiedImages.insert(target, source);
    }
    const auto target = uniquePath(out, stem, {});
    if (!out.rename(staging.path(), target)) return "Cannot publish shader bundle.";
    created = QDir(target).filePath(bundle.relativeFilePath(shaderDirectory.filePath(info.fileName())));
    return {};
}
}

QStringList shaderFiles(const QString &folder, bool skipOrganized) {
    QStringList paths;
    collect(QDir(folder), paths, skipOrganized);
    return paths;
}

QString copyShaderToFolder(const ShaderEntry &entry, const QString &destination, QString &created) {
    return copyEntry(entry, QDir(destination), created);
}

ExportReport exportShaders(const QList<ShaderEntry> &entries, const QString &destination) {
    ExportReport report;
    QDir root(destination);
    for (const auto &folder : folders) {
        const QFileInfo info(root.filePath(folder));
        if (info.isSymLink() || (!info.isDir() && !root.mkdir(folder)))
            report.errors << "Cannot use output folder: " + info.absoluteFilePath();
    }
    if (!report.errors.isEmpty()) return report;
    for (const auto &entry : entries) {
        if (!entry.header || entry.type() == ISFType::MadMapper) { ++report.skipped; continue; }
        QString created;
        const auto error = copyEntry(entry, QDir(root.filePath(ISFScanner::typeFolder(entry.type()))), created);
        if (error.isEmpty()) { ++report.copied; report.copiedFiles.insert(entry.path, created); }
        else report.errors << entry.path + ": " + error;
    }
    return report;
}

std::optional<QByteArray> shaderFingerprint(const ShaderEntry &entry) {
    if (!entry.header || entry.madMapper) return {};
    static const QRegularExpression includes(R"rx(^\s*#\s*include\b)rx",
                                              QRegularExpression::MultilineOption);
    if (includes.match(QString::fromUtf8(entry.source)).hasMatch()) return {};
    QFile fragment(entry.path);
    if (!fragment.open(QIODevice::ReadOnly) || fragment.readAll() != entry.source ||
        fragment.error() != QFileDevice::NoError) return {};

    QCryptographicHash fingerprint(QCryptographicHash::Sha256);
    fingerprint.addData(QCryptographicHash::hash(entry.source, QCryptographicHash::Sha256));
    const auto vertex = ISFScanner::companionVertexPath(entry.path);
    fingerprint.addData(vertex.isEmpty() ? QByteArrayView("0") : QByteArrayView("1"));
    if (!vertex.isEmpty()) {
        QFile file(vertex);
        if (!file.open(QIODevice::ReadOnly)) return {};
        const auto code = file.readAll();
        if (file.error() != QFileDevice::NoError || includes.match(QString::fromUtf8(code)).hasMatch()) return {};
        fingerprint.addData(QCryptographicHash::hash(code, QCryptographicHash::Sha256));
    }
    const auto imports = entry.header->value("IMPORTED");
    if (!imports.isUndefined() && !imports.isObject()) return {};
    const auto folder = QFileInfo(entry.path).dir();
    for (const auto &item : imports.toObject()) {
        const auto relative = item.toObject().value("PATH").toString();
        if (relative.isEmpty() || !QDir::isRelativePath(relative)) return {};
        QFile image(folder.filePath(relative));
        QCryptographicHash imageHash(QCryptographicHash::Sha256);
        if (!image.open(QIODevice::ReadOnly) || !imageHash.addData(&image)) return {};
        fingerprint.addData(imageHash.result());
    }
    return fingerprint.result();
}
