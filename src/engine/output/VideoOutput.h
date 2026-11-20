#pragma once
// Video outputs: the rendered picture shared with other software.
//  - Syphon (macOS) and Spout (Windows): GPU texture sharing, the texture is copied on the GPU, never read back;
//  - v4l2loopback (Linux, a virtual webcam) and OMT (Open Media Transport, network): the picture is read back from
//    the GPU asynchronously, then converted and sent by a dedicated thread so that rendering never waits for it.
// Each output is optional at build time (ISFLAB_OUTPUT_* CMake options) and does nothing until it is enabled.

#include <QSize>
#include <QString>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

enum class OutputKind { Syphon, Spout, V4l2, Omt };
inline constexpr int OutputKindCount = 4;

QString outputKindName(OutputKind kind);
bool outputIsGpu(OutputKind kind);      // shares the texture (Syphon, Spout) rather than a read-back picture
bool outputCompiled(OutputKind kind);   // built into this executable (platform and CMake option)

struct OutputSettings {
    QString name = QStringLiteral("ISFLab");
    QString v4l2Device;      // "/dev/videoN"; empty: the first v4l2loopback device
    int omtQuality = 0;      // 0 = automatic, 1 = low, 50 = medium, 100 = high
    QString libraryFolder;   // an additional folder where to look for libomt
};

// A picture another process maps (Syphon, Spout): used with the render thread's GL context current.
struct GpuFrame {
    unsigned texture = 0;          // GL_TEXTURE_2D, RGBA8, the picture's bottom row first
    QSize size;
    unsigned hostFramebuffer = 0;  // the framebuffer to bind again afterwards
};
class GpuOutput {
public:
    virtual ~GpuOutput() = default;
    virtual bool start(const OutputSettings &settings, QString *error) = 0;
    virtual void publish(const GpuFrame &frame) = 0;
    virtual int receivers() const { return -1; } // -1: unknown
};

// A picture read back from the GPU, in the send thread.
struct CpuFrame {
    std::vector<uint8_t> rgba; // width * height * 4, RGBA, the picture's bottom row first (as OpenGL reads it)
    int width = 0, height = 0;
    int64_t timestamp100ns = 0;
};
class CpuOutput {
public:
    virtual ~CpuOutput() = default;
    // size: the pictures that will be sent. Called before the send thread runs.
    virtual bool start(const OutputSettings &settings, QSize size, QString *error) = 0;
    // In the send thread. The frame is only valid during the call.
    virtual void send(const CpuFrame &frame) = 0;
    // The output cannot change its size while it runs: it is started again when the picture's size changes.
    virtual bool fixedSize() const { return false; }
    // How the output goes: a message after the start (for example the device written to). Empty: nothing more to say.
    virtual QString description() const { return {}; }
    int receivers() const { return receiverCount.load(); }
protected:
    std::atomic<int> receiverCount{-1};
};

std::unique_ptr<GpuOutput> createSyphonOutput(); // nullptr when not built in
std::unique_ptr<GpuOutput> createSpoutOutput();  // nullptr when not built in
std::unique_ptr<CpuOutput> createV4l2Output();   // nullptr when not built in
std::unique_ptr<CpuOutput> createOmtOutput();    // nullptr when not built in
// For tests: receives every frame sent
std::unique_ptr<CpuOutput> createTapOutput(std::function<void(const CpuFrame &)> receive);
