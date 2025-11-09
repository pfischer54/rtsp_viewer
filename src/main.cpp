#include <gst/gst.h>
#include <gtk/gtk.h>
#include <gst/video/videooverlay.h>
#include <iostream>
#include <string>

struct AppData {
    GtkApplication *app = nullptr;
    GtkWidget *window = nullptr;
    GtkWidget *start_button = nullptr;
    GtkWidget *stop_button = nullptr;
    GtkWidget *video = nullptr;        // Drawing area for video display

    GstElement *pipeline = nullptr;
    GstElement *sink = nullptr;        // Keep sink reference for GL context setup
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
static void on_pad_added(GstElement *element, GstPad *pad, gpointer data);

// Handle window-id events for embedding video
static GstBusSyncReply bus_sync_handler(GstBus *bus, GstMessage *message, gpointer user_data) {
    if (!gst_is_video_overlay_prepare_window_handle_message(message))
        return GST_BUS_PASS;

    AppData *app = static_cast<AppData*>(user_data);
    GtkRoot *root = gtk_widget_get_root(app->video);
    if (!root) return GST_BUS_DROP;

    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(root));
    if (!surface) return GST_BUS_DROP;

    guintptr window_handle = (guintptr)surface;
    gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(GST_MESSAGE_SRC(message)), window_handle);

    gst_message_unref(message);
    return GST_BUS_DROP;
}

// Pad added handler
static void on_pad_added(GstElement *element, GstPad *pad, gpointer data) {
    GstCaps *caps = gst_pad_get_current_caps(pad);
    GstStructure *str = gst_caps_get_structure(caps, 0);
    const gchar *name = gst_structure_get_name(str);

    if (!g_str_has_prefix(name, "video/")) {
        gst_caps_unref(caps);
        return;
    }

    GstElement *depay = GST_ELEMENT(data);
    GstPad *sinkpad = gst_element_get_static_pad(depay, "sink");
    
    gst_pad_link(pad, sinkpad);
    gst_object_unref(sinkpad);
    gst_caps_unref(caps);
}

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
            if (err) {
                std::cerr << "[ERROR Details] Domain: " << err->domain 
                         << ", Code: " << err->code << "\n";
                g_error_free(err);
            }
            // Stop pipeline on error
            stop_stream(app);
            break;
        }
        case GST_MESSAGE_EOS:
            std::cout << "[INFO] EOS received\n";
            stop_stream(app);
            break;
        case GST_MESSAGE_STATE_CHANGED: {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
            std::cout << "[STATE] " << gst_element_state_get_name(old_state) 
                     << " -> " << gst_element_state_get_name(new_state) 
                     << " [pending: " << gst_element_state_get_name(pending_state) << "]\n";
            break;
        }
        case GST_MESSAGE_WARNING: {
            GError *err = nullptr;
            gchar *dbg = nullptr;
            gst_message_parse_warning(msg, &err, &dbg);
            std::cout << "[WARNING] " << (err ? err->message : "unknown") << "\n";
            if (dbg) {
                std::cout << "[DEBUG] " << dbg << "\n";
                g_free(dbg);
            }
            if (err) g_error_free(err);
            break;
        }
        default:
            break;
    }
    return TRUE; // Keep watching
}

// --- Create / ensure pipeline ----------------------------------------------

static gboolean ensure_pipeline(AppData *app) {
    if (app->pipeline)
        return TRUE;

    std::cout << "[INFO] Creating pipeline for URL: " << app->url << "\n";
    
    // Create pipeline elements
    app->pipeline = gst_pipeline_new("pipeline");
    GstElement *src = gst_element_factory_make("rtspsrc", "source");
    GstElement *depay = gst_element_factory_make("rtph264depay", "depay");
    GstElement *parse = gst_element_factory_make("h264parse", "parse");
    GstElement *dec = gst_element_factory_make("avdec_h264", "dec");
    GstElement *conv = gst_element_factory_make("videoconvert", "conv");
    app->sink = gst_element_factory_make("waylandsink", "sink");

    if (!app->pipeline || !src || !depay || !parse || !dec || !conv || !app->sink) {
        std::cerr << "[ERROR] One or more elements could not be created.\n";
        return FALSE;
    }

    // Configure waylandsink
    g_object_set(app->sink,
                "enable-last-sample", FALSE,  // Don't keep the last frame
                "sync", FALSE,                // Don't sync to clock
                NULL);

    // Configure source with more options
    g_object_set(src, 
                "location", app->url.c_str(),
                "latency", app->latency_ms,
                "protocols", 0x00000003,  // Try both TCP (2) and UDP (1)
                "retry", 3,               // Number of retries
                "timeout", 5000000,       // 5 second timeout
                "udp-buffer-size", 2097152, // 2MB buffer
                "port-range", "5000-5010",  // UDP port range
                NULL);

    // Configure GL sink
    g_object_set(app->sink,
                "sync", FALSE,  // Avoid blocking on the sink
                NULL);

    // Add elements to pipeline
    gst_bin_add_many(GST_BIN(app->pipeline), src, depay, parse, dec, conv, app->sink, NULL);

    // Link elements (except rtspsrc which will be linked when pad becomes available)
    if (!gst_element_link_many(depay, parse, dec, conv, app->sink, NULL)) {
        std::cerr << "[ERROR] Elements could not be linked.\n";
        gst_object_unref(app->pipeline);
        app->pipeline = nullptr;
        return FALSE;
    }

    // Connect to rtspsrc's pad-added signal
    g_signal_connect(src, "pad-added", G_CALLBACK(on_pad_added), depay);

    // Set up bus watchers
    GstBus *bus = gst_element_get_bus(app->pipeline);
    gst_bus_set_sync_handler(bus, bus_sync_handler, app, NULL);
    gst_bus_add_watch(bus, bus_cb, app);
    gst_object_unref(bus);

    return TRUE;

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

    // Create main window
    app->window = gtk_application_window_new(GTK_APPLICATION(gapp));
    gtk_window_set_title(GTK_WINDOW(app->window), "RTSP Viewer (GTK4 + GStreamer)");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1280, 720);

    // Main vertical layout
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_window_set_child(GTK_WINDOW(app->window), vbox);

    // Create drawing area for video with GL capability
    app->video = gtk_drawing_area_new();
    gtk_widget_set_hexpand(app->video, TRUE);
    gtk_widget_set_vexpand(app->video, TRUE);
    gtk_box_append(GTK_BOX(vbox), app->video);
    
    // Enable GL rendering with a simple draw function
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->video), 
        [](GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
            // Clear any cairo drawing - GStreamer handles the video
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_paint(cr);
        }, nullptr, nullptr);

    // Request minimum size for video area
    gtk_widget_set_size_request(app->video, 320, 240);

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

static int on_command_line(GApplication *gapp, GApplicationCommandLine *cmdline, gpointer user_data) {
    AppData *app = static_cast<AppData*>(user_data);
    
    gchar **argv;
    gint argc;
    
    argv = g_application_command_line_get_arguments(cmdline, &argc);
    
    if (argc > 1) app->url = argv[1];
    if (argc > 2) app->latency_ms = std::stoi(argv[2]);
    if (argc > 3) {
        std::string flag = argv[3];
        if (flag == "hw" || flag == "--hw") app->force_hw = true;
    }
    
    g_strfreev(argv);
    g_application_activate(gapp);
    
    return 0;
}

int main(int argc, char *argv[]) {
    // Enable debug output for video sink
    gst_debug_set_default_threshold(GST_LEVEL_WARNING);
    gst_debug_set_threshold_for_name("waylandsink", GST_LEVEL_DEBUG);
    
    // Initialize GStreamer first
    gst_init(&argc, &argv);

    AppData appdata;

    // Create application with command line handling
    GtkApplication *app = gtk_application_new(
        "com.example.rtsp_gtk4", 
        G_APPLICATION_HANDLES_COMMAND_LINE
    );

    g_signal_connect(app, "command-line", G_CALLBACK(on_command_line), &appdata);
    appdata.app = app;

    g_signal_connect(app, "activate", G_CALLBACK(on_app_activate), &appdata);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_app_shutdown), &appdata);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    // Ensure cleanup (in case shutdown signal wasn't fired as expected)
    stop_stream(&appdata);

    g_object_unref(app);
    return status;
}
