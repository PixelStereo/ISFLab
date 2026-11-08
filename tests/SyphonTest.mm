#define GL_SILENCE_DEPRECATION
#import <OpenGL/OpenGL.h>
#import <Syphon/SyphonOpenGLServer.h>
#include "LiveInput.h"
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
            [server stop];
            server = nil;
            doneCurrent();
        }
    }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        server = [[SyphonOpenGLServer alloc] initWithName:name.toNSString()
                                               context:CGLGetCurrentContext() options:nil];
    }

    void paintGL() override {
        glClearColor(done ? 0.1f : 0.2f, passed ? 0.5f : 0.15f, 0.25f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (done) return;
        if (clock.elapsed() > 8000) {
            finish(false, QString("server created: %1; discovered: %2; attached: %3")
                   .arg(server ? "yes" : "no", receiver ? "yes" : "no", [server hasClients] ? "yes" : "no"));
            return;
        }
        if ([server bindToDrawFrameOfSize:NSMakeSize(64, 32)]) {
            glClearColor(32 / 255.f, 128 / 255.f, 240 / 255.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
            [server unbindAndPublish];
        }
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
            const auto color = image.pixelColor(0, 0);
            finish(image.size() == QSize(64, 32) && qAbs(color.red() - 32) < 3 &&
                   qAbs(color.green() - 128) < 3 && qAbs(color.blue() - 240) < 3,
                   QString("size %1x%2; color %3").arg(image.width()).arg(image.height()).arg(color.name()));
        }
    }
private:
    void finish(bool success, const QString &details) {
        done = true;
        passed = success;
        status->setText(success ? "PASS: Syphon sender discovered and pixels received."
                                : "FAIL: " + details);
        if (automated) QTimer::singleShot(0, qApp, [success] { QCoreApplication::exit(success ? 0 : 1); });
        else update();
    }
    QLabel *status;
    bool automated, done = false, passed = false;
    QString name;
    QElapsedTimer clock;
    SyphonOpenGLServer *server = nil;
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
