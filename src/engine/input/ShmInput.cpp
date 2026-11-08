#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include "LiveInput.h"
#include <QFile>

namespace {
class ShmInput final : public LiveInput {
public:
    ShmInput(const QString &socket, QSize size) {
        if (socket.isEmpty() || !size.isValid()) return;
        static const bool initialized = gst_init_check(nullptr, nullptr, nullptr);
        if (!initialized) { problem = "Could not initialize GStreamer."; return; }
        pipeline = gst_pipeline_new(nullptr);
        auto *source = gst_element_factory_make("shmsrc", nullptr);
        sink = gst_element_factory_make("appsink", nullptr);
        if (!source || !sink) {
            if (source) gst_object_unref(source);
            if (sink) gst_object_unref(sink);
            sink = nullptr;
            problem = "Install the GStreamer shmsrc and appsink plugins.";
            return;
        }
        const auto path = QFile::encodeName(socket);
        g_object_set(source, "socket-path", path.constData(), "is-live", TRUE, "do-timestamp", TRUE, nullptr);
        auto *caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "RGBA",
            "width", G_TYPE_INT, size.width(), "height", G_TYPE_INT, size.height(), nullptr);
        g_object_set(sink, "caps", caps, "sync", FALSE, "max-buffers", 1u, "drop", TRUE, "enable-last-sample", FALSE, nullptr);
        gst_caps_unref(caps);
        gst_bin_add_many(GST_BIN(pipeline), source, sink, nullptr);
        if (!gst_element_link(source, sink) || gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
            problem = "Cannot start the GStreamer shared-memory receiver.";
    }
    ~ShmInput() override {
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
        }
    }
    QString error() const override { return problem; }

    QImage frame(unsigned int) override {
        if (!pipeline || !sink || !problem.isEmpty()) return {};
        auto *bus = gst_element_get_bus(pipeline);
        if (auto *message = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR)) {
            GError *details = nullptr;
            gst_message_parse_error(message, &details, nullptr);
            if (details) { problem = QString::fromUtf8(details->message); g_error_free(details); }
            gst_message_unref(message);
        }
        gst_object_unref(bus);
        auto *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 0);
        if (!sample) return {};
        GstVideoInfo info;
        GstVideoFrame mapped;
        QImage result;
        if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample)) &&
            gst_video_frame_map(&mapped, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) {
            result = QImage(static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&mapped, 0)),
                            GST_VIDEO_FRAME_WIDTH(&mapped), GST_VIDEO_FRAME_HEIGHT(&mapped),
                            GST_VIDEO_FRAME_PLANE_STRIDE(&mapped, 0), QImage::Format_RGBA8888).copy();
            gst_video_frame_unmap(&mapped);
        }
        gst_sample_unref(sample);
        return result;
    }
private:
    GstElement *pipeline = nullptr, *sink = nullptr;
    QString problem;
};
}

std::unique_ptr<LiveInput> createLiveInput(const QString &sender, QSize dimensions) {
    return std::make_unique<ShmInput>(sender, dimensions);
}
