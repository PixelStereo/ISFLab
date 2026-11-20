#include "CpuSendThread.h"

CpuSendThread::CpuSendThread(std::vector<std::shared_ptr<CpuOutput>> list) : outputs(std::move(list)) {
    for (auto &frame : pool) free.push_back(&frame);
    thread = std::thread([this] { run(); });
}

CpuSendThread::~CpuSendThread() {
    {
        std::lock_guard lock(mutex);
        quit = true;
    }
    wake.notify_all();
    if (thread.joinable()) thread.join();
}

CpuFrame *CpuSendThread::acquire() {
    std::lock_guard lock(mutex);
    if (!free.empty()) {
        auto *frame = free.back();
        free.pop_back();
        return frame;
    }
    if (pending) { // the send thread is late: the waiting picture is replaced
        auto *frame = pending;
        pending = nullptr;
        ++droppedFrames;
        return frame;
    }
    return nullptr;
}

void CpuSendThread::submit(CpuFrame *frame) {
    {
        std::lock_guard lock(mutex);
        if (pending) {
            free.push_back(pending);
            ++droppedFrames;
        }
        pending = frame;
    }
    wake.notify_one();
}

void CpuSendThread::run() {
    for (;;) {
        CpuFrame *frame = nullptr;
        {
            std::unique_lock lock(mutex);
            wake.wait(lock, [this] { return quit || pending; });
            if (quit) return;
            frame = pending;
            pending = nullptr;
        }
        for (auto &output : outputs) output->send(*frame);
        ++sentFrames;
        std::lock_guard lock(mutex);
        free.push_back(frame);
    }
}
