// OMT output with the real libomt (skipped, exit code 77, when the library or its discovery service is missing):
// a picture sent by the output is received by an OMT receiver of the same process, upright. With --bench, also prints
// the time the conversion and the VMX encoding take per picture at 720p and 1080p.
#include "OutputManager.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLibrary>
#include <QThread>
#include <QUuid>
#include <cstddef>
#include <cstring>
#include <ctime>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <iostream>

namespace {
struct Frame { // OMTMediaFrame (libomt.h)
    int Type;
    int64_t Timestamp;
    int Codec;
    int Width, Height, Stride;
    int Flags;
    int FrameRateN, FrameRateD;
    float AspectRatio;
    int ColorSpace;
    int SampleRate, Channels, SamplesPerChannel;
    void *Data;
    int DataLength;
    void *CompressedData;
    int CompressedLength;
    void *FrameMetadata;
    int FrameMetadataLength;
};

double processCpuSeconds() { // all threads
#ifdef Q_OS_WIN
    FILETIME created, exited, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    const auto seconds = [](FILETIME t) { return double((uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime) / 1e7; };
    return seconds(kernel) + seconds(user);
#else
    timespec time{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time);
    return double(time.tv_sec) + double(time.tv_nsec) / 1e9;
#endif
}

CpuFrame picture(int width, int height, int phase) {
    CpuFrame frame;
    frame.width = width;
    frame.height = height;
    frame.rgba.resize(size_t(width) * height * 4);
    for (int y = 0; y < height; ++y) // red on the top half, blue at the bottom (the bottom row first), plus a moving texture
        for (int x = 0; x < width; ++x) {
            uint8_t *pixel = frame.rgba.data() + (size_t(y) * width + x) * 4;
            const uint8_t noise = uint8_t(((x * 7 + y * 13 + phase * 31) ^ (x * y)) & 0x3f);
            const bool top = y >= height / 2;
            pixel[0] = top ? uint8_t(192 + noise) : noise;
            pixel[1] = noise / 2;
            pixel[2] = top ? noise : uint8_t(192 + noise);
            pixel[3] = 255;
        }
    return frame;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const bool bench = app.arguments().contains("--bench");
    OutputSettings settings;
    settings.name = "ISFLab test " + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto output = createOmtOutput();
    QString error;
    if (!output || !output->start(settings, QSize(640, 360), &error)) {
        std::cerr << "OMT unavailable: " << qPrintable(error) << "\n";
        return 77;
    }
    if (bench) {
        for (const QSize size : {QSize(1280, 720), QSize(1920, 1080)}) {
            std::vector<CpuFrame> frames;
            for (int i = 0; i < 8; ++i) frames.push_back(picture(size.width(), size.height(), i));
            for (int i = 0; i < 10; ++i) output->send(frames[size_t(i) % 8]); // warm-up
            QThread::msleep(500);
            const double cpuBefore = processCpuSeconds();
            QElapsedTimer clock;
            clock.start();
            const int count = 120;
            for (int i = 0; i < count; ++i) output->send(frames[size_t(i) % 8]);
            const double wall = clock.nsecsElapsed() / 1e6 / count;
            QThread::msleep(1000); // libomt may encode in threads of its own: let them finish
            std::cout << size.width() << "x" << size.height() << ": " << wall << " ms per picture in send() (conversion + libomt), "
                      << (processCpuSeconds() - cpuBefore) * 1000 / count << " ms of CPU time per picture in all threads\n";
        }
        return 0;
    }
    // The receiving side, through the library's C interface
    QLibrary omt;
    for (const QString &dir : {qEnvironmentVariable("OMT_LIBRARY_DIR"), QStringLiteral("/usr/local/lib"), QStringLiteral("/opt/homebrew/lib")}) {
#ifdef Q_OS_WIN
        omt.setFileName(dir + "/libomt.dll");
#elif defined(Q_OS_MACOS)
        omt.setFileName(dir + "/libomt.dylib");
#else
        omt.setFileName(dir + "/libomt.so");
#endif
        if (omt.load()) break;
    }
    using Discover = char **(*)(int *);
    using Create = void *(*)(const char *, int, int, int);
    using Receive = Frame *(*)(void *, int, int);
    using Destroy = void (*)(void *);
    auto discover = reinterpret_cast<Discover>(omt.resolve("omt_discovery_getaddresses"));
    auto create = reinterpret_cast<Create>(omt.resolve("omt_receive_create"));
    auto receive = reinterpret_cast<Receive>(omt.resolve("omt_receive"));
    auto destroy = reinterpret_cast<Destroy>(omt.resolve("omt_receive_destroy"));
    if (!discover || !create || !receive || !destroy) return 77;
    CpuFrame sent = picture(640, 360, 0);
    void *receiver = nullptr;
    QElapsedTimer wait;
    wait.start();
    while (wait.elapsed() < 10000) {
        output->send(sent);
        if (!receiver) {
            int count = 0;
            char **addresses = discover(&count);
            for (int i = 0; addresses && i < count; ++i)
                if (QString::fromUtf8(addresses[i]).contains(settings.name)) {
                    receiver = create(addresses[i], 2 /* video */, 2 /* BGRA */, 0);
                    break;
                }
        } else if (Frame *frame = receive(receiver, 2, 200)) {
            if (frame->Width != 640 || frame->Height != 360) { std::cerr << "wrong size\n"; return 1; }
            const auto *pixels = static_cast<const uint8_t *>(frame->Data);
            const auto *top = pixels + size_t(frame->Stride) * 4 + 4 * 10, *bottom = pixels + size_t(frame->Stride) * (frame->Height - 5) + 4 * 10;
            // BGRA, top row first: red above blue (the codec may be lossy)
            if (!pixels || top[2] < 150 || top[0] > 100 || bottom[0] < 150 || bottom[2] > 100) {
                std::cerr << "the received picture is not upright\n";
                return 1;
            }
            std::cout << "received " << frame->Width << "x" << frame->Height << " codec " << std::hex << frame->Codec << "\n";
            destroy(receiver);
            return 0;
        }
        QThread::msleep(30);
    }
    std::cerr << "no OMT picture received\n";
    return 1;
}
