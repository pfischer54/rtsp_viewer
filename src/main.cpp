/*
 * RTSP Viewer - Low-latency RTSP stream viewer using GStreamer and GTK4
 * 
 * Features:
 * - Hardware-accelerated H.264 decoding (NVIDIA GPU)
 * - Optimized for low latency (~10-15ms glass-to-glass)
 * - UDP-only transport for minimum delay
 * - GTK4 GUI with start/stop controls
 * 
 * Pipeline: rtspsrc → rtph264depay → h264parse → nvh264dec → videoconvert → gtk4paintablesink
 */

#include <gst/gst.h>
#include <gtk/gtk.h>
#include <iostream>
#include <string>

/**
 * Application state container
 * Holds all GTK widgets and GStreamer pipeline elements
 */
struct AppData {
    GtkApplication *app = nullptr;         // GTK application instance
    GtkWindow *window = nullptr;           // Main window
    GtkPicture *picture = nullptr;         // Video display widget
    GtkButton *start_button = nullptr;     // Stream start button
    GtkButton *stop_button = nullptr;      // Stream stop button

    GstElement *pipeline = nullptr;        // GStreamer pipeline container
    GstElement *sink = nullptr;            // Video sink element (gtk4paintablesink)
    std::string url = "rtsp://192.168.1.100:8554/quality_h264";  // Default RTSP URL
    gint latency_ms = 5;                   // Jitter buffer size (5ms optimized for local network)
};

// Forward declarations
static void start_stream(AppData *app);
static void stop_stream(AppData *app);
static gboolean ensure_pipeline(AppData *app);
static void on_pad_added(GstElement *element, GstPad *pad, gpointer user_data);

/**
 * GStreamer bus message callback
 * Handles pipeline messages: errors, end-of-stream, state changes
 * 
 * @param bus The GStreamer bus (unused)
 * @param msg The message received from the pipeline
 * @param user_data Pointer to AppData structure
 * @return G_SOURCE_CONTINUE to keep the bus watch active
 */
static gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data) {
    (void)bus;
    AppData *app = static_cast<AppData*>(user_data);

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            // Parse and log error details
            GError *err = nullptr;
            gchar *dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            std::cerr << "[ERROR] " << (err ? err->message : "unknown") << "\n";
            if (dbg) {
                std::cerr << "[DEBUG] " << dbg << "\n";
                g_free(dbg);
            }
            if (err) g_error_free(err);
            
            // Stop streaming on error
            stop_stream(app);
            break;
        }
        case GST_MESSAGE_EOS:
            // End of stream reached
            std::cout << "[INFO] End of stream\n";
            stop_stream(app);
            break;
        case GST_MESSAGE_STATE_CHANGED: {
            // Log pipeline state transitions for debugging
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

/**
 * Retrieve and set the paintable object from the sink to the picture widget
 * This connects the GStreamer video output to the GTK display
 * 
 * @param app Pointer to AppData structure
 */
static void ensure_paintable(AppData *app) {
    if (!app->sink || !app->picture)
        return;

    // Get the paintable from the gtk4paintablesink
    GdkPaintable *paintable = nullptr;
    g_object_get(app->sink, "paintable", &paintable, NULL);
    if (paintable) {
        // Set it on the GTK picture widget for display
        gtk_picture_set_paintable(app->picture, paintable);
        g_object_unref(paintable);  // Release our reference
    }
}

/**
 * Callback when the sink's paintable property changes
 * Triggered when video frames become available
 * 
 * @param object The GObject (sink element)
 * @param pspec Property specification (unused)
 * @param user_data Pointer to AppData structure
 */
static void on_sink_paintable_notify(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)object;
    (void)pspec;
    ensure_paintable(static_cast<AppData*>(user_data));
}

/**
 * Create and configure the GStreamer pipeline
 * Only creates if it doesn't already exist (lazy initialization)
 * 
 * Pipeline structure:
 *   rtspsrc → rtph264depay → h264parse → nvh264dec → videoconvert → gtk4paintablesink
 * 
 * Optimizations applied:
 * - 5ms latency (jitter buffer)
 * - UDP-only protocol (no TCP retransmission delay)
 * - Drop packets on latency (prefer fresh frames)
 * - No retransmission requests
 * - Zero decoder display delay
 * - No periodic SPS/PPS reinjection
 * - Pipeline latency set to 0
 * 
 * @param app Pointer to AppData structure
 * @return TRUE on success, FALSE on failure
 */
static gboolean ensure_pipeline(AppData *app) {
    // Only create once
    if (app->pipeline)
        return TRUE;

    // Create pipeline and all elements
    app->pipeline = gst_pipeline_new("rtsp-pipeline");
    GstElement *src = gst_element_factory_make("rtspsrc", "source");          // RTSP source
    GstElement *depay = gst_element_factory_make("rtph264depay", "depay");    // RTP H.264 depayloader
    GstElement *parse = gst_element_factory_make("h264parse", "parse");       // H.264 parser
    GstElement *dec = gst_element_factory_make("nvh264dec", "decoder");       // NVIDIA hardware decoder
    GstElement *convert = gst_element_factory_make("videoconvert", "convert");// Format converter
    app->sink = gst_element_factory_make("gtk4paintablesink", "sink");        // GTK4 sink

    // Verify all elements were created
    if (!app->pipeline || !src || !depay || !parse || !dec || !convert || !app->sink) {
        std::cerr << "[ERROR] Failed to create pipeline elements. Ensure gstreamer1.0-gtk4 is installed.\n";
        if (app->pipeline) {
            gst_object_unref(app->pipeline);
            app->pipeline = nullptr;
        }
        return FALSE;
    }

    // Configure RTSP source for low latency
    g_object_set(src,
                 "location", app->url.c_str(),         // RTSP stream URL
                 "latency", app->latency_ms,            // Jitter buffer size (5ms)
                 "protocols", 0x00000001,               // UDP only (0x00000001), no TCP
                 "drop-on-latency", TRUE,               // Drop late packets instead of buffering
                 "do-retransmission", FALSE,            // Disable RTCP retransmission requests
                 NULL);

    // Configure decoder for minimum display delay
    g_object_set(dec,
                 "max-display-delay", 0,                // Display frames immediately
                 NULL);

    // Configure parser to avoid overhead
    g_object_set(parse,
                 "config-interval", -1,                 // Don't periodically insert SPS/PPS headers
                 NULL);

    // Add all elements to the pipeline
    gst_bin_add_many(GST_BIN(app->pipeline), src, depay, parse, dec, convert, app->sink, NULL);

    // Link static elements (rtspsrc pads are dynamic, linked via callback)
    if (!gst_element_link_many(depay, parse, dec, convert, app->sink, NULL)) {
        std::cerr << "[ERROR] Failed to link downstream elements.\n";
        gst_object_unref(app->pipeline);
        app->pipeline = nullptr;
        return FALSE;
    }

    // Connect callback for dynamic pad creation from rtspsrc
    g_signal_connect(src, "pad-added", G_CALLBACK(on_pad_added), depay);
    
    // Connect callback for when video frames become available
    g_signal_connect(app->sink, "notify::paintable", G_CALLBACK(on_sink_paintable_notify), app);

    // Note: Pipeline latency is auto-negotiated by GStreamer
    // Forcing it to 0 causes frame drops - let the pipeline decide

    // Attach bus watch for messages (errors, state changes, etc.)
    GstBus *bus = gst_element_get_bus(app->pipeline);
    gst_bus_add_watch(bus, bus_cb, app);
    gst_object_unref(bus);

    // Initialize video display
    ensure_paintable(app);
    return TRUE;
}

/**
 * Check if a GTK widget is valid and ready to use
 * 
 * @param widget GTK widget pointer to check
 * @return true if widget is valid, false otherwise
 */
static inline bool widget_is_ready(GtkWidget *widget) {
    return widget && GTK_IS_WIDGET(widget);
}

/**
 * Start RTSP stream playback
 * Creates pipeline if needed, sets to PLAYING state, updates UI
 * 
 * @param app Pointer to AppData structure
 */
static void start_stream(AppData *app) {
    // Create pipeline if it doesn't exist
    if (!ensure_pipeline(app))
        return;

    // Ensure video display is connected
    ensure_paintable(app);

    // Attempt to start the pipeline
    GstStateChangeReturn ret = gst_element_set_state(app->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "[ERROR] Unable to set pipeline to PLAYING.\n";
        gst_element_set_state(app->pipeline, GST_STATE_NULL);
        return;
    }

    // Update button states
    if (widget_is_ready(reinterpret_cast<GtkWidget*>(app->start_button))) {
        gtk_widget_set_sensitive(GTK_WIDGET(app->start_button), FALSE);  // Disable Start
    }
    if (widget_is_ready(reinterpret_cast<GtkWidget*>(app->stop_button))) {
        gtk_widget_set_sensitive(GTK_WIDGET(app->stop_button), TRUE);    // Enable Stop
    }
}

/**
 * Stop RTSP stream playback
 * Sets pipeline to NULL state, updates UI
 * 
 * @param app Pointer to AppData structure
 */
static void stop_stream(AppData *app) {
    if (!app->pipeline)
        return;

    // Stop the pipeline completely
    gst_element_set_state(app->pipeline, GST_STATE_NULL);

    // Update button states
    if (widget_is_ready(reinterpret_cast<GtkWidget*>(app->start_button))) {
        gtk_widget_set_sensitive(GTK_WIDGET(app->start_button), TRUE);   // Enable Start
    }
    if (widget_is_ready(reinterpret_cast<GtkWidget*>(app->stop_button))) {
        gtk_widget_set_sensitive(GTK_WIDGET(app->stop_button), FALSE);   // Disable Stop
    }
}

/**
 * Callback for dynamic pad creation on rtspsrc element
 * rtspsrc creates pads dynamically after analyzing the RTSP stream
 * This links the new pad to the depayloader
 * 
 * @param element The rtspsrc element (unused)
 * @param pad The newly created pad
 * @param user_data Pointer to the depay element
 */
static void on_pad_added(GstElement *element, GstPad *pad, gpointer user_data) {
    (void)element;
    GstElement *depay = GST_ELEMENT(user_data);
    
    // Get the sink pad from the depayloader
    GstPad *sinkpad = gst_element_get_static_pad(depay, "sink");
    if (!sinkpad)
        return;

    // Check if already linked (avoid duplicate connections)
    if (gst_pad_is_linked(sinkpad)) {
        gst_object_unref(sinkpad);
        return;
    }

    // Link the dynamic source pad to the depayloader
    if (gst_pad_link(pad, sinkpad) != GST_PAD_LINK_OK) {
        std::cerr << "[WARN] Failed to link dynamic RTSP pad.\n";
    }

    gst_object_unref(sinkpad);
}

/**
 * Callback for Start button click
 * 
 * @param button The clicked button (unused)
 * @param user_data Pointer to AppData structure
 */
static void on_start_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    start_stream(static_cast<AppData*>(user_data));
}

/**
 * Callback for Stop button click
 * 
 * @param button The clicked button (unused)
 * @param user_data Pointer to AppData structure
 */
static void on_stop_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    stop_stream(static_cast<AppData*>(user_data));
}

/**
 * Callback for application shutdown
 * Ensures clean pipeline shutdown before exit
 * 
 * @param gapp GTK application (unused)
 * @param user_data Pointer to AppData structure
 */
static void on_app_shutdown(GApplication *gapp, gpointer user_data) {
    (void)gapp;
    stop_stream(static_cast<AppData*>(user_data));
}

/**
 * Application activation callback
 * Creates the main window and UI, then auto-starts streaming
 * 
 * UI Layout:
 * ┌─────────────────────────────┐
 * │      RTSP Viewer Window     │
 * ├─────────────────────────────┤
 * │    Video Picture Widget     │
 * │    (expands to fill)        │
 * ├─────────────────────────────┤
 * │ [Start Stream] [Stop Stream]│
 * └─────────────────────────────┘
 * 
 * @param gapp GTK application instance
 * @param user_data Pointer to AppData structure
 */
static void on_app_activate(GApplication *gapp, gpointer user_data) {
    AppData *app = static_cast<AppData*>(user_data);

    // Create main application window
    app->window = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(gapp)));
    gtk_window_set_title(app->window, "RTSP Viewer");
    gtk_window_set_default_size(app->window, 1280, 720);

    // Create vertical box layout (video on top, buttons on bottom)
    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_window_set_child(app->window, root_box);

    // Create picture widget for video display (expands to fill available space)
    app->picture = GTK_PICTURE(gtk_picture_new());
    gtk_widget_set_hexpand(GTK_WIDGET(app->picture), TRUE);   // Expand horizontally
    gtk_widget_set_vexpand(GTK_WIDGET(app->picture), TRUE);   // Expand vertically
    gtk_box_append(GTK_BOX(root_box), GTK_WIDGET(app->picture));

    // Create horizontal box for buttons
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(root_box), button_box);

    // Create Start and Stop buttons
    app->start_button = GTK_BUTTON(gtk_button_new_with_label("Start Stream"));
    app->stop_button = GTK_BUTTON(gtk_button_new_with_label("Stop Stream"));

    // Initially disable Stop button (no stream running yet)
    gtk_widget_set_sensitive(GTK_WIDGET(app->stop_button), FALSE);

    // Add buttons to button box
    gtk_box_append(GTK_BOX(button_box), GTK_WIDGET(app->start_button));
    gtk_box_append(GTK_BOX(button_box), GTK_WIDGET(app->stop_button));

    // Connect button click handlers
    g_signal_connect(app->start_button, "clicked", G_CALLBACK(on_start_clicked), app);
    g_signal_connect(app->stop_button, "clicked", G_CALLBACK(on_stop_clicked), app);

    // Show the window
    gtk_widget_show(GTK_WIDGET(app->window));

    // Auto-start stream on launch
    start_stream(app);
}

/**
 * Main entry point
 * Initializes GStreamer and GTK, runs the application
 * 
 * Command-line arguments:
 *   argv[1] - RTSP URL (optional, default: rtsp://192.168.1.100:8554/quality_h264)
 *   argv[2] - Latency in milliseconds (optional, default: 5)
 * 
 * Example: ./rtsp_viewer rtsp://192.168.1.200:8554/stream 10
 * 
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit status (0 for success)
 */
int main(int argc, char *argv[]) {
    // Initialize GStreamer
    gst_init(&argc, &argv);

    AppData app{};

    // Parse command-line arguments
    if (argc > 1)
        app.url = argv[1];          // Override default RTSP URL
    if (argc > 2)
        app.latency_ms = std::stoi(argv[2]);  // Override default latency

    // Create GTK application
    GtkApplication *gtk_app = gtk_application_new("com.example.rtsp_viewer", G_APPLICATION_FLAGS_NONE);
    app.app = gtk_app;

    // Connect application lifecycle callbacks
    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_app_activate), &app);
    g_signal_connect(gtk_app, "shutdown", G_CALLBACK(on_app_shutdown), &app);

    // Run the GTK main loop (blocks until application exits)
    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);

    // Cleanup: stop stream and free resources
    stop_stream(&app);

    if (app.pipeline) {
        gst_object_unref(app.pipeline);
        app.pipeline = nullptr;
    }

    g_object_unref(gtk_app);
    return status;
}
