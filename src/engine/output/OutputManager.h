#pragma once
#include "CpuSendThread.h"
#include "VideoOutput.h"
#include <QElapsedTimer>
#include <QObject>
#include <array>
#include <memory>
#include <optional>

// The outputs the user asked for, and the objects that serve them while a GL context exists.
// Everything here runs in the render thread with the GL context current, except the CPU outputs' send() (their thread).
// Nothing is allocated, read back or sent while no output is enabled: active() is the only cost then.
class OutputManager : public QObject {
    Q_OBJECT
public:
    struct State {
        enum Level { Off, Ok, Error } level = Off;
        QString text;
        int receivers = -1;
        bool operator==(const State &o) const { return level == o.level && text == o.text && receivers == o.receivers; }
    };
    OutputManager();
    ~OutputManager() override;

    void setSettings(const OutputSettings &settings);
    const OutputSettings &settings() const { return current; }
    void setEnabled(OutputKind kind, bool enabled);
    bool isEnabled(OutputKind kind) const { return wanted[int(kind)]; }
    // For tests: an output that receives every picture read back
    void setTestOutput(std::shared_ptr<CpuOutput> output);
    bool active() const { return anyWanted; }
    State state(OutputKind kind) const { return states[int(kind)]; }

    // The picture just rendered into `framebuffer` (its texture `texture`, `size`), once per frame.
    void publish(unsigned framebuffer, unsigned texture, const QSize &size, unsigned hostFramebuffer);
    // The GL context is going away: the outputs asked for are kept and start again at the next publish().
    void release();
    // Frames the send thread has not been able to send in time
    uint64_t droppedFrames() const { return dropped + (thread ? thread->dropped() : 0); }

signals:
    void stateChanged(OutputKind kind, const OutputManager::State &state);

private:
    void setState(OutputKind kind, State::Level level, const QString &text, int receivers = -1);
    void stopCpu();
    void startCpu(const QSize &size);
    void readBack(unsigned framebuffer, const QSize &size);
    void releaseBuffers();
    void refreshState(OutputKind kind, const QString &text);

    OutputSettings current;
    std::array<bool, OutputKindCount> wanted{};
    bool anyWanted = false, testWanted = false;
    std::array<State, OutputKindCount> states;
    std::array<bool, OutputKindCount> failed{}; // the output failed to start: not tried again until enabled again
    std::array<std::unique_ptr<GpuOutput>, OutputKindCount> gpu;
    std::array<std::shared_ptr<CpuOutput>, OutputKindCount> cpu;
    std::shared_ptr<CpuOutput> testOutput;
    bool testStarted = false;
    std::unique_ptr<CpuSendThread> thread;
    uint64_t dropped = 0;
    QSize cpuSize;
    // Asynchronous read-back: the picture is read into a pixel buffer, mapped two frames later (no stall)
    static constexpr int kBuffers = 3;
    unsigned pixelBuffers[kBuffers]{};
    QSize bufferSize;
    int readIndex = 0;
    int64_t framesRead = 0;
    QElapsedTimer clock;
    unsigned counter = 0;
};
Q_DECLARE_METATYPE(OutputManager::State)
