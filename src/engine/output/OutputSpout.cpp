// Spout output (Windows): the render's texture is shared through the Spout SDK's OpenGL/Direct3D interop (a shared
// texture other applications open). No read-back. Used with the GL context current.
#include "VideoOutput.h"
#include <SpoutSender.h>

namespace {
class SpoutOutput final : public GpuOutput {
public:
    ~SpoutOutput() override { sender.ReleaseSender(); }
    bool start(const OutputSettings &settings, QString *) override {
        name = settings.name.toUtf8();
        sender.SetSenderName(name.constData());
        return true; // the sender is created with the first texture
    }
    void publish(const GpuFrame &frame) override {
        // GL's first row is the bottom, Direct3D's the top: invert. The host framebuffer is bound again afterwards,
        // otherwise Spout would leave the widget's framebuffer unbound.
        sent = sender.SendTexture(frame.texture, GL_TEXTURE_2D, unsigned(frame.size.width()),
                                  unsigned(frame.size.height()), true, frame.hostFramebuffer);
    }
private:
    SpoutSender sender;
    QByteArray name;
    bool sent = false;
};
}

std::unique_ptr<GpuOutput> createSpoutOutput() { return std::make_unique<SpoutOutput>(); }
