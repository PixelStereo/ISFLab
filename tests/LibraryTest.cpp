#include "Library.h"
#include <QJsonArray>
#include "ZipExport.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdlib>

namespace {
void require(bool condition, const char *message) {
    if (!condition) qFatal("%s", message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Missing test file");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size(), "Cannot create fixture");
}
}

int main() {
    QTemporaryDir root;
    require(root.isValid(), "Missing temporary directory");
    QDir dir(root.path());
    require(dir.mkpath("src/sub/textures") && dir.mkdir("out"), "Cannot create fixtures");
    const auto example = QString::fromUtf8(ISFLAB_EXAMPLES) + "/";
    const auto generator = read(example + "ColorWave.fs");
    const auto fx = read(example + "Invert.fs");
    const auto transition = read(example + "Crossfade.fs");
    const QByteArray bundled = R"(/*{"IMPORTED":{"tex":{"PATH":"textures/../../shared.png"}}}*/ void main(){})";
    const QByteArray invalid = "/* invalid */";
    const QList<QPair<QString, QByteArray>> fixtures = {
        {"src/Name.FS", generator}, {"src/sub/Name.FS", generator},
        {"src/Invert.fs", fx}, {"src/Crossfade.fs", transition},
        {"src/sub/Resource.fs", bundled}, {"src/Invalid.fs", invalid}
    };
    for (const auto &fixture : fixtures) write(dir.filePath(fixture.first), fixture.second);
    write(dir.filePath("src/sub/Resource.VS"), "vertex source");
    write(dir.filePath("src/shared.png"), "image bytes");

    const auto paths = shaderFiles(dir.filePath("src"), true);
    require(paths.size() == 6, "Recursive scan, including uppercase files");
    QList<ShaderEntry> entries;
    for (const auto &path : paths) {
        const auto bytes = read(path);
        entries << ShaderEntry{path, bytes, ISFScanner::header(bytes)};
    }
    const auto report = exportShaders(entries, dir.filePath("out"));
    require(report.copied == 5 && report.skipped == 1 && report.errors.isEmpty(), "Incorrect export report");
    require(report.copiedFiles.size() == 5 &&
            report.copiedFiles.value(dir.filePath("src/sub/Resource.fs")) == dir.filePath("out/Generators/Resource/sub/Resource.fs"),
            "Report did not identify the published shader");
    require(read(dir.filePath("out/Generators/Name.FS")) == generator &&
            read(dir.filePath("out/Generators/Name_2.FS")) == generator, "Duplicate filename was lost");
    require(read(dir.filePath("out/FX/Invert.fs")) == fx, "Incorrect FX classification");
    require(read(dir.filePath("out/Transitions/Crossfade.fs")) == transition, "Incorrect transition classification");
    const auto bundle = dir.filePath("out/Generators/Resource/sub/");
    require(read(bundle + "Resource.fs") == bundled, "Export modified the fragment shader");
    const auto header = ISFScanner::header(read(bundle + "Resource.fs"));
    require(header.has_value(), "Exported shader header is invalid");
    const auto path = header->value("IMPORTED").toObject().value("tex").toObject().value("PATH").toString();
    require(read(bundle + path) == "image bytes" && read(bundle + "Resource.vs") == "vertex source", "Dependency was lost");
    require(ISFScanner::categories(*ISFScanner::header(fx)).contains("Color"), "Missing category");
    require(!ISFScanner::header("void main() {} /*{}*/"), "JSON comment must lead source");
    require(ISFScanner::classify(*ISFScanner::header(R"(/*{"INPUTS":[{"NAME":"inputImage","TYPE":"float"}]}*/)")) ==
            ISFType::Generator, "A scalar is not an image input");
    require(writeLibraryZip(dir.filePath("out"), QDir::current().filePath("test-library.zip")).isEmpty(), "ZIP export failed");
    require(exportShaders(entries, dir.filePath("src")).copied == 5, "In-place organization failed");
    require(shaderFiles(dir.filePath("src"), true) == paths, "Organization scanned its own output or moved originals");
}
