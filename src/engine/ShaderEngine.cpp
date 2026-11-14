#include "ShaderEngine.h"
#include "ISFScanner.h"
#include "DimensionExpression.h"
#include "MaterialSource.h"
#include "TestPatterns.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QOpenGLShaderProgram>
#include <QOpenGLFramebufferObject>
#include <QOpenGLTexture>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <QRegularExpression>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLPaintDevice>
#include <QVector3D>

static void initializeMaterialResources() {
    Q_INIT_RESOURCE(madmapper);
}


struct ShaderEngine::ShaderInput {
    ShaderEngine engine;
    std::unique_ptr<QOpenGLFramebufferObject> output;
    QString status;
    bool initialized = false;
    bool usable = false;

    ~ShaderInput() {
        if (initialized) engine.release();
    }
};

void ShaderEngine::reportCompiled() { emit status(QStringLiteral("Preview compiled"), true); }
void ShaderEngine::reportError(const QString &message) { emit status(message, false); }

ShaderEngine::ShaderEngine() { clock.start(); materialClock.start(); }
ShaderEngine::~ShaderEngine() = default;

void ShaderEngine::release() {
    shaderInputs.clear();
    liveInput.reset();
    textures.clear();
    renderPasses.clear();
    fixedOutput.reset();
    program.reset();
    if (vao) {
        QOpenGLContext::currentContext()->extraFunctions()->glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    glDeleteBuffers(1, &vbo);
    vbo = 0;
}

void ShaderEngine::load(const QByteArray &code, const QJsonObject &metadata, const QString &file) {
    emit glslDiagnostic({}, {}, {}, 0);
    const bool nextFx = !code.isEmpty() && ISFScanner::classify(metadata) == ISFType::FX;
    if (path != file || !nextFx) {
        liveInput.reset();
        lastVideo = {};
    }
    source = code;
    header = metadata;
    path = file;
    values.clear();
    eventCounts.clear();
    materialChoices.clear();
    materialDefines.clear();
    const bool material = ISFScanner::isMadMapperMaterial(code);
    for (const auto &entry : header.value("INPUTS").toArray()) {
        const auto input = entry.toObject();
        const auto name = input.value("NAME").toString();
        if (name.isEmpty()) continue;
        const auto type = input.value("TYPE").toString();
        const auto value = input.value("DEFAULT");
        if (material) {
            if (type == "long") materialChoices.insert(name, input.value("LABELS").toArray());
            if ((type == "long" || type == "bool") &&
                input.value("FLAGS").toString().split(',').contains("generate_as_define"))
                materialDefines.insert(name);
        }
        if (type == "float") values.insert(name, value.toDouble());
        else if (type == "long") {
            const auto choices = input.value("VALUES").toArray();
            int initial = value.toInt();
            if (!choices.isEmpty() && !choices.contains(QJsonValue(initial))) initial = choices.first().toInt();
            values.insert(name, initial);
        } else if (type == "bool") values.insert(name, value.toBool(value.toInt() != 0));
        else if (type == "event") {
            values.insert(name, false);
            eventCounts.insert(name, 0);
        }
        else if (type == "point2D" || type == "floatRange" || type == "point3D" || type == "color") {
            const auto components = value.toArray();
            const auto coordinate = [&](int index) {
                const double fallback = type == "color" ? 1.0 : 0.0;
                return components.size() > index ? components.at(index).toDouble(fallback) : fallback;
            };
            if (type == "point2D" || type == "floatRange") values.insert(name, QVariant::fromValue(QVector2D(coordinate(0), coordinate(1))));
            else if (type == "point3D")
                values.insert(name, QVariant::fromValue(QVector3D(coordinate(0), coordinate(1), coordinate(2))));
            else values.insert(name, QVariant::fromValue(QVector4D(qBound(0., coordinate(0), 1.),
                qBound(0., coordinate(1), 1.), qBound(0., coordinate(2), 1.), qBound(0., coordinate(3), 1.))));
        }
    }
    images.clear();
    shaderInputs.clear();
    lastInputStatus.clear();
    fxInput = nextFx;
    filterEnabled = true;
    transitionAnimated = false;
    transitionOffset = 0.0;
    videoFrame = {};
    videoClock.invalidate();
    dirty = imagesDirty = true;
    seek(0);
}

QSize ShaderEngine::renderSize(const QSize &viewportSize) const {
    return fixedRenderSize.isEmpty() ? viewportSize : fixedRenderSize;
}

void ShaderEngine::setFixedRenderSize(const QSize &size) {
    if (fixedRenderSize == size) return;
    fixedRenderSize = size;
    resetHistory = true;
    if (fixedOutputError && program) reportCompiled();
    fixedOutputError = false;
}

void ShaderEngine::setPlaying(bool value) { playing = value; clock.restart(); }
void ShaderEngine::setFilterEnabled(bool enabled) {
    filterEnabled = enabled;
    videoClock.invalidate();
    clock.restart();
    resetHistory = true;
}

void ShaderEngine::seek(double value) {
    resetHistory = true;
    generators.reset();
    videoClock.invalidate();
    time = value;
    frame = 0;
    clock.restart();
    for (auto &entry : shaderInputs) entry.second->engine.seek(value);
    emit timeChanged(time);
}
void ShaderEngine::setValue(const QString &name, const QVariant &value) {
    if (materialDefines.contains(name) && values.value(name) != value) dirty = true;
    if (auto event = eventCounts.find(name); event != eventCounts.end() && value.toBool()) ++event.value();
    values[name] = value;
}
QVariant ShaderEngine::value(const QString &name) const {
    if (transitionAnimated && name == "progress") {
        const double elapsed = time + transitionOffset;
        const double phase = elapsed - 6.0 * std::floor(elapsed / 6.0);
        return qBound(0.0, qMin((phase - 1.0) / 2.0, (6.0 - phase) / 2.0), 1.0);
    }
    return values.value(name);
}

void ShaderEngine::setTransitionAnimation(bool enabled) {
    if (transitionAnimated == enabled) return;
    if (enabled) {
        const double progress = qBound(0.0, values.value("progress").toDouble(), 1.0);
        transitionOffset = (progress > 0.0 ? 1.0 + 2.0 * progress : 0.0) - time;
    } else {
        values.insert("progress", value("progress"));
    }
    transitionAnimated = enabled;
}

void ShaderEngine::setLiveInput(std::unique_ptr<LiveInput> input) {
    shaderInputs.erase("inputImage");
    imagesDirty = true;
    liveInput = std::move(input);
    images.remove("inputImage");
    lastVideo = {};
    lastInputStatus.clear();
    videoClock.invalidate();
}

void ShaderEngine::useTestCard() { setLiveInput(nullptr); }

void ShaderEngine::setShaderInput(const QString &name, const QByteArray &code,
                                  const QJsonObject &metadata, const QString &file) {
    if (code.isEmpty() || ISFScanner::classify(metadata) != ISFType::Generator) return;
    if (name == "inputImage" && fxInput) setLiveInput(nullptr);
    images.remove(name);
    imagesDirty = true;
    auto input = std::make_unique<ShaderInput>();
    auto *state = input.get();
    connect(&state->engine, &ShaderEngine::status, &state->engine,
            [state](const QString &message) { state->status = message; });
    state->engine.load(code, metadata, file);
    shaderInputs[name] = std::move(input);
}

bool ShaderEngine::needsAudio() const {
    for (const auto &entry : header.value("INPUTS").toArray()) {
        const auto type = entry.toObject().value("TYPE").toString();
        if (type == "audio" || type == "audioFFT") return true;
    }
    return std::any_of(shaderInputs.begin(), shaderInputs.end(),
        [](const auto &entry) { return entry.second->engine.needsAudio(); });
}

void ShaderEngine::setAudioFrame(std::shared_ptr<const AudioFrame> frame) {
    audioFrame = std::move(frame);
}

void ShaderEngine::renderShaderInputs(GLuint framebuffer, const QSize &viewportSize) {
    const QSize dimensions(640, 360);
    for (auto &[name, input] : shaderInputs) {
        if (!input->initialized) {
            input->engine.initialize();
            input->initialized = true;
            input->output = std::make_unique<QOpenGLFramebufferObject>(dimensions);
            if (!input->output->isValid()) {
                emit shaderInputError(name, "Could not allocate the source shader preview.");
                continue;
            }
        }
        if (!input->output->isValid()) continue;
        if (input->engine.isPlaying() != playing) input->engine.setPlaying(playing);
        input->engine.setAudioFrame(audioFrame);
        input->engine.render(input->output->handle(), dimensions);
        const bool usable = input->engine.program && input->engine.bufferError.isEmpty();
        if (!usable && !input->status.isEmpty())
            emit shaderInputError(name, std::exchange(input->status, QString{}));
        if (usable != input->usable && fxInput && name == "inputImage") {
            lastInputStatus.clear();
            videoClock.invalidate();
            emit testCardActiveChanged(!usable);
        }
        input->usable = usable;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, viewportSize.width(), viewportSize.height());
}

void ShaderEngine::setImage(const QString &name, const QImage &image) {
    shaderInputs.erase(name);
    if (name == "inputImage" && fxInput) {
        setLiveInput(nullptr);
        emit testCardActiveChanged(image.isNull());
    }
    if (image.isNull()) images.remove(name);
    else images[name] = image;
    imagesDirty = true;
}

namespace {
QImage glImage(const QImage &image) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return image.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Vertical);
#else
    return image.convertToFormat(QImage::Format_RGBA8888).mirrored();
#endif
}
}

void ShaderEngine::updateVideoInput(GLuint framebuffer) {
    const auto shader = shaderInputs.find("inputImage");
    if (shader != shaderInputs.end() && shader->second->usable) return;
    const bool refresh = !videoClock.isValid();
    if (!fxInput || images.contains("inputImage") || (!refresh && (!playing || videoClock.elapsed() < 33))) return;
    videoClock.start();
    QImage pixels;
    QString status;
    if (liveInput) {
        pixels = liveInput->frame(framebuffer);
        if (!pixels.isNull()) lastVideo = pixels;
        const auto error = liveInput->error();
        if (!error.isEmpty()) {
            status = error + " Showing the test card.";
            lastVideo = {};
        } else if (lastVideo.isNull()) {
            status = "Waiting for a live frame…";
        } else {
            status = "Live input received.";
            if (pixels.isNull()) {
                if (!refresh && status == lastInputStatus) return;
                pixels = lastVideo;
            }
        }
    } else {
        status = "Animated test card";
    }
    if (status != lastInputStatus) {
        lastInputStatus = status;
        emit inputStatus(status);
        emit testCardActiveChanged(pixels.isNull());
    }
    if (pixels.isNull()) pixels = TestPatterns::testCard(time);
    videoFrame = pixels;
    if (!filterEnabled) return;
    pixels = glImage(pixels);
    for (auto &[name, texture] : textures) {
        if (name != "inputImage") continue;
        if (texture->width() != pixels.width() || texture->height() != pixels.height()) {
            texture = std::make_unique<QOpenGLTexture>(pixels, QOpenGLTexture::DontGenerateMipMaps);
            texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
            texture->setWrapMode(QOpenGLTexture::ClampToEdge);
        } else {
            texture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, pixels.constBits());
        }
        break;
    }
}

int ShaderEngine::uniform(const QByteArray &name) {
    auto location = uniformLocations.find(name);
    if (location == uniformLocations.end()) location = uniformLocations.insert(name, program->uniformLocation(name));
    return location.value();
}

void ShaderEngine::initialize() {
    initializeOpenGLFunctions();
    const GLfloat vertices[] = {-1,-1, 3,-1, -1,3};
    if (QOpenGLContext::currentContext()->format().profile() == QSurfaceFormat::CoreProfile)
        QOpenGLContext::currentContext()->extraFunctions()->glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    dirty = imagesDirty = true;
    fixedOutputError = false;
    videoClock.invalidate();
}

void ShaderEngine::compile() {
    dirty = false;
    program.reset();
    bufferError.clear();
    textures.clear();
    types.clear();
    audioTextures.clear();
    if (source.isEmpty()) return;
    renderPasses.clear();
    resetHistory = true;
    const bool material = ISFScanner::isMadMapperMaterial(source);
    static const bool resourcesReady = [] { initializeMaterialResources(); return true; }();
    Q_UNUSED(resourcesReady);
    generators.clear();
    uniformLocations.clear();
    auto passes = header.value("PASSES").toArray();
    if (passes.isEmpty()) passes.append(QJsonObject{});
    if (passes.size() > 16) {
        reportError("Preview supports up to 16 passes.");
        return;
    }
    const auto *context = QOpenGLContext::currentContext();
    const auto format = context->format();
    const bool modern = !context->isOpenGLES() &&
        (format.profile() == QSurfaceFormat::CoreProfile ||
         (material && format.version() >= qMakePair(3, 2)));
    const QByteArray version = modern ? "#version 150\n" :
        context->isOpenGLES() ? "#version 100\nprecision highp float;\n" : "#version 120\n";
    QByteArray frag = R"(
uniform vec2 RENDERSIZE;
uniform float TIME, TIMEDELTA;
uniform vec4 DATE;
uniform int FRAMEINDEX, PASSINDEX;
varying vec2 isf_FragNormCoord;
#define vv_FragNormCoord isf_FragNormCoord
#define IMG_NORM_PIXEL(img, p) texture2D(img, p)
#define IMG_NORM_THIS_PIXEL(img) texture2D(img, isf_FragNormCoord)
#define IMG_THIS_NORM_PIXEL(img) texture2D(img, isf_FragNormCoord)
)";
    if (material) {
        frag += "uniform float CLOCK_TIME;\n";
        if (modern) frag += "#define IMG_NORM_PIXEL_LOD(img, uv, lod) textureLod(img, uv, lod)\n"
                            "#define IMG_TEXEL_FETCH(img, uv, lod) texelFetch(img, uv, int(lod))\n";
    }
    const QHash<QString, QString> glTypes = {
        {"float","float"}, {"bool","bool"}, {"event","bool"}, {"long","int"},
        {"point2D","vec2"}, {"floatRange","vec2"}, {"point3D","vec3"}, {"color","vec4"}, {"image","sampler2D"}, {"audio","sampler2D"}, {"audioFFT","sampler2D"}
    };
    const QRegularExpression identifier("^[a-zA-Z_][a-zA-Z_0-9]*$");
    const auto add = [&](const QString &name, const QString &type) {
        if (!identifier.match(name).hasMatch() || !glTypes.contains(type) || types.contains(name)) return;
        types[name] = glTypes.value(type);
        frag += "uniform " + types.value(name).toUtf8() + " " + name.toUtf8() + ";\n";
        if (types.value(name) == "sampler2D") frag += "uniform vec2 _isflab_size_" + name.toUtf8() + ";\n";
    };
    for (const auto &entry : header.value("INPUTS").toArray()) {
        const auto obj = entry.toObject();
        const auto name = obj.value("NAME").toString();
        const auto type = obj.value("TYPE").toString();
        add(name, type);
        if (type == "audio" || type == "audioFFT") {
            const int width = obj.value("MAX").toInt(material ? obj.value("SIZE").toInt() : 0);
            audioTextures.insert(name, {type == "audioFFT", qMax(0, width)});
        }
        if (materialDefines.contains(name)) {
            const auto selected = values.value(name);
            const auto choices = materialChoices.value(name);
            const auto options = obj.value("VALUES").toArray();
            const auto option = std::find(options.begin(), options.end(), QJsonValue(selected.toInt()));
            const auto index = option - options.begin();
            QString label = type == "bool" ? (selected.toBool() ? "true" : "false")
                : option != options.end() && index < choices.size() ? choices.at(index).toString() : selected.toString();
            label.replace(QRegularExpression("[^a-zA-Z_0-9]"), "_");
            const auto define = name + "_IS_" + label;
            if (identifier.match(define).hasMatch()) frag += "#define " + define.toUtf8() + " 1\n";
        }
    }
    if (material) {
        for (const auto &entry : header.value("GENERATORS").toArray()) {
            const auto generator = entry.toObject();
            const auto name = generator.value("NAME").toString();
            const auto type = generator.value("TYPE").toString();
            if (!MaterialGenerators::isSupportedType(type) || !identifier.match(name).hasMatch() || types.contains(name)) {
                reportError(QString("Unsupported Material generator: %1 (%2)").arg(name, type));
                return;
            }
            add(name, "float");
            generators.add(name, type, generator.value("PARAMS").toObject());
        }
    }
    if (const auto cycle = generators.order(); !cycle.isEmpty()) {
        reportError("Circular Material generator dependency: " + cycle);
        return;
    }
    QStringList missingImages;
    const auto imported = header.value("IMPORTED").toObject();
    for (auto it = imported.begin(); it != imported.end(); ++it) {
        const auto relative = it.value().toObject().value("PATH").toString();
        QImage image;
        if (!relative.isEmpty() && QDir::isRelativePath(relative))
            image.load(QFileInfo(path).dir().filePath(relative));
        if (material && image.isNull() && QFileInfo(relative).fileName() == "noiseLUT.png")
            image.load(materialResourceRoot + "noiseLUT.png");
        if (image.isNull()) {
            if (!material) {
                reportError("Missing imported image: " + relative);
                return;
            }
            missingImages << QString("%1 (%2)").arg(relative, it.key());
            image = QImage(256, 2, QImage::Format_RGBA8888);
            const bool lut = QFileInfo(relative).completeBaseName().contains("lut", Qt::CaseInsensitive);
            for (int x = 0; x < image.width(); ++x)
                for (int y = 0; y < image.height(); ++y)
                    image.setPixelColor(x, y, lut ? QColor(x, x, x) : QColor(128, 128, 128));
        }
        add(it.key(), "image");
        images[it.key()] = image;
    }
    for (int i = 0; i < passes.size(); ++i) {
        const auto pass = passes[i].toObject();
        const auto target = pass.value("TARGET").toString();
        const auto flag = [&](const char *key) {
            const auto value = pass.value(key);
            return value.toBool() || value.toDouble() > 0;
        };
        if (target.isEmpty() != (i == passes.size() - 1) ||
            (!target.isEmpty() && (!identifier.match(target).hasMatch() || types.contains(target)))) {
            reportError("Preview requires unique pass targets and a final output pass.");
            return;
        }
        renderPasses.push_back({target, flag("PERSISTENT"), flag("FLOAT"),
                                pass.value("WIDTH"), pass.value("HEIGHT"), {}, {}});
        if (!target.isEmpty()) add(target, "image");
    }
    const auto start = ISFScanner::headerStart(source);
    const auto end = start < 0 ? -1 : source.indexOf("*/", start + 2);
    if (end < 0) {
        reportError("Invalid ISF JSON header.");
        return;
    }
    const auto body = source.mid(end + 2);
    // GLSL 1.20 and GLSL ES 1.00 do not portably support token pasting.
    // Specialize the pixel helpers for each named ISF texture.
    QList<QPair<QRegularExpression, QString>> imageReplacements;
    for (auto it = types.cbegin(); it != types.cend(); ++it) {
        if (it.value() != "sampler2D") continue;
        const auto name = it.key();
        const auto size = "_isflab_size_" + name;
        const auto pixel = "_isflab_pixel_" + name;
        frag += "vec4 " + pixel.toUtf8() + "(vec2 p) { return texture2D(" + name.toUtf8() +
                ", p / " + size.toUtf8() + "); }\n";
        imageReplacements.emplaceBack(QRegularExpression("\\bIMG_SIZE\\s*\\(\\s*" + name + "\\s*\\)"), size);
        imageReplacements.emplaceBack(QRegularExpression("\\bIMG_THIS_PIXEL\\s*\\(\\s*" + name + "\\s*\\)"), "texture2D(" + name + ", isf_FragNormCoord)");
        imageReplacements.emplaceBack(QRegularExpression("\\bIMG_PIXEL\\s*\\(\\s*" + name + "\\s*,"), pixel + "(");
    }
    const auto specializeImageCalls = [&](QString shader) {
        for (const auto &[pattern, replacement] : imageReplacements)
            shader.replace(pattern, replacement);
        return shader.toUtf8();
    };
    const auto vertexPath = ISFScanner::companionVertexPath(path);
    QStringList includeFiles{path};
    QStringList vertexIncludes{vertexPath};
    const QByteArray textureCompatibility = modern ? "#define texture2D texture\n#define textureCube texture\n" :
        material ? "#define texture texture2D\n" : "";
    QByteArray vert = version + textureCompatibility;
    if (modern) vert += "#define varying out\n#define attribute in\n";
    vert += frag + R"(
attribute vec2 position;
void isf_vertShaderInit() {
    gl_Position = vec4(position, 0., 1.);
    isf_FragNormCoord = position * .5 + .5;
}
#define vv_vertShaderInit isf_vertShaderInit
)";
    if (vertexPath.isEmpty()) {
        vert += "void main() { isf_vertShaderInit(); }";
    } else {
        QFile vertexFile(vertexPath);
        if (!vertexFile.open(QIODevice::ReadOnly)) {
            reportError("Cannot read vertex shader: " + vertexPath);
            return;
        }
        auto vertexSource = vertexFile.readAll();
        if (vertexFile.error() != QFile::NoError) {
            reportError("Cannot read vertex shader: " + vertexPath);
            return;
        }
        if (vertexSource.startsWith("\xef\xbb\xbf")) vertexSource.remove(0, 3);
        auto vertexBody = QString::fromUtf8(vertexSource);
        if (material) {
            QString error;
            vertexBody = QString::fromUtf8(expandMaterialIncludes(vertexSource, vertexPath, 1, 0,
                                                                  vertexIncludes, error));
            if (!error.isEmpty()) {
                emit glslDiagnostic(DiagnosticStage::MaterialVertexInclude, vertexPath, error, 0);
                reportError(error);
                return;
            }
            static const QRegularExpression output(
                R"rx(^([ \t]*)out(?=[ \t]+\w+[ \t]+\w+[ \t]*(?:\[|;)))rx",
                QRegularExpression::MultilineOption);
            if (!modern) vertexBody.replace(output, "\\1varying");
        }
        vert += "\n#line 1\n" + specializeImageCalls(vertexBody);
        if (material)
            vert += "\nvoid main() { isf_vertShaderInit(); materialVsFunc(isf_FragNormCoord); }\n";
    }
    const int fragmentStartLine = source.left(end + 2).count('\n') + 1;
    auto fragmentBody = body;
    if (material) {
        QString error;
        fragmentBody = expandMaterialIncludes(body, path, fragmentStartLine, 0, includeFiles, error);
        if (!error.isEmpty()) {
            emit glslDiagnostic(DiagnosticStage::MaterialInclude, path, error, 0);
            reportError(error);
            return;
        }
        static const QRegularExpression input(
            R"rx(^([ \t]*)in(?=[ \t]+\w+[ \t]+\w+[ \t]*(?:\[|;)))rx",
            QRegularExpression::MultilineOption);
        auto materialBody = QString::fromUtf8(fragmentBody);
        if (!modern) materialBody.replace(input, "\\1varying");
        fragmentBody = materialBody.toUtf8();
        fragmentBody += "\nvoid main() { gl_FragColor = materialColorForPixel(isf_FragNormCoord); }\n";
    }
    QByteArray fragmentPrelude = version + textureCompatibility;
    if (modern) {
        fragmentPrelude += "#define varying in\n"
                           "out vec4 _isflab_fragColor;\n"
                           "#define gl_FragColor _isflab_fragColor\n";
    }
    frag.prepend(fragmentPrelude);
    frag += "\n#line " + QByteArray::number(fragmentStartLine) + "\n"
        + specializeImageCalls(QString::fromUtf8(fragmentBody));
    auto candidate = std::make_unique<QOpenGLShaderProgram>();
    const auto reportCompilerError = [&](const QString &stage, const QString &file, bool hasSource) {
        const QString log = candidate->log().trimmed();
        int line = 0;
        auto diagnosticFile = file;
        if (hasSource) {
            // Common OpenGL formats are ERROR: 0:42: ..., 0:42(5): ... and 0(42) : ... .
            static const QRegularExpression location(
                R"rx((?:^|\n)\s*(?:ERROR\s*:\s*)?(\d+)(?:\s*:\s*(\d+)\b|\((\d+)\)))rx",
                QRegularExpression::CaseInsensitiveOption);
            const auto match = location.match(log);
            if (match.hasMatch()) {
                const int sourceId = match.captured(1).toInt();
                if (sourceId == 0) line = match.captured(match.capturedLength(2) ? 2 : 3).toInt();
                else if (const auto &files = stage == DiagnosticStage::Vertex ? vertexIncludes : includeFiles;
                         material && sourceId < files.size()) {
                    diagnosticFile = files.at(sourceId);
                    line = match.captured(match.capturedLength(2) ? 2 : 3).toInt();
                }
            }
        }
        emit glslDiagnostic(stage, diagnosticFile, log, line);
        reportError(stage + " failed" + (log.isEmpty() ? "." : ": " + log));
    };
    if (!candidate->addShaderFromSourceCode(QOpenGLShader::Vertex, vert)) {
        reportCompilerError(DiagnosticStage::Vertex, vertexPath, !vertexPath.isEmpty());
        return;
    }
    if (!candidate->addShaderFromSourceCode(QOpenGLShader::Fragment, frag)) {
        reportCompilerError(DiagnosticStage::Fragment, path, true);
        return;
    }
    if (!candidate->link()) {
        reportCompilerError(DiagnosticStage::Linking, {}, false);
        return;
    }
    program = std::move(candidate);
    reportCompiled();
    if (!missingImages.isEmpty())
        emit glslDiagnostic(DiagnosticStage::MaterialWarning, path,
            "Preview uses substitute textures. Restore these files for the original appearance:\n" +
            missingImages.join('\n'), 0);
}

// Image inputs, IMPORTED images and the transition placeholders, uploaded once after each change.
// Audio inputs get a slot here and are filled by uploadAudioTextures.
bool ShaderEngine::createTextures() {
    imagesDirty = false;
    textures.clear();
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &limit);
    if (std::count(types.cbegin(), types.cend(), QString("sampler2D")) > limit) {
        program.reset();
        reportError("Too many textures for this GPU.");
        return false;
    }
    for (auto it = types.cbegin(); it != types.cend(); ++it) {
        if (it.value() != "sampler2D") continue;
        if (std::any_of(renderPasses.begin(), renderPasses.end(),
                        [&](const RenderPass &pass) { return pass.target == it.key(); })) continue;
        if (audioTextures.contains(it.key())) {
            textures.emplace_back(it.key(), nullptr);
            continue;
        }
        auto image = images.value(it.key());
        if (image.isNull() && ISFScanner::classify(header) == ISFType::Transition &&
            (it.key() == "startImage" || it.key() == "endImage"))
            image = TestPatterns::transitionCard(it.key() == "endImage");
        if (image.isNull()) {
            image = QImage(256, 256, QImage::Format_RGBA8888);
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x)
                    image.setPixelColor(x, y, it.key() == "endImage" ? QColor(y, 80, x) : QColor(x, y, 180));
        }
        auto texture = std::make_unique<QOpenGLTexture>(glImage(image), QOpenGLTexture::DontGenerateMipMaps);
        texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
        const auto imageSettings = header.value("IMPORTED").toObject().value(it.key()).toObject();
        texture->setWrapMode(imageSettings.value("GL_TEXTURE_WRAP").toString() == "REPEAT"
            ? QOpenGLTexture::Repeat : QOpenGLTexture::ClampToEdge);
        textures.emplace_back(it.key(), std::move(texture));
    }
    return true;
}

// One row per audio channel: the waveform around 0.5, or the FFT magnitudes, resampled to the width the
// input asks for (MAX). A width of 1 holds the RMS level.
void ShaderEngine::uploadAudioTextures(int textureLimit) {
    const bool changed = uploadedAudio != audioFrame;
    const bool useFloat = !QOpenGLContext::currentContext()->isOpenGLES() &&
                          QOpenGLContext::currentContext()->format().majorVersion() >= 3;
    for (auto &[name, texture] : textures) {
        const auto input = audioTextures.constFind(name);
        if (input == audioTextures.cend() || (!changed && texture)) continue;
        const QList<QList<float>> empty;
        const auto &channels = audioFrame ? (input->spectrum ? audioFrame->spectrum : audioFrame->wave) : empty;
        const int height = qMax(1, int(channels.size()));
        const int nativeWidth = input->spectrum ? 1024 : 2048;
        const int width = qMin(textureLimit, input->width > 0 ? input->width : nativeWidth);
        if (!texture || texture->width() != width || texture->height() != height) {
            texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
            texture->setFormat(useFloat ? QOpenGLTexture::RGBA32F : QOpenGLTexture::RGBA8_UNorm);
            texture->setSize(width, height);
            texture->allocateStorage(QOpenGLTexture::RGBA, useFloat ? QOpenGLTexture::Float32 : QOpenGLTexture::UInt8);
            texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
            texture->setWrapMode(QOpenGLTexture::ClampToEdge);
        }
        QList<float> pixels(width * height * 4, 1.f);
        for (int y = 0; y < height; ++y) {
            const QList<float> silence;
            const auto &samples = y < channels.size() ? channels.at(y) : silence;
            for (int x = 0; x < width; ++x) {
                float value = input->spectrum ? 0.f : 0.5f;
                if (!samples.isEmpty()) {
                    const int start = x * samples.size() / width;
                    const int end = qMax(start + 1, int((x + 1) * samples.size() / width));
                    float sum = 0.f;
                    for (int i = start; i < end; ++i) {
                        const float sample = samples.at(i);
                        sum += !input->spectrum && width == 1 ? sample * sample : sample;
                    }
                    const float mean = sum / (end - start);
                    value = input->spectrum ? mean : width == 1 ? std::sqrt(mean) : 0.5f + 0.5f * mean;
                }
                const auto offset = (y * width + x) * 4;
                pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = qBound(0.f, value, 1.f);
            }
        }
        if (useFloat) texture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::Float32, pixels.constData());
        else {
            QByteArray bytes(pixels.size(), Qt::Uninitialized);
            for (qsizetype i = 0; i < pixels.size(); ++i) bytes[i] = char(qRound(pixels.at(i) * 255.f));
            texture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, bytes.constData());
        }
    }
    uploadedAudio = audioFrame;
}

void ShaderEngine::render(GLuint screen, const QSize &viewportSize) {
    if (viewportSize.isEmpty()) return;
    glBindFramebuffer(GL_FRAMEBUFFER, screen);
    glViewport(0, 0, viewportSize.width(), viewportSize.height());
    glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    const QSize size = renderSize(viewportSize);
    if (!shaderInputs.empty()) renderShaderInputs(screen, viewportSize);
    if (fxInput && !filterEnabled) {
        if (playing) time += qMin(clock.restart() / 1000.0, 0.25);
        updateVideoInput(screen);
        const auto shader = shaderInputs.find("inputImage");
        const QImage input = shader != shaderInputs.end() && shader->second->usable
            ? shader->second->output->toImage() : images.value("inputImage", videoFrame);
        glBindFramebuffer(GL_FRAMEBUFFER, screen);
        const QSize fitted = size.scaled(viewportSize, Qt::KeepAspectRatio);
        QOpenGLPaintDevice device(viewportSize);
        {
            QPainter painter(&device);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(QRect(QPoint((viewportSize.width() - fitted.width()) / 2,
                                           (viewportSize.height() - fitted.height()) / 2), fitted), input);
        }
        emit timeChanged(time);
        return;
    }
    if (dirty) compile();
    if (!program) return;
    GLint textureLimit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &textureLimit);
    QHash<QString, double> variables;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        bool numeric = false;
        const double value = it.value().toDouble(&numeric);
        if (numeric) variables.insert(it.key(), value);
    }
    variables.insert("WIDTH", size.width());
    variables.insert("HEIGHT", size.height());
    const auto dimension = [&](const QJsonValue &expression, int fallback) {
        if (expression.isUndefined()) return fallback;
        const double value = expression.isDouble() ? expression.toDouble()
            : evaluateDimension(expression.toString(), variables);
        return std::isfinite(value) && value <= textureLimit && value >= 0
            ? qMax(1, int(value)) : 0;
    };
    const auto rejectBuffers = [&](const QString &message) {
        if (bufferError == message) return;
        bufferError = message;
        reportError(message);
    };
    if (!fixedRenderSize.isEmpty() && (size.width() > textureLimit || size.height() > textureLimit)) {
        rejectBuffers("Fixed resolution exceeds the GPU texture limit.");
        return;
    }
    constexpr qint64 bufferBudget = 256LL * 1024 * 1024;
    qint64 bytes = fixedRenderSize.isEmpty() ? 0 : qint64(size.width()) * size.height() * 4;
    std::vector<QSize> passSizes;
    for (const auto &pass : renderPasses) {
        const QSize passSize(pass.target.isEmpty() ? size : QSize(dimension(pass.width, size.width()),
                                                                 dimension(pass.height, size.height())));
        if (!pass.target.isEmpty()) {
            if (passSize.isEmpty() || passSize.width() > textureLimit || passSize.height() > textureLimit) {
                rejectBuffers("Invalid buffer dimensions for pass: " + pass.target);
                return;
            }
            bytes += qint64(passSize.width()) * passSize.height() * (pass.floatingPoint ? 32 : 8);
        }
        passSizes.push_back(passSize);
    }
    if (bytes > bufferBudget) {
        rejectBuffers("Preview buffers exceed 256 MiB. Try a smaller render size.");
        return;
    }
    if (!bufferError.isEmpty()) {
        bufferError.clear();
        reportCompiled();
    }
    if (fixedOutput && (fixedRenderSize.isEmpty() || fixedOutput->size() != size)) fixedOutput.reset();
    // Release all obsolete buffers before allocating replacements.
    for (size_t i = 0; i < renderPasses.size(); ++i) {
        auto &pass = renderPasses[i];
        if (pass.read && pass.read->size() != passSizes[i]) {
            pass.read.reset();
            pass.write.reset();
        }
    }
    if (!fixedRenderSize.isEmpty() && !fixedOutput) {
        if (fixedOutputError) return;
        if (QOpenGLFramebufferObject::hasOpenGLFramebufferBlit())
            fixedOutput = std::make_unique<QOpenGLFramebufferObject>(size);
        glBindFramebuffer(GL_FRAMEBUFFER, screen);
        if (!fixedOutput || !fixedOutput->isValid()) {
            fixedOutput.reset();
            fixedOutputError = true;
            reportError("Fixed resolution unavailable. Try a smaller size or switch to Auto.");
            return;
        }
        reportCompiled();
    }

    if (imagesDirty && !createTextures()) return;
    if (!audioTextures.isEmpty()) uploadAudioTextures(textureLimit);
    const float delta = playing && frame > 0 ? qMin(clock.restart() / 1000.0, 0.25) : 0.0;
    if (delta == 0) clock.restart();
    time += delta;
    updateVideoInput(screen);
    const auto now = QDateTime::currentDateTime();
    program->bind();
    program->setUniformValue(uniform("TIME"), float(time));
    program->setUniformValue(uniform("CLOCK_TIME"), float(materialClock.elapsed() / 1000.0));
    program->setUniformValue(uniform("TIMEDELTA"), delta);
    program->setUniformValue(uniform("FRAMEINDEX"), frame);
    program->setUniformValue(uniform("DATE"), float(now.date().year()), float(now.date().month()),
                             float(now.date().day()), now.time().msecsSinceStartOfDay() / 1000.f);
    for (auto it = values.begin(); it != values.end(); ++it) {
        const auto type = types.value(it.key());
        const auto name = it.key().toUtf8();
        if (type == "float") program->setUniformValue(uniform(name), value(it.key()).toFloat());
        else if (type == "int" || type == "bool") program->setUniformValue(uniform(name), it.value().toInt());
        else if (type == "vec2") program->setUniformValue(uniform(name), it.value().value<QVector2D>());
        else if (type == "vec3") program->setUniformValue(uniform(name), it.value().value<QVector3D>());
        else if (type == "vec4") program->setUniformValue(uniform(name), it.value().value<QVector4D>());
    }
    for (const auto &[name, output] : generators.evaluate({values, eventCounts, materialChoices, time, delta}))
        program->setUniformValue(uniform(name.toUtf8()), float(output));
    if (vao) QOpenGLContext::currentContext()->extraFunctions()->glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const int position = program->attributeLocation("position");
    program->enableAttributeArray(position);
    program->setAttributeBuffer(position, GL_FLOAT, 0, 2);
    const auto clearBuffer = [&](QOpenGLFramebufferObject &buffer) {
        glBindFramebuffer(GL_FRAMEBUFFER, buffer.handle());
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
    };
    for (size_t i = 0; i < renderPasses.size(); ++i) {
        auto &pass = renderPasses[i];
        if (pass.target.isEmpty()) continue;
        if (!pass.read) {
            QOpenGLFramebufferObjectFormat format;
            if (pass.floatingPoint) format.setInternalTextureFormat(GL_RGBA32F);
            pass.read = std::make_unique<QOpenGLFramebufferObject>(passSizes[i], format);
            pass.write = std::make_unique<QOpenGLFramebufferObject>(passSizes[i], format);
            if (!pass.read->isValid() || !pass.write->isValid()) {
                glBindFramebuffer(GL_FRAMEBUFFER, screen);
                program->disableAttributeArray(position);
    if (vao) QOpenGLContext::currentContext()->extraFunctions()->glBindVertexArray(0);
                program->release();
                for (auto &buffer : renderPasses) {
                    buffer.read.reset();
                    buffer.write.reset();
                }
                program.reset();
                reportError("Could not allocate preview pass buffers.");
                return;
            }
            clearBuffer(*pass.read);
        } else if (resetHistory || !pass.persistent) {
            clearBuffer(*pass.read);
        }
    }
    for (size_t i = 0; i < textures.size(); ++i) {
        auto &[name, texture] = textures[i];
        const auto shader = shaderInputs.find(name);
        QSize textureSize(texture->width(), texture->height());
        if (shader != shaderInputs.end() && shader->second->usable) {
            const auto &output = shader->second->output;
            glActiveTexture(GL_TEXTURE0 + GLuint(i));
            glBindTexture(GL_TEXTURE_2D, output->texture());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            textureSize = output->size();
        } else {
            texture->bind(i);
        }
        program->setUniformValue(uniform(name.toUtf8()), GLint(i));
        program->setUniformValue(uniform(("_isflab_size_" + name).toUtf8()), float(textureSize.width()), float(textureSize.height()));
    }
    resetHistory = false;
    for (size_t i = 0; i < renderPasses.size(); ++i) {
        auto &pass = renderPasses[i];
        glBindFramebuffer(GL_FRAMEBUFFER, pass.write ? pass.write->handle() : (fixedOutput ? fixedOutput->handle() : screen));
        const auto passSize = passSizes[i];
        glViewport(0, 0, passSize.width(), passSize.height());
        program->setUniformValue(uniform("PASSINDEX"), GLint(i));
        program->setUniformValue(uniform("RENDERSIZE"), float(passSize.width()), float(passSize.height()));
        GLuint unit = GLuint(textures.size());
        for (auto &input : renderPasses) {
            if (!input.read) continue;
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, input.read->texture());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            program->setUniformValue(uniform(input.target.toUtf8()), GLint(unit++));
            program->setUniformValue(uniform(("_isflab_size_" + input.target).toUtf8()), float(input.read->width()), float(input.read->height()));
        }
        if (pass.write) {
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
        if (pass.write) std::swap(pass.read, pass.write);
    }
    if (!playing)
        for (auto &pass : renderPasses)
            if (pass.persistent && pass.write) std::swap(pass.read, pass.write);
    glActiveTexture(GL_TEXTURE0);
    program->disableAttributeArray(position);
    if (vao) QOpenGLContext::currentContext()->extraFunctions()->glBindVertexArray(0);
    program->release();
    if (fixedOutput) {
        const QSize fitted = size.scaled(viewportSize, Qt::KeepAspectRatio);
        const QRect destination(QPoint((viewportSize.width() - fitted.width()) / 2,
                                      (viewportSize.height() - fitted.height()) / 2), fitted);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fixedOutput->handle());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, screen);
        QOpenGLContext::currentContext()->extraFunctions()->glBlitFramebuffer(
            0, 0, size.width(), size.height(),
            destination.x(), destination.y(), destination.x() + destination.width(),
            destination.y() + destination.height(), GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, screen);
        glViewport(0, 0, viewportSize.width(), viewportSize.height());
    }
    // An event input is true for exactly one frame.
    for (auto event = eventCounts.keyBegin(); event != eventCounts.keyEnd(); ++event) values[*event] = false;
    if (playing) ++frame;
    emit timeChanged(time);
}
