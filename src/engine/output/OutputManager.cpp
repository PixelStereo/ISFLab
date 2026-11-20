#include "OutputManager.h"
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <algorithm>
#include <cstring>

QString outputKindName(OutputKind kind) {
    switch (kind) {
    case OutputKind::Syphon: return QStringLiteral("Syphon");
    case OutputKind::Spout: return QStringLiteral("Spout");
    case OutputKind::V4l2: return QStringLiteral("Virtual webcam (v4l2loopback)");
    case OutputKind::Omt: return QStringLiteral("OMT");
    }
    return {};
}

bool outputIsGpu(OutputKind kind) { return kind == OutputKind::Syphon || kind == OutputKind::Spout; }

bool outputCompiled(OutputKind kind) {
    switch (kind) {
    case OutputKind::Syphon:
#ifdef ISFLAB_OUTPUT_SYPHON
        return true;
#else
        return false;
#endif
    case OutputKind::Spout:
#ifdef ISFLAB_OUTPUT_SPOUT
        return true;
#else
        return false;
#endif
    case OutputKind::V4l2:
#ifdef ISFLAB_OUTPUT_V4L2
        return true;
#else
        return false;
#endif
    case OutputKind::Omt:
#ifdef ISFLAB_OUTPUT_OMT
        return true;
#else
        return false;
#endif
    }
    return false;
}

namespace {
class TapOutput final : public CpuOutput {
public:
    explicit TapOutput(std::function<void(const CpuFrame &)> receive) : receive(std::move(receive)) {}
    bool start(const OutputSettings &, QSize, QString *) override { return true; }
    void send(const CpuFrame &frame) override { receive(frame); }
private:
    std::function<void(const CpuFrame &)> receive;
};
}
std::unique_ptr<CpuOutput> createTapOutput(std::function<void(const CpuFrame &)> receive) {
    return std::make_unique<TapOutput>(std::move(receive));
}

OutputManager::OutputManager() {
    qRegisterMetaType<OutputManager::State>();
    clock.start();
}

OutputManager::~OutputManager() = default; // release() has already run with the context current

void OutputManager::setSettings(const OutputSettings &settings) { current = settings; }

void OutputManager::setState(OutputKind kind, State::Level level, const QString &text, int receivers) {
    const State state{level, text, receivers};
    if (states[int(kind)] == state) return;
    states[int(kind)] = state;
    emit stateChanged(kind, state);
}

void OutputManager::setEnabled(OutputKind kind, bool enabled) {
    if (wanted[int(kind)] == enabled) return;
    if (enabled && !outputCompiled(kind)) {
        setState(kind, State::Error, QStringLiteral("Not available in this build."));
        return;
    }
    wanted[int(kind)] = enabled;
    failed[int(kind)] = false;
    anyWanted = testWanted || std::any_of(wanted.begin(), wanted.end(), [](bool on) { return on; });
    if (enabled) {
        // The output starts at the next publish(), when the picture's size is known
        setState(kind, State::Ok, QStringLiteral("Starting…"));
        return;
    }
    if (outputIsGpu(kind)) gpu[int(kind)].reset();
    else {
        stopCpu();                // the thread runs the outputs: stop it, then start the ones that remain
        cpu[int(kind)].reset();
        cpuSize = {};             // startCpu() at the next picture
    }
    if (!anyWanted) releaseBuffers();
    setState(kind, State::Off, {});
}

void OutputManager::setTestOutput(std::shared_ptr<CpuOutput> output) {
    stopCpu();
    testOutput = std::move(output);
    testWanted = bool(testOutput);
    testStarted = false;
    cpuSize = {};
    anyWanted = testWanted || std::any_of(wanted.begin(), wanted.end(), [](bool on) { return on; });
    if (!anyWanted) releaseBuffers();
}

void OutputManager::stopCpu() {
    if (thread) dropped += thread->dropped();
    thread.reset(); // joins: the outputs are no longer used by their thread
}

void OutputManager::startCpu(const QSize &size) {
    stopCpu();
    cpuSize = size;
    std::vector<std::shared_ptr<CpuOutput>> running;
    for (OutputKind kind : {OutputKind::V4l2, OutputKind::Omt}) {
        const int i = int(kind);
        if (!wanted[i] || failed[i]) continue;
        // A running output that can change its size keeps running
        if (!cpu[i] || cpu[i]->fixedSize()) {
            cpu[i] = kind == OutputKind::V4l2 ? std::shared_ptr<CpuOutput>(createV4l2Output())
                                              : std::shared_ptr<CpuOutput>(createOmtOutput());
            QString error;
            if (!cpu[i] || !cpu[i]->start(current, size, &error)) {
                cpu[i].reset();
                failed[i] = true;
                setState(kind, State::Error, error.isEmpty() ? QStringLiteral("Could not start.") : error);
                continue;
            }
            setState(kind, State::Ok, cpu[i]->description().isEmpty() ? current.name : cpu[i]->description(),
                     cpu[i]->receivers());
        }
        running.push_back(cpu[i]);
    }
    if (testOutput) {
        if (!testStarted) {
            QString error;
            testStarted = testOutput->start(current, size, &error);
        }
        if (testStarted) running.push_back(testOutput);
    }
    if (!running.empty()) thread = std::make_unique<CpuSendThread>(std::move(running));
}

void OutputManager::releaseBuffers() {
    if (pixelBuffers[0]) {
        auto *context = QOpenGLContext::currentContext();
        if (context) context->functions()->glDeleteBuffers(kBuffers, pixelBuffers);
        std::memset(pixelBuffers, 0, sizeof pixelBuffers);
    }
    bufferSize = {};
    framesRead = 0;
}

void OutputManager::release() {
    stopCpu();
    for (auto &output : gpu) output.reset();
    for (auto &output : cpu) output.reset();
    testStarted = false;
    cpuSize = {};
    releaseBuffers();
    for (int i = 0; i < OutputKindCount; ++i) failed[i] = false;
}

void OutputManager::refreshState(OutputKind kind, const QString &text) {
    const int receivers = outputIsGpu(kind) ? (gpu[int(kind)] ? gpu[int(kind)]->receivers() : -1)
                                            : (cpu[int(kind)] ? cpu[int(kind)]->receivers() : -1);
    setState(kind, State::Ok, text, receivers);
}

void OutputManager::publish(unsigned framebuffer, unsigned texture, const QSize &size, unsigned hostFramebuffer) {
    if (!anyWanted || size.isEmpty()) return;
    ++counter;
    for (OutputKind kind : {OutputKind::Syphon, OutputKind::Spout}) {
        const int i = int(kind);
        if (!wanted[i] || failed[i]) continue;
        if (!gpu[i]) {
            gpu[i] = kind == OutputKind::Syphon ? createSyphonOutput() : createSpoutOutput();
            QString error;
            if (!gpu[i] || !gpu[i]->start(current, &error)) {
                gpu[i].reset();
                failed[i] = true;
                setState(kind, State::Error, error.isEmpty() ? QStringLiteral("Could not start.") : error);
                continue;
            }
            setState(kind, State::Ok, current.name);
        }
        gpu[i]->publish({texture, size, hostFramebuffer});
        if (counter % 30 == 1) refreshState(kind, current.name);
    }
    bool wantsCpu = testWanted;
    for (OutputKind kind : {OutputKind::V4l2, OutputKind::Omt}) wantsCpu |= wanted[int(kind)] && !failed[int(kind)];
    if (!wantsCpu) return;
    if (cpuSize != size) startCpu(size);
    if (!thread) return;
    readBack(framebuffer, size);
    if (counter % 30 == 1)
        for (OutputKind kind : {OutputKind::V4l2, OutputKind::Omt})
            if (cpu[int(kind)]) refreshState(kind, states[int(kind)].text);
}

void OutputManager::readBack(unsigned framebuffer, const QSize &size) {
    auto *gl = QOpenGLContext::currentContext()->extraFunctions();
    const GLsizeiptr bytes = GLsizeiptr(size.width()) * size.height() * 4;
    if (!pixelBuffers[0] || bufferSize != size) { // only when the size changes
        if (!pixelBuffers[0]) gl->glGenBuffers(kBuffers, pixelBuffers);
        for (unsigned buffer : pixelBuffers) {
            gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
            gl->glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
        }
        bufferSize = size;
        framesRead = 0;
    }
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, pixelBuffers[framesRead % kBuffers]);
    gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    gl->glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    ++framesRead;
    if (framesRead >= kBuffers) { // the picture read two frames ago is complete: map it without waiting
        gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, pixelBuffers[framesRead % kBuffers]);
        const void *pixels = gl->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT);
        if (pixels) {
            if (CpuFrame *frame = thread->acquire()) {
                frame->rgba.resize(size_t(bytes)); // no reallocation unless the size changed
                std::memcpy(frame->rgba.data(), pixels, size_t(bytes));
                frame->width = size.width();
                frame->height = size.height();
                frame->timestamp100ns = clock.nsecsElapsed() / 100;
                thread->submit(frame);
            }
            gl->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
    }
    // A pixel buffer left bound would turn every later glReadPixels (grabFramebuffer) into a read into it
    gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}
