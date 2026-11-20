#pragma once
#include "VideoOutput.h"
#include <condition_variable>
#include <mutex>
#include <thread>

// Sends the pictures read back by the render thread to the CPU outputs, in a thread of its own.
// Three buffers rotate: one being sent, one waiting, one for the render thread to fill. A picture that is late
// is replaced by the next one (dropped): rendering never waits for the outputs. Buffers are never reallocated
// from one picture to the next (std::vector::resize to the same size does nothing).
class CpuSendThread {
public:
    explicit CpuSendThread(std::vector<std::shared_ptr<CpuOutput>> outputs);
    ~CpuSendThread(); // stops and joins; a picture still waiting is not sent
    CpuFrame *acquire();       // a buffer to fill, never blocks
    void submit(CpuFrame *frame);
    uint64_t sent() const { return sentFrames.load(); }
    uint64_t dropped() const { return droppedFrames.load(); }
private:
    void run();
    std::vector<std::shared_ptr<CpuOutput>> outputs;
    std::mutex mutex;
    std::condition_variable wake;
    CpuFrame pool[3];
    std::vector<CpuFrame *> free;
    CpuFrame *pending = nullptr;
    bool quit = false;
    std::atomic<uint64_t> sentFrames{0}, droppedFrames{0};
    std::thread thread;
};
