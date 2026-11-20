#define GL_SILENCE_DEPRECATION
#import <OpenGL/OpenGL.h>
#include "LiveInput.h"
#include "OutputManager.h"
#include <QOpenGLFramebufferObject>
#include <QApplication>
#include <QElapsedTimer>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QLabel>
#include <QUuid>

class SyphonProbe final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    SyphonProbe(QLabel *status, bool automated)
        : status(status), automated(automated), name("ISFLab test " + QUuid::createUuid().toString(QUuid::WithoutBraces)) {
        setMinimumSize(280, 100);
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] { if (!done) update(); });
        timer->start(33);
        clock.start();
    }

    ~SyphonProbe() override {
        if (isValid()) {
            makeCurrent();
            receiver.reset();
            outputs.release();
            source.reset();
            doneCurrent();
        }
    }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        // The Syphon output of the application: the texture of an offscreen picture, upright (red above blue)
        OutputSettings settings;
        settings.name = name;
        outputs.setSettings(settings);
        outputs.setEnabled(OutputKind::Syphon, true);
        source = std::make_unique<QOpenGLFramebufferObject>(QSize(64, 32));
    }

    void paintGL() override {
        glClearColor(done ? 0.1f : 0.2f, passed ? 0.5f : 0.15f, 0.25f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (done) return;
        if (clock.elapsed() > 8000) {
            finish(false, QString("server created: %1; discovered: %2; attached: %3")
                   .arg(outputs.state(OutputKind::Syphon).level == OutputManager::State::Ok ? "yes" : "no",
                        receiver ? "yes" : "no", outputs.state(OutputKind::Syphon).receivers > 0 ? "yes" : "no"));
            return;
        }
        source->bind();
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 16, 64, 16);
        glClearColor(1.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(0, 0, 64, 16);
        glClearColor(0.f, 0.f, 1.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        source->release();
        outputs.publish(source->handle(), source->texture(), source->size(), defaultFramebufferObject());
        if (!receiver) {
            auto discovery = createLiveInput({});
            for (const auto &sender : discovery->senders())
                if (sender.label.contains(name)) {
                    receiver = createLiveInput(sender.id);
                    break;
                }
        }
        if (!receiver) return;
        const auto image = receiver->frame(defaultFramebufferObject());
        if (!image.isNull()) {
            const auto top = image.pixelColor(10, 4), bottom = image.pixelColor(10, 28);
            finish(image.size() == QSize(64, 32) && top.red() > 250 && top.blue() < 5 && bottom.blue() > 250 && bottom.red() < 5,
                   QString("size %1x%2; top %3, bottom %4").arg(image.width()).arg(image.height()).arg(top.name(), bottom.name()));
        }
    }
private:
    void finish(bool success, const QString &details) {
        done = true;
        passed = success;
        status->setText(success ? "PASS: the Syphon output is discovered and its pixels are received, upright."
                                : "FAIL: " + details);
        if (automated) QTimer::singleShot(0, qApp, [success] { QCoreApplication::exit(success ? 0 : 1); });
        else update();
    }
    QLabel *status;
    bool automated, done = false, passed = false;
    QString name;
    QElapsedTimer clock;
    OutputManager outputs;
    std::unique_ptr<QOpenGLFramebufferObject> source;
    std::unique_ptr<LiveInput> receiver;
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (!app.primaryScreen()) return 77;
    QWidget window;
    window.setWindowTitle("ISFLab Syphon Test");
    auto *layout = new QVBoxLayout(&window);
    auto *status = new QLabel("Testing a local Syphon sender…");
    layout->addWidget(status);
    layout->addWidget(new SyphonProbe(status, app.arguments().contains("--automated")));
    window.show();
    return app.exec();
}
