#include "ShaderEngine.h"
#include "ISFScanner.h"
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

namespace {
const QString materialResourceRoot = ":/madmapper/";

QByteArray expandMaterialIncludes(const QByteArray &source, const QString &path, int firstLine,
                                  int sourceId, QStringList &files, QString &error, int depth = 0) {
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
} // namespace

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
    if (fixedOutputError && program) emit status("Preview compiled");
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
    resetGenerators = true;
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
class DimensionExpression {
public:
    DimensionExpression(QString expression, const QHash<QString, double> &variables)
        : expression(std::move(expression)), variables(variables) {}

    double evaluate() {
        if (expression.size() > 512) return invalid;
        const double result = sum();
        skipSpace();
        return position == expression.size() ? result : invalid;
    }

private:
    static constexpr double invalid = std::numeric_limits<double>::quiet_NaN();

    void skipSpace() {
        while (position < expression.size() && expression.at(position).isSpace()) ++position;
    }

    bool consume(QChar character) {
        skipSpace();
        if (position == expression.size() || expression.at(position) != character) return false;
        ++position;
        return true;
    }

    double sum() {
        double result = product();
        for (;;) {
            if (consume('+')) result += product();
            else if (consume('-')) result -= product();
            else return result;
        }
    }

    double product() {
        double result = operand();
        for (;;) {
            if (consume('*')) result *= operand();
            else if (consume('/')) result /= operand();
            else return result;
        }
    }

    double operand() {
        if (consume('+')) return operand();
        if (consume('-')) return -operand();
        if (consume('(')) {
            const double result = sum();
            return consume(')') ? result : invalid;
        }

        const bool variable = consume('$');
        static const QRegularExpression token(
            R"rx([A-Za-z_]\w*|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)rx");
        const auto match = token.match(expression, position, QRegularExpression::NormalMatch,
                                       QRegularExpression::AnchorAtOffsetMatchOption);
        if (!match.hasMatch()) return invalid;
        position = match.capturedEnd();
        const auto name = match.captured();
        if (variable) return variables.value(name, invalid);

        bool numeric = false;
        const double value = name.toDouble(&numeric);
        if (numeric) return value;
        if (!consume('(')) return invalid;
        const double first = sum();
        if (name == "min" || name == "max") {
            if (!consume(',')) return invalid;
            const double second = sum();
            if (!consume(')') || std::isnan(first) || std::isnan(second)) return invalid;
            return name == "min" ? std::min(first, second) : std::max(first, second);
        }
        if (!consume(')')) return invalid;
        if (name == "floor") return std::floor(first);
        if (name == "ceil") return std::ceil(first);
        if (name == "round") return std::round(first);
        if (name == "abs") return std::abs(first);
        return invalid;
    }

    QString expression;
    const QHash<QString, double> &variables;
    qsizetype position = 0;
};

QImage testCard(double seconds) {
    QImage image(640, 360, QImage::Format_RGBA8888);
    const QColor ink("#111319"), panel("#191d27"), grid("#343a48"), white("#f4f1e9");
    const QColor accents[] = {QColor("#fa684f"), QColor("#f8cc63"), QColor("#a8dc9c"),
                              QColor("#69d9d7"), QColor("#839bff"), QColor("#db8be6")};
    image.fill(ink);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);

    p.setPen(white);
    p.setFont(QFont("Sans Serif", 13, QFont::DemiBold));
    p.drawText(QRect(20, 12, 600, 28), Qt::AlignLeft | Qt::AlignVCenter, "ISFLAB  /  PIXEL STEREO");
    p.drawText(QRect(230, 12, 275, 28), Qt::AlignCenter, "CHROMA  /  LUMA  /  MOTION");
    p.drawText(QRect(510, 12, 110, 28), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(seconds, 'f', 2) + " s");

    p.fillRect(QRect(20, 52, 356, 214), panel);
    p.setPen(grid);
    for (int x = 38; x < 376; x += 20) p.drawLine(x, 52, x, 265);
    for (int y = 59; y < 266; y += 20) p.drawLine(20, y, 375, y);
    const QPointF center(198, 159);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor("#7c869a"), 1));
    for (int radius : {28, 54, 80}) p.drawEllipse(center, radius, radius);
    p.drawLine(QPointF(102, 159), QPointF(294, 159));
    p.drawLine(QPointF(198, 63), QPointF(198, 255));
    for (int i = 0; i < 3; ++i) {
        const double phase = seconds * (1.4 + i * 0.55) + i * 2.1;
        const double radius = 80 - i * 26;
        const auto dot = center + QPointF(std::cos(phase) * radius, std::sin(phase) * radius);
        p.setBrush(i == 0 ? white : accents[i + 2]);
        p.setPen(QPen(ink, 2));
        p.drawEllipse(dot, 12 - i * 2, 12 - i * 2);
    }

    const QColor rgb[] = {Qt::red, Qt::green, Qt::blue};
    for (int i = 0; i < 3; ++i) {
        const QRect tile(388 + i * 79, 52, 74, 94);
        p.fillRect(tile, rgb[i]);
        p.setPen(ink);
        p.drawText(tile.adjusted(8, 0, 0, -6), Qt::AlignLeft | Qt::AlignBottom,
                   QString(QChar("RGB"[i])));
    }

    const QRect gradientArea(388, 156, 232, 110);
    QLinearGradient cmyk(gradientArea.topLeft(), gradientArea.bottomRight());
    cmyk.setColorAt(0.0, QColor::fromCmyk(255, 0, 0, 0));
    cmyk.setColorAt(1.0 / 3.0, QColor::fromCmyk(0, 255, 0, 0));
    cmyk.setColorAt(2.0 / 3.0, QColor::fromCmyk(0, 0, 255, 0));
    cmyk.setColorAt(1.0, QColor::fromCmyk(0, 0, 0, 255));
    p.fillRect(gradientArea, cmyk);

    const QRect track(0, 304, image.width(), image.height() - 304);
    p.fillRect(track, panel);
    p.setClipRect(track);
    const int travel = int(std::fmod(seconds * 110, 32.0));
    for (int x = track.left() - 96 + travel; x < track.right(); x += 32) {
        QPolygon stripe;
        stripe << QPoint(x + 76, track.top()) << QPoint(x + 86, track.top())
               << QPoint(x + 10, track.bottom() + 1) << QPoint(x, track.bottom() + 1);
        p.setPen(Qt::NoPen);
        p.setBrush(white);
        p.drawPolygon(stripe);
    }
    p.setClipping(false);

    for (int i = 0; i < 16; ++i) {
        const int level = i * 255 / 15;
        const int x = 20 + i * 600 / 16;
        p.fillRect(x, 281, 20 + (i + 1) * 600 / 16 - x, 19, QColor(level, level, level));
    }
    p.end();
    return image;
}

QImage transitionCard(bool after) {
    QImage image(640, 360, QImage::Format_RGBA8888);
    QPainter painter(&image);
    QLinearGradient gradient(0, 0, image.width(), image.height());
    gradient.setColorAt(0, QColor(after ? "#813e73" : "#294297"));
    gradient.setColorAt(1, QColor(after ? "#e68556" : "#319fa6"));
    painter.fillRect(image.rect(), gradient);
    painter.setPen(QColor(255, 255, 255, 150));
    painter.drawRect(image.rect().adjusted(18, 18, -19, -19));
    painter.setPen(Qt::white);
    painter.setFont(QFont("Sans Serif", 58, QFont::Bold));
    painter.drawText(image.rect(), Qt::AlignCenter, after ? "AFTER" : "BEFORE");
    return image;
}

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
    if (pixels.isNull()) pixels = testCard(time);
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
    generatorOrder.clear();
    resetGenerators = true;
    auto passes = header.value("PASSES").toArray();
    if (passes.isEmpty()) passes.append(QJsonObject{});
    if (passes.size() > 16) {
        emit status("Preview supports up to 16 passes.");
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
            if ((type != "time_base" && type != "animator" && type != "incrementer" && type != "multiplier" && type != "pass_thru") || !identifier.match(name).hasMatch() || types.contains(name)) {
                emit status(QString("Unsupported Material generator: %1 (%2)").arg(name, type));
                return;
            }
            add(name, "float");
            generators.insert(name, {generator.value("PARAMS").toObject(), type});
        }
    }
    QSet<QString> visiting, ordered;
    const auto orderGenerator = [&](const auto &visit, const QString &name) -> bool {
        if (ordered.contains(name)) return true;
        if (visiting.contains(name)) return false;
        visiting.insert(name);
        const auto &parameters = generators.constFind(name)->parameters;
        for (const auto &parameter : parameters) {
            const auto dependency = parameter.toString();
            if (generators.contains(dependency) && !visit(visit, dependency)) return false;
        }
        visiting.remove(name);
        ordered.insert(name);
        generatorOrder.append(name);
        return true;
    };
    for (const auto &name : generators.keys()) {
        if (!orderGenerator(orderGenerator, name)) {
            emit status("Circular Material generator dependency: " + name);
            return;
        }
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
                emit status("Missing imported image: " + relative);
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
            emit status("Preview requires unique pass targets and a final output pass.");
            return;
        }
        renderPasses.push_back({target, flag("PERSISTENT"), flag("FLOAT"),
                                pass.value("WIDTH"), pass.value("HEIGHT"), {}, {}});
        if (!target.isEmpty()) add(target, "image");
    }
    const auto start = ISFScanner::headerStart(source);
    const auto end = start < 0 ? -1 : source.indexOf("*/", start + 2);
    if (end < 0) {
        emit status("Invalid ISF JSON header.");
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
            emit status("Cannot read vertex shader: " + vertexPath);
            return;
        }
        auto vertexSource = vertexFile.readAll();
        if (vertexFile.error() != QFile::NoError) {
            emit status("Cannot read vertex shader: " + vertexPath);
            return;
        }
        if (vertexSource.startsWith("\xef\xbb\xbf")) vertexSource.remove(0, 3);
        auto vertexBody = QString::fromUtf8(vertexSource);
        if (material) {
            QString error;
            vertexBody = QString::fromUtf8(expandMaterialIncludes(vertexSource, vertexPath, 1, 0,
                                                                  vertexIncludes, error));
            if (!error.isEmpty()) {
                emit glslDiagnostic("Material vertex include", vertexPath, error, 0);
                emit status(error);
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
            emit glslDiagnostic("Material include", path, error, 0);
            emit status(error);
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
                else if (const auto &files = stage == "Vertex shader" ? vertexIncludes : includeFiles;
                         material && sourceId < files.size()) {
                    diagnosticFile = files.at(sourceId);
                    line = match.captured(match.capturedLength(2) ? 2 : 3).toInt();
                }
            }
        }
        emit glslDiagnostic(stage, diagnosticFile, log, line);
        emit status(stage + " failed" + (log.isEmpty() ? "." : ": " + log));
    };
    if (!candidate->addShaderFromSourceCode(QOpenGLShader::Vertex, vert)) {
        reportCompilerError("Vertex shader", vertexPath, !vertexPath.isEmpty());
        return;
    }
    if (!candidate->addShaderFromSourceCode(QOpenGLShader::Fragment, frag)) {
        reportCompilerError("Fragment shader", path, true);
        return;
    }
    if (!candidate->link()) {
        reportCompilerError("Shader linking", {}, false);
        return;
    }
    program = std::move(candidate);
    emit status("Preview compiled");
    if (!missingImages.isEmpty())
        emit glslDiagnostic("Material warning", path,
            "Preview uses substitute textures. Restore these files for the original appearance:\n" +
            missingImages.join('\n'), 0);
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
            : DimensionExpression(expression.toString(), variables).evaluate();
        return std::isfinite(value) && value <= textureLimit && value >= 0
            ? qMax(1, int(value)) : 0;
    };
    const auto rejectBuffers = [&](const QString &message) {
        if (bufferError == message) return;
        bufferError = message;
        emit status(message);
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
        emit status("Preview compiled");
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
            emit status("Fixed resolution unavailable. Try a smaller size or switch to Auto.");
            return;
        }
        emit status("Preview compiled");
    }

    if (imagesDirty) {
        imagesDirty = false;
        textures.clear();
        GLint limit = 0;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &limit);
        if (std::count(types.cbegin(), types.cend(), QString("sampler2D")) > limit) {
            program.reset();
            emit status("Too many textures for this GPU.");
            return;
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
                image = transitionCard(it.key() == "endImage");
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
    }
    if (!audioTextures.isEmpty()) {
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
    const float delta = playing && frame > 0 ? qMin(clock.restart() / 1000.0, 0.25) : 0.0;
    if (delta == 0) clock.restart();
    time += delta;
    updateVideoInput(screen);
    const auto now = QDateTime::currentDateTime();
    program->bind();
    program->setUniformValue("TIME", float(time));
    program->setUniformValue("CLOCK_TIME", float(materialClock.elapsed() / 1000.0));
    program->setUniformValue("TIMEDELTA", delta);
    program->setUniformValue("FRAMEINDEX", frame);
    program->setUniformValue("DATE", float(now.date().year()), float(now.date().month()),
                             float(now.date().day()), now.time().msecsSinceStartOfDay() / 1000.f);
    for (auto it = values.begin(); it != values.end(); ++it) {
        const auto type = types.value(it.key());
        const auto name = it.key().toUtf8();
        if (type == "float") program->setUniformValue(name, value(it.key()).toFloat());
        else if (type == "int" || type == "bool") program->setUniformValue(name, it.value().toInt());
        else if (type == "vec2") program->setUniformValue(name, it.value().value<QVector2D>());
        else if (type == "vec3") program->setUniformValue(name, it.value().value<QVector3D>());
        else if (type == "vec4") program->setUniformValue(name, it.value().value<QVector4D>());
    }
    QHash<QString, double> generatorOutputs;
    for (const auto &name : generatorOrder) {
        auto it = generators.find(name);
        const auto publish = [&](double output) {
            generatorOutputs.insert(name, output);
            program->setUniformValue(name.toUtf8(), float(output));
        };
        const auto &parameters = it->parameters;
        const auto parameter = [&](const char *key, const QVariant &fallback) {
            const auto entry = parameters.value(key);
            if (entry.isString()) {
                const auto reference = entry.toString();
                if (generatorOutputs.contains(reference)) return QVariant(generatorOutputs.value(reference));
                return values.value(reference, fallback);
            }
            return entry.isUndefined() ? fallback : entry.toVariant();
        };
        if (it->type == "incrementer") {
            const auto increments = eventCounts.value(parameters.value("increment").toString());
            const auto decrements = eventCounts.value(parameters.value("decrement").toString());
            publish(double(increments - decrements));
            continue;
        }
        if (it->type == "multiplier") {
            publish(parameter("value1", 1.).toDouble() * parameter("value2", 1.).toDouble() *
                    parameter("value3", 1.).toDouble() * parameter("value4", 1.).toDouble());
            continue;
        }
        if (it->type == "pass_thru") {
            const auto channel = parameters.value("input_value").toString();
            if (channel == "/custom/BPM/bpm") publish(120.);
            else if (channel == "/custom/BPM/bpmPos") publish(time * 2.);
            else publish(parameter("input_value", 0.).toDouble());
            continue;
        }
        double speed = parameter("speed", 1.0).toDouble();
        const double curve = parameter("speed_curve", 1.0).toDouble();
        if (speed != 0 && curve > 0) speed = std::copysign(std::pow(std::abs(speed), curve), speed);
        if (parameter("reverse", false).toBool()) speed = -speed;
        const bool bpmSync = parameter("bpm_sync", false).toBool();
        constexpr double beatsPerSecond = 2.0; // Preview clock at 120 BPM.
        if (bpmSync && speed != 0)
            speed = std::copysign(std::exp2(std::round(std::log2(std::abs(speed)))), speed) * beatsPerSecond;
        it->elapsed = resetGenerators ? time * speed : it->elapsed + delta * speed;
        const double strob = parameter("strob", 0.0).toDouble() / (bpmSync ? beatsPerSecond : 1.0);
        if (strob <= 0) it->sampled = it->elapsed;
        else if (resetGenerators || std::floor(time / strob) != std::floor((time - delta) / strob))
            it->sampled = it->elapsed - std::fmod(time, strob) * speed;
        double output = it->sampled;
        if (it->type == "animator") {
            const auto shapeRef = parameters.value("shape").toString("Smooth");
            const auto choices = materialChoices.value(shapeRef);
            const int index = values.value(shapeRef, -1).toInt();
            const auto shape = index >= 0 && index < choices.size() ? choices.at(index).toString() : shapeRef;
            const double cycle = std::floor(output);
            const double phase = output - cycle;
            if (shape == "In") output = phase;
            else if (shape == "Out") output = 1.0 - phase;
            else if (shape == "Linear") output = 1.0 - std::abs(2.0 * phase - 1.0);
            else if (shape == "Cut") output = phase >= 0.5 ? 1.0 : 0.0;
            else if (shape == "Random" || shape == "Noise") {
                const auto random = [](double cell) {
                    const double hash = std::sin(cell * 127.1 + 311.7) * 43758.5453;
                    return hash - std::floor(hash);
                };
                const double blend = phase * phase * (3.0 - 2.0 * phase);
                output = random(cycle);
                if (shape == "Noise") output += blend * (random(cycle + 1.0) - output);
            } else {
                constexpr double tau = 6.283185307179586;
                output = 0.5 - 0.5 * std::cos(tau * phase);
            }
        }
        publish(output);
    }
    resetGenerators = false;
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
                emit status("Could not allocate preview pass buffers.");
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
        program->setUniformValue(name.toUtf8(), GLint(i));
        program->setUniformValue(("_isflab_size_" + name).toUtf8(), float(textureSize.width()), float(textureSize.height()));
    }
    resetHistory = false;
    for (size_t i = 0; i < renderPasses.size(); ++i) {
        auto &pass = renderPasses[i];
        glBindFramebuffer(GL_FRAMEBUFFER, pass.write ? pass.write->handle() : (fixedOutput ? fixedOutput->handle() : screen));
        const auto passSize = passSizes[i];
        glViewport(0, 0, passSize.width(), passSize.height());
        program->setUniformValue("PASSINDEX", GLint(i));
        program->setUniformValue("RENDERSIZE", float(passSize.width()), float(passSize.height()));
        GLuint unit = GLuint(textures.size());
        for (auto &input : renderPasses) {
            if (!input.read) continue;
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, input.read->texture());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            program->setUniformValue(input.target.toUtf8(), GLint(unit++));
            program->setUniformValue(("_isflab_size_" + input.target).toUtf8(), float(input.read->width()), float(input.read->height()));
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
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (it.value().userType() == QMetaType::Bool && it.value().toBool()) {
            for (const auto &entry : header.value("INPUTS").toArray()) {
                const auto input = entry.toObject();
                if (input.value("NAME").toString() == it.key() && input.value("TYPE").toString() == "event")
                    it.value() = false;
            }
        }
    }
    if (playing) ++frame;
    emit timeChanged(time);
}
