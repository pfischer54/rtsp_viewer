#include <gst/gst.h>
#include <gtk/gtk.h>
#include <iostream>
#include <string>

struct AppData {
    GtkApplication *app = nullptr;
    GtkWindow *window = nullptr;
    GtkPicture *picture = nullptr;
    GtkButton *start_button = nullptr;
    GtkButton *stop_button = nullptr;

    GstElement *pipeline = nullptr;
    GstElement *sink = nullptr;
    std::string url = "rtsp://192.168.1.100:8554/quality_h264";
    gint latency_ms = 5;  // Test: reduced from 10ms
};

static void start_stream(AppData *app);
static void stop_stream(AppData *app);
static gboolean ensure_pipeline(AppData *app);
static void on_pad_added(GstElement *element, GstPad *pad, gpointer user_data);

static gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data) {
    (void)bus;
    AppData *app = static_cast<AppData*>(user_data);

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
            stop_stream(app);
            break;
        }
        case GST_MESSAGE_EOS:
            std::cout << "[INFO] End of stream\n";
            stop_stream(app);
            break;
        case GST_MESSAGE_STATE_CHANGED: {
            GstState old_state, new_state, pending;
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(app->pipeline)) {
                gst_message_parse_state_changed(msg, &old_state, &new_state, &pending);
                std::cout << "[STATE] "
                          << gst_element_state_get_name(old_state) << " -> "
                          << gst_element_state_get_name(new_state)
                          << " [pending: " << gst_element_state_get_name(pending) << "]\n";
            }
            break;
        }
        default:
            break;
    }

    return G_SOURCE_CONTINUE;
}

static void ensure_paintable(AppData *app) {
    if (!app->sink || !app->picture)
        return;

    GdkPaintable *paintable = nullptr;
    g_object_get(app->sink, "paintable", &paintable, NULL);
    if (paintable) {
        gtk_picture_set_paintable(app->picture, paintable);
        g_object_unref(paintable);
    }
}

static void on_sink_paintable_notify(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)object;
    (void)pspec;
    ensure_paintable(static_cast<AppData*>(user_data));
}

static gboolean ensure_pipeline(AppData *app) {
    if (app->pipeline)
        return TRUE;

    app->pipeline = gst_pipeline_new("rtsp-pipeline");
    GstElement *src = gst_element_factory_make("rtspsrc", "source");
    GstElement *depay = gst_element_factory_make("rtph264depay", "depay");
    GstElement *parse = gst_element_factory_make("h264parse", "parse");
    GstElement *dec = gst_element_factory_make("nvh264dec", "decoder");
    GstElement *convert = gst_element_factory_make("videoconvert", "convert");
    app->sink = gst_element_factory_make("gtk4paintablesink", "sink");

    if (!app->pipeline || !src || !depay || !parse || !dec || !convert || !app->sink) {
        std::cerr << "[ERROR] Failed to create pipeline elements. Ensure gstreamer1.0-gtk4 is installed.\n";
        if (app->pipeline) {
            gst_object_unref(app->pipeline);
            app->pipeline = nullptr;
        }
        return FALSE;
    }

    g_object_set(src,
                 "location", app->url.c_str(),
                 "latency", app->latency_ms,
                 "protocols", 0x00000001,  // Test: UDP only
                 "drop-on-latency", TRUE,  // Test: drop packets rather than buffer
                 "do-retransmission", FALSE,  // Test: disable retransmission requests
                 NULL);

    // Test: Configure decoder for minimum display delay
    g_object_set(dec,
                 "max-display-delay", 0,
                 NULL);

    // Test: Configure parser to avoid SPS/PPS reinjection overhead
    g_object_set(parse,
                 "config-interval", -1,
                 NULL);

    gst_bin_add_many(GST_BIN(app->pipeline), src, depay, parse, dec, convert, app->sink, NULL);

    if (!gst_element_link_many(depay, parse, dec, convert, app->sink, NULL)) {
        std::cerr << "[ERROR] Failed to link downstream elements.\n";
        gst_object_unref(app->pipeline);
        app->pipeline = nullptr;
        return FALSE;
    }

    g_signal_connect(src, "pad-added", G_CALLBACK(on_pad_added), depay);
    g_signal_connect(app->sink, "notify::paintable", G_CALLBACK(on_sink_paintable_notify), app);

    // Test: Set pipeline latency to minimum
    gst_pipeline_set_latency(GST_PIPELINE(app->pipeline), 0);

    GstBus *bus = gst_element_get_bus(app->pipeline);
    gst_bus_add_watch(bus, bus_cb, app);
    gst_object_unref(bus);

    ensure_paintable(app);
    return TRUE;
}

static inline bool widget_is_ready(GtkWidget *widget) {
    return widget && GTK_IS_WIDGET(widget);
}

static void start_stream(AppData *app) {
    if (!ensure_pipeline(app))
        return;

    ensure_paintable(app);

    GstStateChangeReturn ret = gst_element_set_state(app->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "[ERROR] Unable to set pipeline to PLAYING.\n";
        gst_element_set_state(app->pipeline, GST_STATE_NULL);
        return;
    }

    if (widget_is_ready(reinterpret_cast<GtkWidget*>(app->start_button))) {
        gtk_widget_set_sensitive(GTK_WIDGET(app->start_button), FALSE);
    }
    if (widget_is_ready(reinterpret_cast<GtkWidget*>(app->stop_button))) {
        gtk_widget_set_sensitive(GTK_WIDGET(app->stop_button), TRUE);
    }
}

static void stop_stream(AppData *app) {
    if (!app->pipeline)
        return;

    gst_element_set_state(app->pipeline, GST_STATE_NULL);

    if (widget_is_ready(reinterpret_cast<GtkWidget*>(app->start_button))) {
        gtk_widget_set_sensitive(GTK_WIDGET(app->start_button), TRUE);
    }
    if (widget_is_ready(reinterpret_cast<GtkWidget*>(app->stop_button))) {
        gtk_widget_set_sensitive(GTK_WIDGET(app->stop_button), FALSE);
    }
}

static void on_pad_added(GstElement *element, GstPad *pad, gpointer user_data) {
    (void)element;
    GstElement *depay = GST_ELEMENT(user_data);
    GstPad *sinkpad = gst_element_get_static_pad(depay, "sink");
    if (!sinkpad)
        return;

    if (gst_pad_is_linked(sinkpad)) {
        gst_object_unref(sinkpad);
        return;
    }

    if (gst_pad_link(pad, sinkpad) != GST_PAD_LINK_OK) {
        std::cerr << "[WARN] Failed to link dynamic RTSP pad.\n";
    }

    gst_object_unref(sinkpad);
}

static void on_start_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    start_stream(static_cast<AppData*>(user_data));
}

static void on_stop_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    stop_stream(static_cast<AppData*>(user_data));
}

static void on_app_shutdown(GApplication *gapp, gpointer user_data) {
    (void)gapp;
    stop_stream(static_cast<AppData*>(user_data));
}

static void on_app_activate(GApplication *gapp, gpointer user_data) {
    AppData *app = static_cast<AppData*>(user_data);

    app->window = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(gapp)));
    gtk_window_set_title(app->window, "RTSP Viewer");
    gtk_window_set_default_size(app->window, 1280, 720);

    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_window_set_child(app->window, root_box);

    app->picture = GTK_PICTURE(gtk_picture_new());
    gtk_widget_set_hexpand(GTK_WIDGET(app->picture), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(app->picture), TRUE);
    gtk_box_append(GTK_BOX(root_box), GTK_WIDGET(app->picture));

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(root_box), button_box);

    app->start_button = GTK_BUTTON(gtk_button_new_with_label("Start Stream"));
    app->stop_button = GTK_BUTTON(gtk_button_new_with_label("Stop Stream"));

    gtk_widget_set_sensitive(GTK_WIDGET(app->stop_button), FALSE);

    gtk_box_append(GTK_BOX(button_box), GTK_WIDGET(app->start_button));
    gtk_box_append(GTK_BOX(button_box), GTK_WIDGET(app->stop_button));

    g_signal_connect(app->start_button, "clicked", G_CALLBACK(on_start_clicked), app);
    g_signal_connect(app->stop_button, "clicked", G_CALLBACK(on_stop_clicked), app);

    gtk_widget_show(GTK_WIDGET(app->window));

    // Auto-start stream on launch
    start_stream(app);
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);

    AppData app{};

    // Parse command-line arguments
    if (argc > 1)
        app.url = argv[1];
    if (argc > 2)
        app.latency_ms = std::stoi(argv[2]);

    GtkApplication *gtk_app = gtk_application_new("com.example.rtsp_viewer", G_APPLICATION_FLAGS_NONE);
    app.app = gtk_app;

    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_app_activate), &app);
    g_signal_connect(gtk_app, "shutdown", G_CALLBACK(on_app_shutdown), &app);

    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);

    stop_stream(&app);

    if (app.pipeline) {
        gst_object_unref(app.pipeline);
        app.pipeline = nullptr;
    }

    g_object_unref(gtk_app);
    return status;
}
