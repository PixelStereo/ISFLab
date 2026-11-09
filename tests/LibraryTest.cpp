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
    const QByteArray material = "/*{ invalid material metadata }*/\nvec4 materialColorForPixel(vec2 uv) { return vec4(uv, 0., 1.); }";
    require(ShaderEntry{"material.fs", material, ISFScanner::header(material)}.type() == ISFType::MadMapper,
            "MadMapper entry point should take precedence over the ISF header");
    const auto materialOptions = ISFScanner::materialHeader(R"(/*{"INPUTS":[
        {"TYPE":"long","NAME":"shape","VALUES":["In","Out","Smooth"],"DEFAULT":"Smooth"},
    ]}*/)");
    require(materialOptions.has_value(), "Material options header should parse");
    const auto shape = materialOptions->value("INPUTS").toArray().first().toObject();
    require(shape.value("DEFAULT").toInt(-1) == 2 &&
            shape.value("LABELS").toArray().at(2).toString() == "Smooth",
            "Material string choices should preserve the selected shape");
    require(!ISFScanner::isMadMapperMaterial("// vec4 materialColorForPixel(vec2 uv) {}\nvoid main() {}"),
            "Commented examples should not classify a shader as a Material");

    require(ShaderEntry{"plain.fs", "/* GLSL comment */ void main() {}", {}}.type() == ISFType::NonISF,
            "Plain fragment should be classified separately");
    require(ShaderEntry{"broken.fs", "/* { \"INPUTS\": } */", {}}.type() == ISFType::Unknown,
            "Malformed metadata should be reported as an error");
    require(ShaderEntry{"broken.isf", "void main() {}", {}}.type() == ISFType::Unknown,
            "Explicit ISF with no header should be reported as an error");

    const QByteArray credited = "// Zebre — credit\n/* Attribution */\n/*{\"INPUTS\":[]}*/\nvoid main() {}";
    require(ISFScanner::header(credited).has_value() && ISFScanner::headerStart(credited) == credited.indexOf("/*{"),
            "ISF metadata may follow a comment preamble");

    const QByteArray commas = R"(// Crédit
/*{"DESCRIPTION":"Keep ,] and ,} inside the string","INPUTS":[{"NAME":"level",},],}*/
void main() {})";
    ISFScanner::HeaderError diagnostic;
    require(!ISFScanner::header(commas, &diagnostic) && diagnostic.byteOffset > 0,
            "Invalid JSON should have a source location");
    const auto fixes = ISFScanner::trailingCommaFix(commas);
    require(fixes.size() == 3, "Expected only the three trailing commas outside strings");
    auto repaired = commas;
    for (auto it = fixes.crbegin(); it != fixes.crend(); ++it) repaired.remove(*it, 1);
    const auto repairedHeader = ISFScanner::header(repaired);
    require(repairedHeader && repairedHeader->value("DESCRIPTION").toString() == "Keep ,] and ,} inside the string",
            "Trailing comma repair modified a string");
    require(ISFScanner::trailingCommaFix(R"(/*{"INPUTS":[,]}*/)").isEmpty(),
            "An empty array entry should not be repaired as a trailing comma");

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
    const ShaderEntry original{dir.filePath("src/sub/Resource.fs"), bundled, header};
    const ShaderEntry duplicate{bundle + "Resource.fs", bundled, header};
    const auto fingerprint = shaderFingerprint(original);
    require(fingerprint && shaderFingerprint(duplicate) == fingerprint,
            "Identical shaders and dependency bytes should match");
    require(QFile::remove(bundle + path), "Could not replace the exported test image");
    write(bundle + path, "different image bytes");
    require(shaderFingerprint(duplicate) && shaderFingerprint(duplicate) != fingerprint,
            "Shaders with different imported images must not match");
    require(QFile::remove(bundle + path) && !shaderFingerprint(duplicate),
            "Missing dependencies must not match");
    write(bundle + path, "image bytes");
    require(ISFScanner::categories(*ISFScanner::header(fx)).contains("Color"), "Missing category");
    require(!ISFScanner::header("void main() {} /*{}*/"), "JSON comment must lead source");
    require(ISFScanner::classify(*ISFScanner::header(R"(/*{"INPUTS":[{"NAME":"inputImage","TYPE":"float"}]}*/)")) ==
            ISFType::Generator, "A scalar is not an image input");
    require(writeLibraryZip(dir.filePath("out"), QDir::current().filePath("test-library.zip")).isEmpty(), "ZIP export failed");
    require(exportShaders(entries, dir.filePath("src")).copied == 5, "In-place organization failed");
    require(shaderFiles(dir.filePath("src"), true) == paths, "Organization scanned its own output or moved originals");
}
