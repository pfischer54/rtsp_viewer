// Build & run a GStreamer pipeline equivalent to:
// gst-launch-1.0 -e rtspsrc location=rtsp://192.168.1.100:8554/quality_h264 protocols=udp latency=30 ntp-sync=false do-retransmission=false ! \
//   rtph264depay ! h264parse config-interval=1 ! nvh264dec ! cudadownload ! video/x-raw,format=NV12 ! \
//   videoconvert ! video/x-raw,format=I420 ! queue leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! \
//   xvimagesink sync=false

#include <gst/gst.h>
#include <csignal>
#include <iostream>
#include <string>

static GMainLoop* loop = nullptr;

static gboolean bus_cb(GstBus* bus, GstMessage* msg, gpointer user_data) {
    (void)bus; (void)user_data;
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            std::cerr << "[ERROR] " << (err ? err->message : "unknown") << std::endl;
            if (dbg)   std::cerr << "[DEBUG] " << dbg << std::endl;
            if (err)   g_error_free(err);
            if (dbg)   g_free(dbg);
            if (loop)  g_main_loop_quit(loop);
            break;
        }
        case GST_MESSAGE_EOS:
            std::cout << "[INFO] EOS received.\n";
            if (loop) g_main_loop_quit(loop);
            break;
        default:
            break;
    }
    return TRUE;
}

static void on_sigint(int) {
    if (loop) g_main_loop_quit(loop);
}

int main(int argc, char* argv[]) {
    gst_init(&argc, &argv);

    std::string url = "rtsp://192.168.1.100:8554/quality_h264";
    if (argc > 1) url = argv[1];

    // Same pipeline as your gst-launch call
    std::string pipeline_desc =
        "rtspsrc location=" + url +
        " protocols=udp latency=0 ntp-sync=false do-retransmission=false ! "
        "rtph264depay ! "
        "h264parse config-interval=1 ! "
        "nvh264dec ! "
        "cudadownload ! "
        "video/x-raw,format=NV12 ! "
        "videoconvert ! "
        "video/x-raw,format=I420 ! "
        "queue leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! "
        "xvimagesink sync=false";

    GError* err = nullptr;
    GstElement* pipeline = gst_parse_launch(pipeline_desc.c_str(), &err);
    if (!pipeline) {
        std::cerr << "Failed to create pipeline: " << (err ? err->message : "unknown") << "\n";
        if (err) g_error_free(err);
        return 1;
    }
    if (err) { // warnings still possible
        std::cerr << "Pipeline warning: " << err->message << "\n";
        g_error_free(err);
    }

    GstBus* bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, bus_cb, nullptr);
    gst_object_unref(bus);

    std::signal(SIGINT, on_sigint);

    std::cout << "[INFO] Starting pipeline...\n";
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);

    std::cout << "[INFO] Stopping...\n";
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);
    return 0;
}
