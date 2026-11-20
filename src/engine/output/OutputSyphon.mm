// Syphon output (macOS): the render's texture is handed to a SyphonOpenGLServer, which copies it on the GPU into an
// IOSurface other applications map. No read-back. The server belongs to the GL context that is current when it starts.
#define GL_SILENCE_DEPRECATION
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <Syphon/SyphonOpenGLServer.h>
#include "VideoOutput.h"

namespace {
class SyphonOutput final : public GpuOutput {
public:
    ~SyphonOutput() override {
        @autoreleasepool { [server stop]; server = nil; }
    }
    bool start(const OutputSettings &settings, QString *error) override {
        @autoreleasepool {
            const auto context = CGLGetCurrentContext();
            if (context)
                server = [[SyphonOpenGLServer alloc] initWithName:settings.name.toNSString() context:context options:nil];
            if (!server) {
                if (error) *error = QStringLiteral("The Syphon server could not start.");
                return false;
            }
        }
        return true;
    }
    void publish(const GpuFrame &frame) override {
        if (!server) return;
        @autoreleasepool {
            // Syphon takes the texture as OpenGL has it (its bottom row first): not flipped
            [server publishFrameTexture:frame.texture textureTarget:GL_TEXTURE_2D
                            imageRegion:NSMakeRect(0, 0, frame.size.width(), frame.size.height())
                      textureDimensions:NSMakeSize(frame.size.width(), frame.size.height()) flipped:NO];
            clients = server.hasClients ? 1 : 0;
        }
    }
    int receivers() const override { return clients; }
private:
    SyphonOpenGLServer *server = nil;
    int clients = 0;
};
}

std::unique_ptr<GpuOutput> createSyphonOutput() { return std::make_unique<SyphonOutput>(); }
