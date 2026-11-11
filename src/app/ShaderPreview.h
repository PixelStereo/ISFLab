#pragma once
#include "ShaderEngine.h"
#include <QOpenGLWidget>

class QAudioSource;
class QAudioDecoder;
class QTimer;
#include <QElapsedTimer>

class ShaderPreview : public QOpenGLWidget {
    Q_OBJECT
public:
    static void configureDefaultFormat();
    explicit ShaderPreview(QWidget *parent = nullptr);
    ~ShaderPreview() override;
    void load(const QByteArray &source, const QJsonObject &header, const QString &path);
    QSize renderSize() const;
    void setFixedRenderSize(const QSize &size);
    void setPlaying(bool playing);
    void setFilterEnabled(bool enabled);
    void seek(double time);
    void setValue(const QString &name, const QVariant &value);
    QVariant value(const QString &name) const { return engine.value(name); }
    void setTransitionAnimation(bool enabled);
    void useTestCard();
    void setLiveInput(std::unique_ptr<LiveInput> input);
    void setShaderInput(const QString &name, const QByteArray &source, const QJsonObject &header, const QString &path);
    enum class AudioMode { Live, Off, File };
    void setAudioMode(AudioMode mode, const QString &file = {});
    void setAudioDevice(const QByteArray &id);
    QString audioStatus() const { return audioMessage; }
    void setImage(const QString &name, const QImage &image);
signals:
    void audioStatusChanged(const QString &message);
    void status(const QString &message);
    void glslDiagnostic(const QString &stage, const QString &file, const QString &log, int line);
    void inputStatus(const QString &message);
    void testCardActiveChanged(bool active);
    void shaderInputError(const QString &name, const QString &message);
    void renderPointerMoved(const QPointF &normalized, const QPointF &pixels);
    void renderSizeChanged(const QSize &size);
    void timeChanged(double time);
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void initializeGL() override;
    void paintGL() override;
private:
    void syncAudio();
    void startFileAudio();
    void stopFileAudio();
    void setAudioStatus(const QString &message);
    void reportPointer(const QPointF &position);
    void releaseEngine();
    QSize viewportSize() const;
    ShaderEngine engine;
    std::unique_ptr<QAudioSource> audioSource;
    QByteArray audioDeviceId, runningAudioDevice, audioBuffer;
    QString audioMessage, audioFile, fileLoadedPath;
    AudioMode audioMode = AudioMode::Live;
    std::unique_ptr<QAudioDecoder> decoder;
    QList<QList<float>> fileWaves;
    int fileRate = 0;
    QElapsedTimer fileClock;
    QTimer *fileTimer = nullptr;
    bool microphoneRequestPending = false;
    QSize reportedRenderSize;
    QMetaObject::Connection contextCleanup;
};
