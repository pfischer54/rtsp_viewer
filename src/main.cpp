#include <gst/gst.h>
#include <gtk/gtk.h>
#include <iostream>
#include <string>

struct AppData {
    GtkApplication *app = nullptr;
    GtkWidget *window = nullptr;
    GtkWidget *start_button = nullptr;
    GtkWidget *stop_button = nullptr;
    GtkWidget *picture = nullptr;        // Where video will be shown

    GstElement *pipeline = nullptr;
    std::string url = "rtsp://192.168.1.100:8554/quality_h264";
    int latency_ms = 10; // desired rtspsrc latency (ms)
    bool force_hw = false; // if true, prefer hardware pipeline aggressively
};

// Forward declarations
static void start_stream(AppData *app);
static void stop_stream(AppData *app);
static gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data);
static gboolean ensure_pipeline(AppData *app);
static void on_start_clicked(GtkButton *button, gpointer user_data);
static void on_stop_clicked(GtkButton *button, gpointer user_data);
static void on_app_shutdown(GApplication *gapp, gpointer user_data);

// --- Simple GTK callbacks --------------------------------------------------

static void on_start_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppData *app = static_cast<AppData*>(user_data);
    start_stream(app);
}

static void on_stop_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppData *app = static_cast<AppData*>(user_data);
    stop_stream(app);
}

static void on_app_shutdown(GApplication *gapp, gpointer user_data) {
    (void)gapp;
    AppData *app = static_cast<AppData*>(user_data);
    stop_stream(app);
}

// --- GStreamer bus callback -------------------------------------------------

static gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data) {
    (void)bus;
    AppData *app = static_cast<AppData*>(user_data);

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err = nullptr;
            gchar *dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            std::cerr << "[ERROR] "
                      << (err ? err->message : "unknown") << "\n";
            if (dbg) {
                std::cerr << "[DEBUG] " << dbg << "\n";
                g_free(dbg);
            }
            if (err) g_error_free(err);

            // Stop pipeline on error
            stop_stream(app);
            break;
        }
        case GST_MESSAGE_EOS:
            std::cout << "[INFO] EOS received\n";
            stop_stream(app);
            break;

        default:
            break;
    }
    return TRUE; // Keep watching
}

// --- Create / ensure pipeline ----------------------------------------------

static gboolean ensure_pipeline(AppData *app) {
    if (app->pipeline)
        return TRUE;

    // Try hardware-accelerated pipeline first (NVDEC). Use gst_parse_launch for
    // a concise description and fallback if creation fails.
    std::string hw_desc =
        std::string("rtspsrc location=\"") + app->url + "\" "
        "protocols=udp latency=" + std::to_string(app->latency_ms) +
        " ntp-sync=false do-retransmission=false ! "
        "rtph264depay ! h264parse config-interval=1 ! "
        "nvh264dec ! cudadownload ! videoconvert ! video/x-raw,format=I420 ! "
        "queue leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! "
        "autovideosink sync=false";

    std::cout << "[INFO] Trying hardware pipeline:\n" << hw_desc << "\n";
    GError *err = nullptr;
    GstElement *pipeline = gst_parse_launch(hw_desc.c_str(), &err);
    if (pipeline) {
        if (err) { g_error_free(err); err = nullptr; }
        GstBus *bus = gst_element_get_bus(pipeline);
        gst_bus_add_watch(bus, bus_cb, app);
        gst_object_unref(bus);
        app->pipeline = pipeline;
        std::cout << "[INFO] Hardware pipeline created and selected.\n";
        return TRUE;
    }
    if (err) {
        std::cerr << "[WARN] Hardware pipeline creation failed: " << err->message << "\n";
        g_error_free(err);
        err = nullptr;
    } else {
        std::cerr << "[WARN] Hardware pipeline creation failed (unknown reason)\n";
    }

    // Fallback: use playbin with an auto sink selection
    std::cout << "[INFO] Falling back to playbin-based pipeline...\n";
    GstElement *playbin = gst_element_factory_make("playbin", "playbin");
    if (!playbin) {
        std::cerr << "[FATAL] Could not create playbin element\n";
        return FALSE;
    }

    const char *candidates[] = {"autovideosink", "xvimagesink", "ximagesink", "gtksink", NULL};
    GstElement *vsink = nullptr;
    for (const char **c = candidates; *c; ++c) {
        vsink = gst_element_factory_make(*c, "vsink");
        if (vsink) {
            std::cout << "[INFO] Using video sink: " << *c << "\n";
            break;
        }
    }
    if (!vsink) {
        std::cerr << "[FATAL] Could not create any video sink (no suitable element found)\n";
        gst_object_unref(playbin);
        return FALSE;
    }

    g_object_set(playbin, "uri", app->url.c_str(), NULL);
    g_object_set(playbin, "video-sink", vsink, NULL);

    GstBus *bus2 = gst_element_get_bus(playbin);
    gst_bus_add_watch(bus2, bus_cb, app);
    gst_object_unref(bus2);

    GdkPaintable *paintable = nullptr;
    g_object_get(vsink, "paintable", &paintable, NULL);
    if (paintable) {
        gtk_picture_set_paintable(GTK_PICTURE(app->picture), paintable);
        g_object_unref(paintable);
        gst_object_unref(vsink);
    } else {
        std::cout << "[INFO] Selected sink does not expose GdkPaintable; video will be shown in sink's own window.\n";
        gst_object_unref(vsink);
    }

    app->pipeline = playbin;
    return TRUE;
}

// --- Start / Stop handlers --------------------------------------------------

static void start_stream(AppData *app) {
    if (!ensure_pipeline(app)) {
        std::cerr << "[ERROR] Cannot start stream: pipeline create failed\n";
        return;
    }

    std::cout << "[INFO] Setting pipeline to PLAYING\n";
    GstStateChangeReturn ret = gst_element_set_state(app->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "[FATAL] Failed to set pipeline to PLAYING\n";
        gst_element_set_state(app->pipeline, GST_STATE_NULL);
        gst_object_unref(app->pipeline);
        app->pipeline = nullptr;
        return;
    }

    if (app->start_button) gtk_widget_set_sensitive(app->start_button, FALSE);
    if (app->stop_button) gtk_widget_set_sensitive(app->stop_button, TRUE);
}

static void stop_stream(AppData *app) {
    if (!app->pipeline)
        return;

    std::cout << "[INFO] Stopping pipeline\n";
    gst_element_set_state(app->pipeline, GST_STATE_NULL);
    gst_object_unref(app->pipeline);
    app->pipeline = nullptr;

    if (app->start_button) gtk_widget_set_sensitive(app->start_button, TRUE);
    if (app->stop_button) gtk_widget_set_sensitive(app->stop_button, FALSE);
}
// --- Application activate ---------------------------------------------------

static void on_app_activate(GApplication *gapp, gpointer user_data) {
    AppData *app = static_cast<AppData*>(user_data);

    app->window = gtk_application_window_new(GTK_APPLICATION(gapp));
    gtk_window_set_title(GTK_WINDOW(app->window), "RTSP Viewer (GTK4 + GStreamer)");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1280, 720);

    // Vertical layout: video on top, buttons below
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_window_set_child(GTK_WINDOW(app->window), vbox);

    // Picture for video
    app->picture = gtk_picture_new();
    gtk_widget_set_hexpand(app->picture, TRUE);
    gtk_widget_set_vexpand(app->picture, TRUE);
    gtk_box_append(GTK_BOX(vbox), app->picture);

    // Buttons row
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(vbox), hbox);

    app->start_button = gtk_button_new_with_label("Start Stream");
    app->stop_button  = gtk_button_new_with_label("Stop Stream");

    gtk_box_append(GTK_BOX(hbox), app->start_button);
    gtk_box_append(GTK_BOX(hbox), app->stop_button);

    g_signal_connect(app->start_button, "clicked",
                     G_CALLBACK(on_start_clicked), app);
    g_signal_connect(app->stop_button, "clicked",
                     G_CALLBACK(on_stop_clicked), app);

    // Initially: stream stopped
    gtk_widget_set_sensitive(app->stop_button, FALSE);

    gtk_widget_show(app->window);

    // Auto-start stream on activate so the window shows video immediately.
    // If this fails, the user can still press Start.
    start_stream(app);
}

// --- main -------------------------------------------------------------------

int main(int argc, char *argv[]) {
    // Init GStreamer first so it can hook into GLib/GTK
    gst_init(&argc, &argv);

    AppData appdata;

    if (argc > 1) {
        appdata.url = argv[1];
    }
    if (argc > 2) {
        appdata.latency_ms = std::stoi(argv[2]);
    }
    if (argc > 3) {
        std::string flag = argv[3];
        if (flag == "hw" || flag == "--hw") appdata.force_hw = true;
    }

    GtkApplication *app =
        gtk_application_new("com.example.rtsp_gtk4", G_APPLICATION_FLAGS_NONE);
    appdata.app = app;

    g_signal_connect(app, "activate", G_CALLBACK(on_app_activate), &appdata);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_app_shutdown), &appdata);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    // Ensure cleanup (in case shutdown signal wasn't fired as expected)
    stop_stream(&appdata);

    g_object_unref(app);
    return status;
}
