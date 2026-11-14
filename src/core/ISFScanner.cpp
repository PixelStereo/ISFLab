#include "ISFScanner.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>

namespace ISFScanner {

std::optional<QJsonObject> header(const QByteArray &source, HeaderError *error) {
    if (error) *error = {};
    const auto fail = [error](const QString &message, qsizetype offset) -> std::optional<QJsonObject> {
        if (error) *error = {message, offset};
        return std::nullopt;
    };
    const auto range = headerRange(source);
    if (range.start < 0) return fail("No ISF JSON header found.", 0);
    if (!range.isValid()) return fail("The ISF header comment is missing */.", range.start);
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(source.mid(range.jsonStart(), range.jsonLength()), &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(parseError.errorString(),
                    qBound(range.jsonStart(), range.jsonStart() + parseError.offset, range.end));
    if (!doc.isObject()) return fail("The ISF header must be a JSON object.", range.jsonStart());
    const auto object = doc.object();
    if (object.contains("INPUTS") && !object.value("INPUTS").isArray())
        return fail("INPUTS must be an array.", range.jsonStart());
    return object;
}

QList<qsizetype> trailingCommaFix(const QByteArray &source) {
    const auto range = headerRange(source);
    if (!range.isValid()) return {};
    const auto text = QString::fromUtf8(source.mid(range.jsonStart(), range.jsonLength()));
    static const QRegularExpression tokens(R"rx("(?:\\.|[^"\\])*"|,(?=[ \t\r\n]*[}\]]))rx");
    QList<qsizetype> positions;
    auto matches = tokens.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (match.capturedView() != u",") continue;
        auto previous = match.capturedStart() - 1;
        while (previous >= 0 && text.at(previous).isSpace()) --previous;
        if (previous < 0 || QStringView(u"{[,:").contains(text.at(previous))) continue;
        positions.append(range.jsonStart() + text.left(match.capturedStart()).toUtf8().size());
    }
    if (positions.isEmpty()) return {};
    auto repaired = source;
    for (auto it = positions.crbegin(); it != positions.crend(); ++it) repaired.remove(*it, 1);
    return header(repaired) ? positions : QList<qsizetype>{};
}

qsizetype headerStart(const QByteArray &source) {
    qsizetype offset = source.startsWith("\xef\xbb\xbf") ? 3 : 0;
    const auto skipSpace = [&] {
        while (offset < source.size() && QByteArrayView(" \t\r\n\f\v").contains(source.at(offset))) ++offset;
    };
    while (offset < source.size()) {
        skipSpace();
        const auto remaining = QByteArrayView(source).sliced(offset);
        if (remaining.startsWith("//")) {
            const auto lineEnd = source.indexOf('\n', offset + 2);
            if (lineEnd < 0) return -1;
            offset = lineEnd + 1;
            continue;
        }
        if (!remaining.startsWith("/*")) return -1;
        const auto start = offset;
        offset += 2;
        skipSpace();
        if (offset < source.size() && source.at(offset) == '{') return start;
        const auto end = source.indexOf("*/", start + 2);
        if (end < 0) return -1;
        offset = end + 2;
    }
    return -1;
}

HeaderRange headerRange(const QByteArray &source) {
    HeaderRange range;
    range.start = headerStart(source);
    if (range.start >= 0) range.end = source.indexOf("*/", range.start + 2);
    return range;
}

std::optional<QJsonObject> materialHeader(const QByteArray &source) {
    auto metadata = header(source);
    if (!metadata) {
        const auto range = headerRange(source);
        if (!range.isValid()) return {};
        auto json = QString::fromUtf8(source.mid(range.jsonStart(), range.jsonLength()));
        // Preserve quoted text while accepting the comments and decimal forms
        // found in MadMapper headers. Strict ISF parsing remains separate.
        static const QRegularExpression tokens(
            R"rx("(?:\\.|[^"\\])*"|//[^\r\n]*|(?<![\w.])(?:[-+]?\d+\.(?![\d.])|[-+]?\.\d+|\+\d+)(?:[eE][-+]?\d+)?)rx");
        struct Replacement { qsizetype start, length; QString text; };
        QList<Replacement> replacements;
        auto matches = tokens.globalMatch(json);
        while (matches.hasNext()) {
            const auto match = matches.next();
            auto value = match.captured();
            if (value.startsWith('"')) continue;
            if (value.startsWith("//")) value.fill(' ');
            else {
                if (value.startsWith('+')) value.remove(0, 1);
                if (value.startsWith('.')) value.prepend('0');
                else if (value.startsWith("-.")) value.insert(1, '0');
                const auto dot = value.indexOf('.');
                if (dot >= 0 && (dot + 1 == value.size() || value.at(dot + 1).toLower() == 'e'))
                    value.insert(dot + 1, '0');
            }
            replacements.append({match.capturedStart(), match.capturedLength(), value});
        }
        for (auto it = replacements.crbegin(); it != replacements.crend(); ++it)
            json.replace(it->start, it->length, it->text);
        auto repaired = QByteArray("/*") + json.toUtf8() + "*/";
        for (const auto offset : trailingCommaFix(repaired)) repaired[offset] = ' ';
        metadata = header(repaired);
    }
    if (!metadata) return {};
    // MadMapper spells integer inputs "int". The shared preview uses ISF's "long".
    if (metadata->value("INPUTS").isArray()) {
        auto inputs = metadata->value("INPUTS").toArray();
        for (qsizetype i = 0; i < inputs.size(); ++i) {
            auto input = inputs.at(i).toObject();
            if (input.value("TYPE") == QJsonValue("int")) input.insert("TYPE", "long");
            const auto choices = input.value("VALUES").toArray();
            if (input.value("TYPE") == QJsonValue("long") && !choices.isEmpty() &&
                std::all_of(choices.begin(), choices.end(), [](const auto &choice) { return choice.isString(); })) {
                QJsonArray indices;
                for (qsizetype index = 0; index < choices.size(); ++index) indices.append(int(index));
                int initial = 0;
                for (qsizetype index = 0; index < choices.size(); ++index)
                    if (choices.at(index) == input.value("DEFAULT")) initial = int(index);
                input.insert("VALUES", indices);
                input.insert("LABELS", choices);
                input.insert("DEFAULT", initial);
            }
            inputs.replace(i, input);
        }
        metadata->insert("INPUTS", inputs);
    }
    if (metadata->value("IMPORTED").isArray()) {
        QJsonObject imports;
        for (const auto &value : metadata->value("IMPORTED").toArray()) {
            const auto image = value.toObject();
            const auto name = image.value("NAME").toString();
            if (!name.isEmpty()) imports.insert(name, image);
        }
        metadata->insert("IMPORTED", imports);
    }
    return metadata;
}

bool isMadMapperMaterial(const QByteArray &source) {
    if (!source.contains("materialColorForPixel")) return false;
    static const QRegularExpression comments(R"rx(/\*[\s\S]*?\*/|//[^\r\n]*)rx");
    static const QRegularExpression entry(
        R"rx(\bvec4\s+materialColorForPixel\s*\(\s*(?:in\s+)?vec2\s+\w+\s*\)\s*\{)rx");
    auto code = QString::fromUtf8(source);
    code.replace(comments, " ");
    return entry.match(code).hasMatch();
}

ISFType classify(const QJsonObject &header) {
    bool input = false, start = false, end = false, progress = false;
    for (const auto &value : header.value("INPUTS").toArray()) {
        const auto entry = value.toObject();
        const auto name = entry.value("NAME").toString();
        const auto type = entry.value("TYPE").toString();
        input |= name == "inputImage" && type == "image";
        start |= name == "startImage" && type == "image";
        end |= name == "endImage" && type == "image";
        progress |= name == "progress" && type == "float";
    }
    if (start && end && progress) return ISFType::Transition;
    return input ? ISFType::FX : ISFType::Generator;
}

QStringList categories(const QJsonObject &header) {
    QStringList result;
    for (const auto &value : header.value("CATEGORIES").toArray()) {
        const auto name = value.toString().trimmed();
        if (!name.isEmpty() && !result.contains(name, Qt::CaseInsensitive)) result << name;
    }
    return result;
}

QString typeToString(ISFType type) {
    switch (type) {
    case ISFType::Generator: return "Generator";
    case ISFType::FX: return "FX";
    case ISFType::Transition: return "Transition";
    case ISFType::NonISF: return "Non-ISF fragment";
    case ISFType::MadMapper: return "MadMapper Material";
    default: return "Invalid header";
    }
}

QString typeFolder(ISFType type) {
    switch (type) {
    case ISFType::Generator: return "Generators";
    case ISFType::FX: return "FX";
    case ISFType::Transition: return "Transitions";
    default: return {};
    }
}

QString companionVertexPath(const QString &shaderPath) {
    if (shaderPath.isEmpty()) return {};
    const QFileInfo shader(shaderPath);
    const auto stem = shader.completeBaseName();
    const auto directory = shader.dir();
    const auto conventional = directory.filePath(stem + ".vs");
    if (QFileInfo(conventional).isFile()) return conventional;
    for (const auto &name : directory.entryList(QDir::Files | QDir::Hidden, QDir::Name)) {
        const QFileInfo candidate(name);
        if (candidate.completeBaseName() == stem &&
            candidate.suffix().compare("vs", Qt::CaseInsensitive) == 0)
            return directory.filePath(name);
    }
    return {};
}

bool isShaderFile(const QString &path) {
    const auto suffix = QFileInfo(path).suffix();
    return suffix.compare("fs", Qt::CaseInsensitive) == 0 || suffix.compare("isf", Qt::CaseInsensitive) == 0;
}

QString shaderFileFilter() { return "ISF shaders (*.fs *.isf *.FS *.ISF)"; }

const QStringList &inputTypes() {
    static const QStringList types{"event", "bool", "long", "float", "point2D", "point3D",
                                   "floatRange", "color", "image", "audio", "audioFFT"};
    return types;
}

const QStringList &defaultableInputTypes() {
    static const QStringList types{"float", "long", "bool", "point2D", "point3D", "floatRange", "color"};
    return types;
}
} // namespace ISFScanner
