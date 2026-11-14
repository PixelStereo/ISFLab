// ISF header parsing, repair and classification: pure functions of the source bytes.
#include "ISFScanner.h"
#include <QJsonArray>
#include <cmath>

namespace {
void require(bool condition, const char *message) {
    if (!condition) qFatal("%s", message);
}
}

int main() {
    const QByteArray shader = "// leading comment\n/*{\"INPUTS\":[]}*/\nvoid main() {}\n";
    const auto range = ISFScanner::headerRange(shader);
    require(range.isValid() && shader.mid(range.start, 2) == "/*" && shader.mid(range.end, 2) == "*/",
            "headerRange must span the header comment");
    require(shader.mid(range.bodyStart()).startsWith("\nvoid main"), "bodyStart must follow the header");
    require(!ISFScanner::headerRange("void main() {}").isValid(), "A shader without header has no range");
    require(!ISFScanner::headerRange("/*{ \"INPUTS\": [] ").isValid(), "An unterminated header has no range");

    ISFScanner::HeaderError error;
    require(!ISFScanner::header("/*{\"INPUTS\": {}}*/", &error) && error.message.contains("INPUTS"),
            "INPUTS must be an array");
    const QByteArray broken = "/*{\n  \"INPUTS\": [1, 2,],\n  \"CREDIT\": \"x\",\n}*/";
    const auto commas = ISFScanner::trailingCommaFix(broken);
    require(commas.size() == 2, "Both trailing commas must be found");
    auto repaired = broken;
    for (auto it = commas.crbegin(); it != commas.crend(); ++it) repaired.remove(*it, 1);
    require(ISFScanner::header(repaired).has_value(), "Removing the reported commas must validate the header");
    require(ISFScanner::trailingCommaFix("/*{\"A\": \"a,]\"}*/").isEmpty(), "Commas inside strings are not trailing");

    const QByteArray material = R"glsl(/*{
        "INPUTS": [{"NAME": "count", "TYPE": "int", "DEFAULT": 2.},
                   {"NAME": "shape", "TYPE": "long", "VALUES": ["Smooth", "In"], "DEFAULT": "In"}],
        "IMPORTED": [{"NAME": "lut", "PATH": "noiseLUT.png"}], // MadMapper comment
    }*/
    vec4 materialColorForPixel(vec2 uv) { return vec4(uv, 0., 1.); })glsl";
    require(ISFScanner::isMadMapperMaterial(material), "materialColorForPixel marks a Material");
    require(!ISFScanner::isMadMapperMaterial("// vec4 materialColorForPixel(vec2 uv) {\nvoid main() {}"),
            "A commented-out entry point is not a Material");
    const auto materialHeader = ISFScanner::materialHeader(material);
    require(materialHeader.has_value(), "The lenient Material parser accepts comments and 2. numbers");
    const auto inputs = materialHeader->value("INPUTS").toArray();
    require(inputs.at(0).toObject().value("TYPE") == "long", "Material int inputs become long");
    const auto shape = inputs.at(1).toObject();
    require(shape.value("LABELS").toArray().size() == 2 && shape.value("DEFAULT").toInt() == 1,
            "String choices become indices with labels");
    require(materialHeader->value("IMPORTED").toObject().contains("lut"), "IMPORTED arrays become objects");

    const auto classify = [](const QByteArray &json) { return ISFScanner::classify(*ISFScanner::header(json)); };
    require(classify(R"(/*{"INPUTS":[]}*/)") == ISFType::Generator, "No image input: generator");
    require(classify(R"(/*{"INPUTS":[{"NAME":"inputImage","TYPE":"image"}]}*/)") == ISFType::FX,
            "inputImage: FX");
    require(classify(R"(/*{"INPUTS":[{"NAME":"startImage","TYPE":"image"},{"NAME":"endImage","TYPE":"image"},
                                     {"NAME":"progress","TYPE":"float"}]}*/)") == ISFType::Transition,
            "start, end and progress: transition");
    require(ISFScanner::categories(*ISFScanner::header(R"(/*{"CATEGORIES":[" Noise ","noise","Blur",""]}*/)"))
                == QStringList({"Noise", "Blur"}),
            "Categories are trimmed and deduplicated case-insensitively");

    require(ISFScanner::isShaderFile("a/b.FS") && ISFScanner::isShaderFile("x.isf") && !ISFScanner::isShaderFile("x.vs"),
            "Shader files are .fs and .isf, in any case");
    require(ISFScanner::inputTypes().contains("audioFFT") && !ISFScanner::defaultableInputTypes().contains("image"),
            "Input type lists");
    return 0;
}
