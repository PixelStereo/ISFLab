#pragma once
#include <QJsonObject>
#include <QString>
#include <memory>

class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;
class ShaderEngine;

// Compiles and renders shaders one frame each in an offscreen OpenGL context, with the same engine and
// GLSL version as the preview, so a batch check reports exactly the errors the preview would show.
// Use from the GUI thread.
class ShaderChecker {
public:
    ShaderChecker();
    ~ShaderChecker();
    // Creates the context. Returns why it is unavailable, or an empty string.
    QString initialize();
    // Returns why the shader does not preview, or an empty string when it compiles and renders.
    QString check(const QByteArray &source, const QJsonObject &header, const QString &path);

private:
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QOpenGLContext> context;
    std::unique_ptr<ShaderEngine> engine;
    std::unique_ptr<QOpenGLFramebufferObject> target;
    QString lastStatus;
    bool compiled = false;
};
