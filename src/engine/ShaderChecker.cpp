#include "ShaderChecker.h"
#include "ShaderEngine.h"
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>

namespace {
const QSize checkSize(64, 36);
}

ShaderChecker::ShaderChecker() = default;

ShaderChecker::~ShaderChecker() {
    if (!context || !engine) return;
    if (context->makeCurrent(surface.get())) {
        engine->release();
        target.reset();
        context->doneCurrent();
    }
}

QString ShaderChecker::initialize() {
    context = std::make_unique<QOpenGLContext>();
    context->setFormat(QSurfaceFormat::defaultFormat());
    if (!context->create()) return "Cannot create an OpenGL context to check shaders.";
    surface = std::make_unique<QOffscreenSurface>();
    surface->setFormat(context->format());
    surface->create();
    if (!surface->isValid() || !context->makeCurrent(surface.get()))
        return "Cannot use an offscreen OpenGL surface to check shaders.";
    engine = std::make_unique<ShaderEngine>();
    QObject::connect(engine.get(), &ShaderEngine::status, [this](const QString &message, bool ok) {
        lastStatus = message;
        compiled = ok;
    });
    engine->initialize();
    engine->setPlaying(false);
    target = std::make_unique<QOpenGLFramebufferObject>(checkSize);
    const bool valid = target->isValid();
    context->doneCurrent();
    return valid ? QString() : "Cannot allocate an offscreen buffer to check shaders.";
}

QString ShaderChecker::check(const QByteArray &source, const QJsonObject &header, const QString &path) {
    // Other GL widgets may have made their own context current since the previous check.
    if (!context->makeCurrent(surface.get())) return "The OpenGL context used for checking was lost.";
    lastStatus.clear();
    compiled = false;
    engine->load(source, header, path);
    engine->render(target->handle(), checkSize);
    context->doneCurrent();
    if (compiled) return {};
    return lastStatus.isEmpty() ? QString("The preview did not compile.") : lastStatus;
}
