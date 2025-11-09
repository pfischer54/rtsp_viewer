// Minimal GStreamer RTSP viewer with hardware decoding (nvh264dec)
#include <gst/gst.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

std::atomic<bool> running{true};

static gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err = nullptr;
            gchar *dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            std::cerr << "[ERROR] " << (err ? err->message : "unknown") << "\n";
            if (dbg) {
                std::cerr << "[DEBUG] " << dbg << "\n";
                g_free(dbg);
            }
            if (err) g_error_free(err);
            running = false;
            break;
        }
        case GST_MESSAGE_EOS:
            std::cout << "[INFO] End of stream\n";
            running = false;
            break;
        default:
            break;
    }
    return TRUE;
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);

    std::string url = "rtsp://192.168.1.100:8554/quality_h264";
    if (argc > 1) url = argv[1];

    std::string pipeline_desc =
        "rtspsrc location=" + url +
        " latency=10 ! rtph264depay ! h264parse ! nvh264dec ! videoconvert ! autovideosink sync=false";

    GError *err = nullptr;
    GstElement *pipeline = gst_parse_launch(pipeline_desc.c_str(), &err);
    if (!pipeline) {
        std::cerr << "[FATAL] Failed to create pipeline: "
                  << (err ? err->message : "unknown") << "\n";
        if (err) g_error_free(err);
        return 1;
    }

    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, bus_cb, nullptr);
    gst_object_unref(bus);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    std::cout << "Streaming from: " << url << "\n";
    std::cout << "Press Ctrl+C to stop.\n";

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    std::cout << "Stopped.\n";
    return 0;
}
