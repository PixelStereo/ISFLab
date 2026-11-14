#include "ShaderPreview.h"
#include "SettingsKeys.h"
#include <QMouseEvent>
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioDevice>
#include <QElapsedTimer>
#include <QFileInfo>
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

std::shared_ptr<ShaderEngine::AudioFrame> makeAudioFrame(QList<QList<float>> waves) {
    auto frame = std::make_shared<ShaderEngine::AudioFrame>();
    for (auto &wave : waves) {
        frame->spectrum.append(audioSpectrum(wave));
        frame->wave.append(std::move(wave));
    }
    return frame;
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
    audioDeviceId = QSettings().value(SettingsKeys::AudioInputDevice).toByteArray();
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

void ShaderPreview::setAudioMode(AudioMode mode, const QString &file) {
    audioMode = mode;
    audioFile = file;
    fileLoadedPath.clear();
    syncAudio();
}

void ShaderPreview::stopFileAudio() {
    if (fileTimer) fileTimer->stop();
    if (decoder) { decoder->disconnect(this); decoder->stop(); decoder.reset(); }
    fileWaves.clear();
    fileLoadedPath.clear();
}

void ShaderPreview::startFileAudio() {
    if (audioFile.isEmpty() || !QFileInfo::exists(audioFile)) {
        stopFileAudio();
        engine.setAudioFrame({});
        setAudioStatus("No audio file selected");
        update();
        return;
    }
    if (fileLoadedPath == audioFile && (decoder || !fileWaves.isEmpty())) return;
    stopFileAudio();
    engine.setAudioFrame({});
    fileLoadedPath = audioFile;
    setAudioStatus("Decoding " + QFileInfo(audioFile).fileName() + "…");
    decoder = std::make_unique<QAudioDecoder>();
    QAudioFormat format;
    format.setSampleFormat(QAudioFormat::Float);
    decoder->setAudioFormat(format);
    decoder->setSource(QUrl::fromLocalFile(audioFile));
    connect(decoder.get(), &QAudioDecoder::bufferReady, this, [this] {
        const auto buffer = decoder->read();
        const auto &format = buffer.format();
        if (!buffer.isValid() || format.sampleFormat() != QAudioFormat::Float) return;
        fileRate = format.sampleRate();
        if (fileWaves.isEmpty()) fileWaves.resize(format.channelCount());
        const auto *data = buffer.constData<float>();
        const int channels = format.channelCount();
        if (channels != fileWaves.size()) return;
        for (int i = 0; i < buffer.frameCount(); ++i)
            for (int c = 0; c < channels; ++c) fileWaves[c].append(data[i * channels + c]);
    });
    connect(decoder.get(), &QAudioDecoder::finished, this, [this] {
        if (fileWaves.isEmpty() || fileWaves.first().isEmpty() || fileRate <= 0) {
            setAudioStatus("Could not decode audio file");
            return;
        }
        setAudioStatus(QFileInfo(audioFile).fileName() + " (loop)");
        fileClock.start();
        if (!fileTimer) {
            fileTimer = new QTimer(this);
            fileTimer->setInterval(16);
            connect(fileTimer, &QTimer::timeout, this, [this] {
                if (fileWaves.isEmpty()) return;
                const qint64 total = fileWaves.first().size();
                const qint64 end = (fileClock.nsecsElapsed() * fileRate / 1000000000LL) % total;
                QList<QList<float>> waves;
                for (const auto &channel : std::as_const(fileWaves)) {
                    QList<float> wave(audioFrames);
                    for (int i = 0; i < audioFrames; ++i)
                        wave[i] = channel.at(((end - audioFrames + 1 + i) % total + total) % total);
                    waves.append(std::move(wave));
                }
                engine.setAudioFrame(makeAudioFrame(std::move(waves)));
                update();
            });
        }
        fileTimer->start();
    });
    connect(decoder.get(), qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [this] {
        setAudioStatus("Could not decode audio file");
    });
    decoder->start();
}

void ShaderPreview::setAudioStatus(const QString &message) {
    if (audioMessage == message) return;
    audioMessage = message;
    emit audioStatusChanged(message);
}

void ShaderPreview::syncAudio() {
    QAudioDevice device;
    const bool needed = engine.needsAudio() && audioMode == AudioMode::Live && audioDeviceId != "disabled";
    if (audioMode == AudioMode::File) {
        if (audioSource) audioSource->disconnect(this);
        audioSource.reset();
        runningAudioDevice.clear();
        audioBuffer.clear();
        startFileAudio();
        return;
    }
    stopFileAudio();
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
        setAudioStatus(audioMode == AudioMode::Off || audioDeviceId == "disabled" ? "Audio input disabled" :
                       !needed ? "Audio input idle" : "Audio input unavailable");
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
        QList<QList<float>> waves;
        for (int channel = 0; channel < format.channelCount(); ++channel) {
            QList<float> wave(audioFrames, 0.f);
            for (int i = 0; i < count; ++i) {
                const auto *sample = audioBuffer.constData() + i * stride + channel * format.bytesPerSample();
                wave[audioFrames - count + i] = format.normalizedSampleValue(sample);
            }
            waves.append(std::move(wave));
        }
        auto frame = makeAudioFrame(std::move(waves));
        engine.setAudioFrame(std::move(frame));
        update();
    });
}

// Engine changes may create or release GL resources: run them with the widget's context current,
// then restart audio capture if the inputs changed and repaint.
template <typename Change> void ShaderPreview::changeEngine(Change &&change) {
    if (isValid()) makeCurrent();
    change();
    if (isValid()) doneCurrent();
    syncAudio();
    update();
}

void ShaderPreview::load(const QByteArray &source, const QJsonObject &header, const QString &path) {
    changeEngine([&] { engine.load(source, header, path); });
}

void ShaderPreview::setLiveInput(std::unique_ptr<LiveInput> input) {
    changeEngine([&] { engine.setLiveInput(std::move(input)); });
}

void ShaderPreview::useTestCard() { setLiveInput(nullptr); }

void ShaderPreview::setShaderInput(const QString &name, const QByteArray &source,
                                  const QJsonObject &header, const QString &path) {
    changeEngine([&] { engine.setShaderInput(name, source, header, path); });
}

void ShaderPreview::setImage(const QString &name, const QImage &image) {
    changeEngine([&] { engine.setImage(name, image); });
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
