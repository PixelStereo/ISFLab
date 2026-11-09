#include "ShaderPreview.h"
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QTimer>
#include <QSurfaceFormat>

void ShaderPreview::configureDefaultFormat() {
#ifdef Q_OS_MACOS
    auto format = QSurfaceFormat::defaultFormat();
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
#endif
}

ShaderPreview::ShaderPreview(QWidget *parent) : QOpenGLWidget(parent) {
    connect(&engine, &ShaderEngine::status, this, &ShaderPreview::status);
    connect(&engine, &ShaderEngine::glslDiagnostic, this, &ShaderPreview::glslDiagnostic);
    connect(&engine, &ShaderEngine::inputStatus, this, &ShaderPreview::inputStatus);
    connect(&engine, &ShaderEngine::testCardActiveChanged, this, &ShaderPreview::testCardActiveChanged);
    connect(&engine, &ShaderEngine::shaderInputError, this, &ShaderPreview::shaderInputError);
    connect(&engine, &ShaderEngine::timeChanged, this, &ShaderPreview::timeChanged);
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { if (engine.isPlaying() && isVisible()) update(); });
    timer->start(16);
}

ShaderPreview::~ShaderPreview() { releaseEngine(); }

void ShaderPreview::releaseEngine() {
    if (!contextCleanup) return;
    disconnect(contextCleanup);
    contextCleanup = {};
    makeCurrent();
    engine.release();
    doneCurrent();
}

void ShaderPreview::initializeGL() {
    engine.initialize();
    contextCleanup = connect(context(), &QOpenGLContext::aboutToBeDestroyed,
                             this, &ShaderPreview::releaseEngine);
}

void ShaderPreview::paintGL() {
    const auto size = renderSize();
    if (reportedRenderSize != size) {
        reportedRenderSize = size;
        emit renderSizeChanged(size);
    }
    engine.render(defaultFramebufferObject(), viewportSize());
}

QSize ShaderPreview::viewportSize() const {
    return {qRound(width() * devicePixelRatioF()), qRound(height() * devicePixelRatioF())};
}

QSize ShaderPreview::renderSize() const { return engine.renderSize(viewportSize()); }

void ShaderPreview::load(const QByteArray &source, const QJsonObject &header, const QString &path) {
    if (isValid()) makeCurrent();
    engine.load(source, header, path);
    if (isValid()) doneCurrent();
    update();
}

void ShaderPreview::setLiveInput(std::unique_ptr<LiveInput> input) {
    if (isValid()) makeCurrent();
    engine.setLiveInput(std::move(input));
    if (isValid()) doneCurrent();
    update();
}

void ShaderPreview::useTestCard() { setLiveInput(nullptr); }

void ShaderPreview::setShaderInput(const QString &name, const QByteArray &source,
                                  const QJsonObject &header, const QString &path) {
    if (isValid()) makeCurrent();
    engine.setShaderInput(name, source, header, path);
    if (isValid()) doneCurrent();
    update();
}

void ShaderPreview::setImage(const QString &name, const QImage &image) {
    if (isValid()) makeCurrent();
    engine.setImage(name, image);
    if (isValid()) doneCurrent();
    update();
}

void ShaderPreview::setFixedRenderSize(const QSize & size) {
    engine.setFixedRenderSize(size);
    update();
}

void ShaderPreview::setPlaying(bool playing) {
    engine.setPlaying(playing);
    update();
}

void ShaderPreview::setFilterEnabled(bool enabled) {
    engine.setFilterEnabled(enabled);
    update();
}

void ShaderPreview::seek(double time) {
    engine.seek(time);
    update();
}

void ShaderPreview::setTransitionAnimation(bool enabled) {
    engine.setTransitionAnimation(enabled);
    update();
}

void ShaderPreview::setValue(const QString &name, const QVariant &value) {
    engine.setValue(name, value);
    update();
}

void ShaderPreview::reportPointer(const QPointF &position) {
    const QSize size = renderSize();
    if (!hasMouseTracking() || !engine.canTrackPointer() || size.isEmpty()) return;
    const qreal ratio = devicePixelRatioF();
    const QSize viewport = viewportSize();
    const QSize fitted = size.scaled(viewport, Qt::KeepAspectRatio);
    if (fitted.isEmpty()) return;
    const QRectF image((viewport.width() - fitted.width()) / 2,
                       (viewport.height() - fitted.height() + 1) / 2,
                       fitted.width(), fitted.height());
    const auto point = position * ratio;
    if (!image.contains(point)) return;
    const QPointF normalized((point.x() - image.left()) / image.width(),
                             1.0 - (point.y() - image.top()) / image.height());
    emit renderPointerMoved(normalized, QPointF(normalized.x() * size.width(), normalized.y() * size.height()));
}

void ShaderPreview::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) reportPointer(event->position());
    QOpenGLWidget::mousePressEvent(event);
}

void ShaderPreview::mouseMoveEvent(QMouseEvent *event) {
    reportPointer(event->position());
    QOpenGLWidget::mouseMoveEvent(event);
}
