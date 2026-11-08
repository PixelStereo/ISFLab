#pragma once
#include "ShaderEngine.h"
#include <QOpenGLWidget>

class ShaderPreview : public QOpenGLWidget {
    Q_OBJECT
public:
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
    void setImage(const QString &name, const QImage &image);
signals:
    void status(const QString &message);
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
    void reportPointer(const QPointF &position);
    void releaseEngine();
    QSize viewportSize() const;
    ShaderEngine engine;
    QSize reportedRenderSize;
    QMetaObject::Connection contextCleanup;
};
