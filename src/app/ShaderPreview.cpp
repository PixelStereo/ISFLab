#include "ShaderPreview.h"
#include <QMouseEvent>
#include <QAudioDevice>
#include <QAudioSource>
#include <QCoreApplication>
#include <QIODevice>
#include <QMediaDevices>
#include <QPermissions>
#include <QSettings>
#include <array>
#include <cmath>
#include <complex>
#include <QOpenGLContext>
#include <QTimer>
#include <QSurfaceFormat>

namespace {
constexpr int audioFrames = 2048;

QList<float> audioSpectrum(const QList<float> &wave) {
    constexpr double tau = 6.283185307179586;
    std::array<std::complex<double>, audioFrames> fft;
    for (int i = 0, reversed = 0; i < audioFrames; ++i) {
        fft[reversed] = wave.at(i) * (0.5 - 0.5 * std::cos(tau * i / audioFrames));
        int bit = audioFrames / 2;
        while (bit && (reversed & bit)) { reversed ^= bit; bit >>= 1; }
        reversed ^= bit;
    }
    for (int length = 2; length <= audioFrames; length *= 2) {
        const auto step = std::polar(1.0, -tau / length);
        for (int block = 0; block < audioFrames; block += length) {
            std::complex<double> rotation(1, 0);
            for (int j = 0; j < length / 2; ++j) {
                auto &left = fft[block + j];
                auto &right = fft[block + j + length / 2];
                const auto rotated = right * rotation;
                right = left - rotated;
                left += rotated;
                rotation *= step;
            }
        }
    }
    QList<float> spectrum;
    spectrum.reserve(audioFrames / 2);
    for (int i = 0; i < audioFrames / 2; ++i)
        spectrum.append(float(std::abs(fft[i]) * (i == 0 ? 2.0 : 4.0) / audioFrames));
    return spectrum;
}
}

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
    audioDeviceId = QSettings().value("audio/inputDevice").toByteArray();
    auto *devices = new QMediaDevices(this);
    connect(devices, &QMediaDevices::audioInputsChanged, this, &ShaderPreview::syncAudio);
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

void ShaderPreview::setAudioDevice(const QByteArray &id) {
    audioDeviceId = id;
    syncAudio();
}

void ShaderPreview::setAudioStatus(const QString &message) {
    if (audioMessage == message) return;
    audioMessage = message;
    emit audioStatusChanged(message);
}

void ShaderPreview::syncAudio() {
    QAudioDevice device;
    const bool needed = engine.needsAudio() && audioDeviceId != "disabled";
    if (needed) {
        if (audioDeviceId.isEmpty()) device = QMediaDevices::defaultAudioInput();
        else for (const auto &candidate : QMediaDevices::audioInputs()) {
            if (candidate.id() == audioDeviceId) { device = candidate; break; }
        }
    }
    if (audioSource && !device.isNull() && runningAudioDevice == device.id() &&
        audioSource->state() != QAudio::StoppedState) return;
    if (audioSource) audioSource->disconnect(this);
    audioSource.reset();
    runningAudioDevice.clear();
    audioBuffer.clear();
    engine.setAudioFrame({});
    update();
    if (!needed || device.isNull()) {
        setAudioStatus(!needed ? "Audio input disabled" : "Audio input unavailable");
        return;
    }

#ifdef Q_OS_MACOS
    const QMicrophonePermission permission;
    const auto access = qApp->checkPermission(permission);
    if (access == Qt::PermissionStatus::Undetermined) {
        setAudioStatus("Waiting for microphone permission…");
        if (!microphoneRequestPending) {
            microphoneRequestPending = true;
            qApp->requestPermission(permission, this, [this](const QPermission &result) {
                microphoneRequestPending = false;
                if (result.status() == Qt::PermissionStatus::Granted) syncAudio();
                else setAudioStatus("Microphone access denied");
            });
        }
        return;
    }
    if (access == Qt::PermissionStatus::Denied) {
        setAudioStatus("Microphone access denied");
        return;
    }
#endif
    const auto format = device.preferredFormat();
    if (format.bytesPerFrame() <= 0) {
        setAudioStatus("Unsupported audio input format");
        return;
    }
    audioSource = std::make_unique<QAudioSource>(device, format);
    auto *stream = audioSource->start();
    if (!stream || audioSource->error() != QAudio::NoError) {
        audioSource.reset();
        setAudioStatus("Could not start audio input");
        return;
    }
    runningAudioDevice = device.id();
    setAudioStatus(device.description());
    connect(audioSource.get(), &QAudioSource::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::StoppedState && audioSource->error() != QAudio::NoError) {
            engine.setAudioFrame({});
            setAudioStatus("Audio input stopped");
            update();
        }
    });
    connect(stream, &QIODevice::readyRead, this, [this, stream, format] {
        const int stride = format.bytesPerFrame();
        audioBuffer += stream->readAll();
        const auto complete = audioBuffer.size() / stride;
        if (complete == 0) return;
        const int count = int(qMin<qsizetype>(audioFrames, complete));
        if (complete > audioFrames) audioBuffer.remove(0, (complete - audioFrames) * stride);
        auto frame = std::make_shared<ShaderEngine::AudioFrame>();
        for (int channel = 0; channel < format.channelCount(); ++channel) {
            QList<float> wave(audioFrames, 0.f);
            for (int i = 0; i < count; ++i) {
                const auto *sample = audioBuffer.constData() + i * stride + channel * format.bytesPerSample();
                wave[audioFrames - count + i] = format.normalizedSampleValue(sample);
            }
            frame->spectrum.append(audioSpectrum(wave));
            frame->wave.append(std::move(wave));
        }
        engine.setAudioFrame(std::move(frame));
        update();
    });
}

void ShaderPreview::load(const QByteArray &source, const QJsonObject &header, const QString &path) {
    if (isValid()) makeCurrent();
    engine.load(source, header, path);
    if (isValid()) doneCurrent();
    syncAudio();
    update();
}

void ShaderPreview::setLiveInput(std::unique_ptr<LiveInput> input) {
    if (isValid()) makeCurrent();
    engine.setLiveInput(std::move(input));
    if (isValid()) doneCurrent();
    syncAudio();
    update();
}

void ShaderPreview::useTestCard() { setLiveInput(nullptr); }

void ShaderPreview::setShaderInput(const QString &name, const QByteArray &source,
                                  const QJsonObject &header, const QString &path) {
    if (isValid()) makeCurrent();
    engine.setShaderInput(name, source, header, path);
    if (isValid()) doneCurrent();
    syncAudio();
    update();
}

void ShaderPreview::setImage(const QString &name, const QImage &image) {
    if (isValid()) makeCurrent();
    engine.setImage(name, image);
    if (isValid()) doneCurrent();
    syncAudio();
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
