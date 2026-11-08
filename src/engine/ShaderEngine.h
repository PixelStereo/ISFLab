#pragma once
#include "LiveInput.h"
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QOpenGLFunctions>
#include <QObject>
#include <QVariant>
#include <memory>
#include <map>
#include <vector>
#include <utility>

class QOpenGLShaderProgram;
class QOpenGLTexture;
class QOpenGLFramebufferObject;

class ShaderEngine : public QObject, protected QOpenGLFunctions {
    Q_OBJECT
public:
    ShaderEngine();
    ~ShaderEngine() override;

    // The caller owns the GL context and keeps it current for initialization,
    // rendering, cleanup, and changes that may release a live input.
    void initialize();
    void release();
    void render(GLuint framebuffer, const QSize &viewportSize);
    bool isPlaying() const { return playing; }
    bool canTrackPointer() const { return program && !fixedOutputError; }
    void load(const QByteArray &source, const QJsonObject &header, const QString &path);
    QSize renderSize(const QSize &viewportSize) const;
    void setFixedRenderSize(const QSize &size);
    void setPlaying(bool playing);
    void setFilterEnabled(bool enabled);
    void seek(double time);
    void setValue(const QString &name, const QVariant &value);
    QVariant value(const QString &name) const;
    void setTransitionAnimation(bool enabled);
    void setTransitionSpeed(double cyclesPerSecond);
    void useTestCard();
    void setLiveInput(std::unique_ptr<LiveInput> input);
    void setShaderInput(const QString &name, const QByteArray &source, const QJsonObject &header, const QString &path);
    void setImage(const QString &name, const QImage &image);
signals:
    void status(const QString &message);
    void inputStatus(const QString &message);
    void testCardActiveChanged(bool active);
    void shaderInputError(const QString &name, const QString &message);
    void timeChanged(double time);
private:
    struct ShaderInput;
    void renderShaderInputs(GLuint framebuffer, const QSize &viewportSize);
    void compile();
    void updateVideoInput(GLuint framebuffer);
    QByteArray source;
    QJsonObject header;
    QString path;
    QHash<QString, QVariant> values;
    QHash<QString, QImage> images;
    std::map<QString, std::unique_ptr<ShaderInput>> shaderInputs;
    QHash<QString, QString> types;
    std::unique_ptr<QOpenGLShaderProgram> program;
    std::vector<std::pair<QString, std::unique_ptr<QOpenGLTexture>>> textures;
    struct RenderPass {
        QString target;
        bool persistent = false;
        bool floatingPoint = false;
        QJsonValue width, height;
        std::unique_ptr<QOpenGLFramebufferObject> read, write;
    };
    std::vector<RenderPass> renderPasses;
    QSize fixedRenderSize;
    std::unique_ptr<QOpenGLFramebufferObject> fixedOutput;
    bool fixedOutputError = false;
    QString bufferError;
    bool resetHistory = true;
    QElapsedTimer clock, videoClock;
    std::unique_ptr<LiveInput> liveInput;
    QImage lastVideo, videoFrame;
    bool fxInput = false;
    bool filterEnabled = true;
    bool transitionAnimated = false;
    double transitionSpeed = 0.25;
    double transitionOffset = 0.0;
    QString lastInputStatus;
    double time = 0;
    int frame = 0;
    bool playing = true, dirty = true, imagesDirty = true;
    GLuint vbo = 0;
};
