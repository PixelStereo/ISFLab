// OMT output (Open Media Transport, MIT license): the picture is sent over the network as BGRA; libomt compresses it
// (VMX codec, on the CPU) and announces the source on the network. libomt and libvmx are loaded at run time: ISFLab
// builds and runs without them, the output works as soon as they are found (OMT_LIBRARY_DIR, next to the application,
// the output settings' folder, or the system's library folders).
// The structures match libomt.h (github.com/openmediatransport/libomt).
#include "VideoOutput.h"
#include "PixelConvert.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>

namespace {
struct MediaFrame {
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
constexpr int kVideo = 2;         // OMTFrameType_Video
constexpr int kBGRA = 0x41524742; // OMTCodec_BGRA
constexpr int kBT709 = 709;
constexpr int kFrameRate = 60;    // the preview repaints at 60 Hz

struct Library {
    bool ok = false;
    QString error, path;
    void *(*sendCreate)(const char *, int) = nullptr;
    void (*sendDestroy)(void *) = nullptr;
    int (*send)(void *, MediaFrame *) = nullptr;
    int (*sendConnections)(void *) = nullptr;
};

QStringList candidates(const QString &extra, QString *codec) {
#if defined(Q_OS_WIN)
    const QString name = QStringLiteral("libomt.dll"), vmx = QStringLiteral("libvmx.dll");
#elif defined(Q_OS_MACOS)
    const QString name = QStringLiteral("libomt.dylib"), vmx = QStringLiteral("libvmx.dylib");
#else
    const QString name = QStringLiteral("libomt.so"), vmx = QStringLiteral("libvmx.so");
#endif
    QStringList dirs;
    if (!extra.isEmpty()) dirs << extra;
    const QString env = qEnvironmentVariable("OMT_LIBRARY_DIR");
    if (!env.isEmpty()) dirs << env;
    const QString app = QCoreApplication::applicationDirPath();
    if (!app.isEmpty()) {
        dirs << app;
#ifdef Q_OS_MACOS
        dirs << QDir::cleanPath(app + "/../Frameworks") << QDir::cleanPath(app + "/../Resources");
#endif
    }
#if defined(Q_OS_MACOS)
    dirs << QStringLiteral("/usr/local/lib") << QStringLiteral("/opt/homebrew/lib");
#elif !defined(Q_OS_WIN)
    dirs << QStringLiteral("/usr/local/lib") << QStringLiteral("/usr/lib");
#endif
    QStringList paths;
    for (const auto &dir : dirs) paths << QDir(dir).filePath(name);
    paths << name;
    if (codec) *codec = vmx;
    return paths;
}

const Library &library(const QString &extra) {
    static QMutex mutex;
    static const Library *loaded = nullptr;
    QMutexLocker lock(&mutex);
    if (loaded && (loaded->ok || extra.isEmpty())) return *loaded;
    auto &lib = *new Library; // one record per attempt, never written again once published
    loaded = &lib;
    QString codec;
    const auto paths = candidates(extra, &codec);
    for (const auto &path : paths) { // libomt loads the libvmx codec: load it first, from the same folder
        if (!QFileInfo(path).isAbsolute() || !QFileInfo::exists(path)) continue;
        const auto vmx = QFileInfo(path).dir().filePath(codec);
        if (QFileInfo::exists(vmx)) {
            auto *library = new QLibrary(vmx);
            library->setLoadHints(QLibrary::ExportExternalSymbolsHint);
            if (!library->load()) delete library;
        }
        break;
    }
    QLibrary *omt = nullptr;
    QString failed;
    for (const auto &path : paths) {
        const bool file = QFileInfo(path).isAbsolute();
        if (file && !QFileInfo::exists(path)) continue;
        auto *candidate = new QLibrary(path);
        candidate->setLoadHints(QLibrary::ExportExternalSymbolsHint);
        if (candidate->load()) { omt = candidate; break; }
        if (file && failed.isEmpty()) failed = QDir::toNativeSeparators(path) + ": " + candidate->errorString();
        delete candidate;
    }
    if (!omt) {
        lib.error = failed.isEmpty()
            ? QStringLiteral("OMT library not found: place libomt and libvmx (github.com/openmediatransport) next to the "
                             "application, in the OMT_LIBRARY_DIR folder or in the system's library folder.")
            : QStringLiteral("OMT library found but not loaded: ") + failed;
        return lib;
    }
    lib.path = omt->fileName();
    lib.sendCreate = reinterpret_cast<void *(*)(const char *, int)>(omt->resolve("omt_send_create"));
    lib.sendDestroy = reinterpret_cast<void (*)(void *)>(omt->resolve("omt_send_destroy"));
    lib.send = reinterpret_cast<int (*)(void *, MediaFrame *)>(omt->resolve("omt_send"));
    lib.sendConnections = reinterpret_cast<int (*)(void *)>(omt->resolve("omt_send_connections"));
    if (!lib.sendCreate || !lib.sendDestroy || !lib.send) {
        lib.error = QStringLiteral("Incompatible OMT library: ") + lib.path;
        return lib;
    }
    lib.ok = true;
    return lib;
}

class OmtOutput final : public CpuOutput {
public:
    ~OmtOutput() override { if (sender) lib->sendDestroy(sender); }
    bool start(const OutputSettings &settings, QSize, QString *error) override {
#if defined(Q_OS_LINUX)
        // libomt announces its sources through Avahi and aborts the whole program when the service is not running
        if (!QFileInfo::exists(QStringLiteral("/run/avahi-daemon/socket")) &&
            !QFileInfo::exists(QStringLiteral("/var/run/avahi-daemon/socket"))) {
            if (error) *error = QStringLiteral("OMT needs the Avahi service (avahi-daemon), which is not running.");
            return false;
        }
#endif
        lib = &library(settings.libraryFolder);
        if (!lib->ok) {
            if (error) *error = lib->error;
            return false;
        }
        sender = lib->sendCreate(settings.name.toUtf8().constData(), settings.omtQuality);
        if (!sender) {
            if (error) *error = QStringLiteral("The OMT source could not be created.");
            return false;
        }
        return true;
    }
    void send(const CpuFrame &frame) override {
        const size_t bytes = size_t(frame.width) * size_t(frame.height) * 4;
        bgra.resize(bytes); // reallocates only when the picture's size changes
        PixelConvert::rgbaBottomUpToBgra(frame.rgba.data(), frame.width, frame.height, bgra.data());
        MediaFrame media{};
        media.Type = kVideo;
        media.Timestamp = frame.timestamp100ns;
        media.Codec = kBGRA;
        media.Width = frame.width;
        media.Height = frame.height;
        media.Stride = frame.width * 4;
        media.FrameRateN = kFrameRate;
        media.FrameRateD = 1;
        media.AspectRatio = frame.height ? float(frame.width) / float(frame.height) : 0.f;
        media.ColorSpace = kBT709;
        media.Data = bgra.data();
        media.DataLength = int(bytes);
        lib->send(sender, &media);
        if (lib->sendConnections && (++count % 30) == 1) receiverCount = lib->sendConnections(sender);
    }
private:
    const Library *lib = nullptr;
    void *sender = nullptr;
    std::vector<uint8_t> bgra;
    unsigned count = 0;
};
}

std::unique_ptr<CpuOutput> createOmtOutput() { return std::make_unique<OmtOutput>(); }
