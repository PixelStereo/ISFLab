// The video outputs' logic that needs no GL context: pixel conversions, the send thread and the optional outputs.
#include "CpuSendThread.h"
#include "OutputManager.h"
#include "PixelConvert.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <mutex>

namespace {
void require(bool condition, const char *message) {
    if (!condition) qFatal("%s", message);
}
// A 2x2 picture as OpenGL reads it: the bottom row first
const uint8_t rgba[16] = {
    255, 0, 0, 255,   0, 255, 0, 255,      // bottom row: red, green
    0, 0, 255, 128,   255, 255, 255, 255,  // top row: blue (half transparent), white
};
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    uint8_t bgra[16];
    PixelConvert::rgbaBottomUpToBgra(rgba, 2, 2, bgra);
    const uint8_t topBlue[4] = {255, 0, 0, 128}; // top row first, BGRA
    require(std::equal(topBlue, topBlue + 4, bgra), "BGRA: the top row comes first, red and blue swapped");
    const uint8_t bottomGreen[4] = {0, 255, 0, 255};
    require(std::equal(bottomGreen, bottomGreen + 4, bgra + 12), "BGRA: the bottom row comes last");

    uint8_t yuyv[8];
    PixelConvert::rgbaBottomUpToYuyv(rgba, 2, 2, yuyv);
    // Top row blue and white: Y of blue (limited range) is 41, of white 235; the pair's chroma is the pair's average
    require(yuyv[0] == 41 && yuyv[2] == 235, "YUYV: luma of the top row (blue, white)");
    require(yuyv[1] > 128 && yuyv[3] > 100, "YUYV: the blue-white pair leans blue");
    // Bottom row red and green: luma 82 and 145
    require(std::abs(yuyv[4] - 82) <= 1 && std::abs(yuyv[6] - 145) <= 1, "YUYV: luma of the bottom row (red, green)");
    // An odd width is cropped to its pairs, rows keep their stride in the source
    const uint8_t odd[24] = {10, 10, 10, 255, 20, 20, 20, 255, 30, 30, 30, 255,   // bottom row, 3 pixels
                             200, 200, 200, 255, 210, 210, 210, 255, 220, 220, 220, 255}; // top row
    uint8_t cropped[4];
    PixelConvert::rgbaBottomUpToYuyv(odd, 3, 2, cropped);
    require(cropped[0] > 150 && cropped[2] > 150, "YUYV: an odd width keeps the pairs of the first pixels");
    PixelConvert::rgbaBottomUpToYuyv(odd, 3, 2, cropped); // twice: no state

    // The send thread: every output gets the pictures in order; a late send drops pictures, never blocks
    {
        std::mutex lock;
        std::vector<int> received;
        std::atomic<bool> slow{true};
        auto tap = std::shared_ptr<CpuOutput>(createTapOutput([&](const CpuFrame &frame) {
            if (slow) QThread::msleep(60);
            std::lock_guard guard(lock);
            received.push_back(frame.width);
        }));
        CpuSendThread thread({tap});
        QElapsedTimer clock;
        clock.start();
        int submitted = 0;
        for (int i = 1; i <= 20; ++i) {
            CpuFrame *frame = thread.acquire();
            require(frame != nullptr, "A buffer is always available to the render thread");
            frame->rgba.resize(16);
            frame->width = i;
            thread.submit(frame);
            ++submitted;
        }
        require(clock.elapsed() < 500, "Submitting pictures to a slow output does not wait for it");
        slow = false;
        QThread::msleep(300);
        std::lock_guard guard(lock);
        require(!received.empty() && received.size() < size_t(submitted), "A slow output drops pictures");
        require(std::is_sorted(received.begin(), received.end()), "Pictures are sent in order");
        require(received.back() == 20, "The last picture is the one that is sent");
        require(thread.dropped() > 0, "Dropped pictures are counted");
    }

    // The outputs are optional and do nothing until enabled
    OutputManager manager;
    require(!manager.active(), "No output is enabled at start: nothing is read back");
    for (int i = 0; i < OutputKindCount; ++i) {
        const auto kind = OutputKind(i);
        manager.setEnabled(kind, true);
        if (outputCompiled(kind)) {
            require(manager.active() && manager.isEnabled(kind), "An enabled output makes the manager active");
            manager.setEnabled(kind, false);
        } else {
            require(!manager.active() && manager.state(kind).level == OutputManager::State::Error,
                    "An output that is not built in reports it, and stays disabled");
        }
        require(!manager.active(), "Disabling the outputs leaves the manager idle");
    }
#if defined(Q_OS_MACOS)
    require(outputCompiled(OutputKind::Syphon) == bool(createSyphonOutput() != nullptr), "Syphon is the macOS output");
    require(!outputCompiled(OutputKind::Spout) && !outputCompiled(OutputKind::V4l2), "Spout and v4l2 are not built on macOS");
#elif defined(Q_OS_LINUX)
    require(!outputCompiled(OutputKind::Syphon) && !outputCompiled(OutputKind::Spout), "Syphon and Spout are not built on Linux");
#endif
    if (outputCompiled(OutputKind::V4l2)) {
        OutputSettings settings;
        settings.v4l2Device = "/dev/isflab-missing";
        QString error;
        require(!createV4l2Output()->start(settings, QSize(64, 64), &error) && !error.isEmpty(),
                "A missing virtual webcam device is reported");
    }
    if (outputCompiled(OutputKind::Omt)) {
        OutputSettings settings;
        settings.libraryFolder = "/nonexistent";
        QString error;
        auto omt = createOmtOutput();
        if (!omt->start(settings, QSize(64, 64), &error)) require(!error.isEmpty(), "A missing OMT library is reported");
    }
    return 0;
}
