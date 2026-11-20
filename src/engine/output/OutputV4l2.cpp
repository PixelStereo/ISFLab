// Virtual webcam output (Linux, v4l2loopback): the picture is converted to YUYV and written to a v4l2loopback device
// that webcam clients (browsers, video calls, OBS) open as a camera. Load the module with
//   sudo modprobe v4l2loopback exclusive_caps=1 card_label=ISFLab
// (exclusive_caps=1 makes the device visible to Chrome).
#include "VideoOutput.h"
#include "PixelConvert.h"
#include <QDir>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
// True when the open device is a v4l2loopback device that accepts frames from us
bool isLoopback(int fd) {
    v4l2_capability capability{};
    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) < 0) return false;
    const auto driver = QByteArray(reinterpret_cast<const char *>(capability.driver));
    const auto caps = capability.capabilities & V4L2_CAP_DEVICE_CAPS ? capability.device_caps : capability.capabilities;
    return driver.startsWith("v4l2 loopback") && (caps & V4L2_CAP_VIDEO_OUTPUT);
}

class V4l2Output final : public CpuOutput {
public:
    ~V4l2Output() override { if (fd >= 0) ::close(fd); }
    bool fixedSize() const override { return true; }
    QString description() const override { return device; }

    bool start(const OutputSettings &settings, QSize size, QString *error) override {
        QStringList candidates;
        if (!settings.v4l2Device.isEmpty()) candidates << settings.v4l2Device;
        else for (const auto &entry : QDir("/dev").entryList({"video*"}, QDir::System | QDir::Files, QDir::Name))
            candidates << "/dev/" + entry;
        for (const auto &path : candidates) {
            const int handle = ::open(path.toLocal8Bit().constData(), O_RDWR);
            if (handle < 0) continue;
            if (!isLoopback(handle)) { ::close(handle); continue; }
            fd = handle;
            device = path;
            break;
        }
        if (fd < 0) {
            if (error) *error = settings.v4l2Device.isEmpty()
                ? QStringLiteral("No v4l2loopback device found. Load the module: sudo modprobe v4l2loopback exclusive_caps=1 card_label=ISFLab")
                : settings.v4l2Device + QStringLiteral(" is not a v4l2loopback device that can be written to.");
            return false;
        }
        width = size.width() & ~1; // YUYV carries pixels in pairs
        height = size.height();
        v4l2_format format{};
        format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        format.fmt.pix.width = unsigned(width);
        format.fmt.pix.height = unsigned(height);
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        format.fmt.pix.field = V4L2_FIELD_NONE;
        format.fmt.pix.bytesperline = unsigned(width * 2);
        format.fmt.pix.sizeimage = unsigned(width * height * 2);
        format.fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
        if (width < 2 || ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
            if (error) *error = errno == EBUSY
                ? device + QStringLiteral(" is open in another application, which fixes its picture size. Close it, or use a fixed preview resolution.")
                : device + QStringLiteral(": the picture size ") + QString::number(width) + "x" + QString::number(height) +
                  QStringLiteral(" was refused (") + QString::fromLocal8Bit(std::strerror(errno)) + ")";
            ::close(fd);
            fd = -1;
            return false;
        }
        yuyv.resize(size_t(width) * size_t(height) * 2);
        return true;
    }

    void send(const CpuFrame &frame) override {
        if (fd < 0 || frame.width < width || frame.height != height) return;
        PixelConvert::rgbaBottomUpToYuyv(frame.rgba.data(), frame.width, frame.height, yuyv.data());
        // Rows are `width` pixels wide in YUYV; a frame wider by one pixel (odd) has been cropped by the conversion
        const auto written = ::write(fd, yuyv.data(), yuyv.size());
        (void)written; // no reader: v4l2loopback drops the frame
    }
private:
    int fd = -1, width = 0, height = 0;
    QString device;
    std::vector<uint8_t> yuyv;
};
}

std::unique_ptr<CpuOutput> createV4l2Output() { return std::make_unique<V4l2Output>(); }
