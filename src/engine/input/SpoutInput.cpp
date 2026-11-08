#include "LiveInput.h"
#include <QElapsedTimer>
#include <SpoutReceiver.h>

namespace {
class SpoutInput final : public LiveInput {
public:
    explicit SpoutInput(const QString &sender) : name(sender.toUtf8()) {
        if (!name.isEmpty()) receiver.SetReceiverName(name.constData());
    }
    ~SpoutInput() override { if (!name.isEmpty()) receiver.ReleaseReceiver(); }

    QList<LiveSender> senders() override {
        QList<LiveSender> result;
        const int total = receiver.GetSenderCount();
        for (int i = 0; i < total; ++i) {
            char sender[256]{};
            if (receiver.GetSender(i, sender, sizeof sender)) {
                const auto value = QString::fromUtf8(sender);
                result << LiveSender{value, value};
            }
        }
        return result;
    }

    QImage frame(unsigned int framebuffer) override {
        if (name.isEmpty() || (retry.isValid() && retry.elapsed() < 500)) return {};
        const bool received = receiver.ReceiveImage(pixels.isNull() ? nullptr : pixels.bits(), GL_RGBA, true, framebuffer);
        if (receiver.IsUpdated()) {
            const auto w = receiver.GetSenderWidth(), h = receiver.GetSenderHeight();
            pixels = (w && h && w <= 8192 && h <= 8192) ? QImage(int(w), int(h), QImage::Format_RGBA8888) : QImage();
            return {};
        }
        if (!received) {
            retry.start();
            return {};
        }
        retry.invalidate();
        return receiver.IsFrameNew() ? pixels : QImage();
    }
private:
    QByteArray name;
    SpoutReceiver receiver;
    QImage pixels;
    QElapsedTimer retry;
};
}

std::unique_ptr<LiveInput> createLiveInput(const QString &sender, QSize) {
    return std::make_unique<SpoutInput>(sender);
}
