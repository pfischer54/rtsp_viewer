# RTSP Viewer - Detailed Function Descriptions

## Overview
This document provides comprehensive descriptions of all functions in the RTSP Viewer application, which is a GTK4-based RTSP stream viewer using GStreamer for media handling and NVIDIA hardware acceleration.

---

## Data Structure

### `AppData`
Holds application state including GTK window components and GStreamer pipeline elements.

**Members:**
- `GtkApplication *app` - GTK application instance
- `GtkWindow *window` - Main application window
- `GtkPicture *picture` - Widget for displaying video frames
- `GtkButton *start_button` - Button to start streaming
- `GtkButton *stop_button` - Button to stop streaming
- `GstElement *pipeline` - GStreamer pipeline container
- `GstElement *sink` - gtk4paintablesink element
- `std::string url` - RTSP stream URL (default: `rtsp://192.168.1.100:8554/quality_h264`)
- `gint latency_ms` - Stream latency in milliseconds (default: 10)

---

## GStreamer Pipeline & Callback Functions

### `bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data)`
GStreamer bus message callback that handles pipeline events and messages.

**Parameters:**
- `bus` - GStreamer bus (unused)
- `msg` - Message from the pipeline
- `user_data` - Pointer to AppData structure

**Functionality:**
- **ERROR messages**: Parses error details using `gst_message_parse_error()`, logs error and debug info to stderr, then calls `stop_stream()` to halt playback
- **EOS messages**: Handles end-of-stream events by logging info and stopping the stream
- **STATE_CHANGED messages**: Logs pipeline state transitions (NULL → READY → PAUSED → PLAYING) when the source is the pipeline itself

**Returns:** `G_SOURCE_CONTINUE` to keep the bus watch active

---

### `ensure_paintable(AppData *app)`
Retrieves the paintable object from the gtk4paintablesink and sets it on the GTK picture widget to display video frames.

**Parameters:**
- `app` - Pointer to AppData structure

**Functionality:**
- Checks if sink and picture widgets are valid
- Gets the "paintable" property from the sink element
- Sets the paintable on the GTK picture widget for video display
- Unreferences the paintable after setting it

---

### `on_sink_paintable_notify(GObject *object, GParamSpec *pspec, gpointer user_data)`
GObject property notification callback triggered when the sink's paintable property changes.

**Parameters:**
- `object` - The GObject (unused)
- `pspec` - Property specification (unused)
- `user_data` - Pointer to AppData structure

**Functionality:**
- Calls `ensure_paintable()` to update the video display when the sink's paintable becomes available

---

### `ensure_pipeline(AppData *app)`
Creates and configures the complete GStreamer pipeline if it doesn't already exist.

**Parameters:**
- `app` - Pointer to AppData structure

**Pipeline Elements:**
1. **rtspsrc** - RTSP source element for network streaming
2. **rtph264depay** - RTP H.264 depayloader
3. **h264parse** - H.264 stream parser
4. **nvh264dec** - NVIDIA hardware H.264 decoder
5. **videoconvert** - Video format converter
6. **gtk4paintablesink** - GTK4 paintable sink for display

**Configuration:**
- Sets rtspsrc location to the configured URL
- Sets latency to configured value
- Configures protocols to allow both UDP (0x01) and TCP (0x02)
- Links static elements: depay → parse → decoder → convert → sink
- Connects dynamic pad-added signal for rtspsrc
- Connects paintable notification signal
- Attaches bus watch for message handling

**Returns:** 
- `TRUE` on successful pipeline creation
- `FALSE` on failure (with error logging)

---

### `on_pad_added(GstElement *element, GstPad *pad, gpointer user_data)`
Dynamic pad callback for the rtspsrc element, triggered when output pads are created.

**Parameters:**
- `element` - Source element (unused)
- `pad` - Newly created pad from rtspsrc
- `user_data` - Pointer to depay element

**Functionality:**
- Retrieves the sink pad from the depay element
- Checks if the pad is already linked to avoid duplicate connections
- Links the dynamic source pad to the depay element's sink pad
- Logs warning if linking fails
- Unreferences the sink pad after use

**Note:** This is necessary because rtspsrc creates pads dynamically after analyzing the RTSP stream.

---

## Stream Control Functions

### `start_stream(AppData *app)`
Initiates RTSP stream playback.

**Parameters:**
- `app` - Pointer to AppData structure

**Functionality:**
- Calls `ensure_pipeline()` to create pipeline if needed
- Calls `ensure_paintable()` to set up video display
- Sets pipeline state to `GST_STATE_PLAYING`
- Handles state change failures by resetting pipeline to NULL
- Updates UI state: disables Start button, enables Stop button
- Uses `widget_is_ready()` to safely check widget validity before modification

---

### `stop_stream(AppData *app)`
Halts RTSP stream playback.

**Parameters:**
- `app` - Pointer to AppData structure

**Functionality:**
- Sets pipeline state to `GST_STATE_NULL` (complete shutdown)
- Updates UI state: enables Start button, disables Stop button
- Safe to call even if pipeline doesn't exist (null check)

---

### `widget_is_ready(GtkWidget *widget)`
Inline validation helper function.

**Parameters:**
- `widget` - GTK widget pointer to check

**Returns:** `true` if widget is non-null and a valid GTK widget, `false` otherwise

**Purpose:** Prevents crashes from accessing invalid or destroyed widgets

---

## GTK UI Event Handlers

### `on_start_clicked(GtkButton *button, gpointer user_data)`
Button click callback for the Start Stream button.

**Parameters:**
- `button` - The clicked button (unused)
- `user_data` - Pointer to AppData structure

**Functionality:**
- Invokes `start_stream()` to begin playback

---

### `on_stop_clicked(GtkButton *button, gpointer user_data)`
Button click callback for the Stop Stream button.

**Parameters:**
- `button` - The clicked button (unused)
- `user_data` - Pointer to AppData structure

**Functionality:**
- Invokes `stop_stream()` to halt playback

---

### `on_app_shutdown(GApplication *gapp, gpointer user_data)`
Application shutdown signal handler.

**Parameters:**
- `gapp` - GTK application (unused)
- `user_data` - Pointer to AppData structure

**Functionality:**
- Calls `stop_stream()` to ensure clean shutdown before application exit
- Prevents resource leaks and ensures proper GStreamer cleanup

---

### `on_app_activate(GApplication *gapp, gpointer user_data)`
Application activation callback that builds the user interface.

**Parameters:**
- `gapp` - GTK application instance
- `user_data` - Pointer to AppData structure

**UI Layout:**
```
┌─────────────────────────────┐
│      RTSP Viewer Window     │
│  (1280×720 default size)    │
├─────────────────────────────┤
│                             │
│    Video Picture Widget     │
│    (expands to fill space)  │
│                             │
├─────────────────────────────┤
│ [Start Stream] [Stop Stream]│
└─────────────────────────────┘
```

**Functionality:**
- Creates application window with title "RTSP Viewer" and default size 1280×720
- Builds vertical box layout with 8-pixel spacing
- Creates expandable picture widget for video display
- Creates horizontal button box with Start and Stop buttons
- Connects button click signals to respective handlers
- Adds weak pointers to buttons for lifecycle tracking (automatically null when destroyed)
- Shows the window
- Auto-starts stream playback to match previous behavior

---

### `on_command_line(GApplication *gapp, GApplicationCommandLine *cmdline, gpointer user_data)`
Command-line argument handler.

**Parameters:**
- `gapp` - GTK application instance
- `cmdline` - Command-line object
- `user_data` - Pointer to AppData structure

**Command-Line Arguments:**
- `argv[1]` - RTSP URL (optional, overrides default)
- `argv[2]` - Latency in milliseconds (optional, overrides default 10ms)

**Functionality:**
- Parses command-line arguments
- Updates AppData URL and latency if provided
- Frees argument array
- Activates the application

**Returns:** 0 (success)

**Example Usage:**
```bash
./rtsp_viewer rtsp://192.168.1.200:8554/stream 50
```

---

## Main Entry Point

### `main(int argc, char *argv[])`
Application entry point and lifecycle manager.

**Parameters:**
- `argc` - Argument count
- `argv` - Argument vector

**Initialization:**
1. Calls `gst_init()` to initialize GStreamer framework
2. Creates AppData instance
3. Creates GTK application with ID "com.example.rtsp_viewer"
4. Sets `G_APPLICATION_HANDLES_COMMAND_LINE` flag for argument processing

**Signal Connections:**
- `command-line` → `on_command_line()` for argument parsing
- `activate` → `on_app_activate()` for UI creation
- `shutdown` → `on_app_shutdown()` for cleanup

**Main Loop:**
- Runs GTK main loop with `g_application_run()`
- Blocks until application exits

**Cleanup:**
1. Stops stream playback
2. Unreferences pipeline if it exists
3. Unreferences GTK application

**Returns:** Exit status from GTK application

---

## GStreamer Pipeline Architecture

### Data Flow
```
RTSP Server
    ↓
rtspsrc (network reception, RTP/RTCP handling)
    ↓
rtph264depay (extract H.264 NAL units from RTP packets)
    ↓
h264parse (parse H.264 stream structure, extract metadata)
    ↓
nvh264dec (GPU-accelerated H.264 decoding - NVIDIA hardware)
    ↓
videoconvert (format conversion for GTK compatibility)
    ↓
gtk4paintablesink (render to GTK4 paintable interface)
    ↓
GTK Picture Widget (display in UI)
```

### Key Features
- **Hardware Acceleration**: Uses NVIDIA GPU via nvh264dec for efficient decoding
- **Low Latency**: Configurable latency (default 10ms) for minimal delay
- **Protocol Flexibility**: Supports both UDP and TCP for RTSP transport
- **Dynamic Pad Handling**: Automatically connects rtspsrc pads when stream is analyzed
- **GTK4 Integration**: Native rendering using paintable interface

### Error Handling
- Pipeline creation failures are logged and handled gracefully
- Bus messages capture runtime errors and state changes
- Failed state transitions trigger automatic cleanup
- Widget validity checks prevent UI crashes

---

## Threading Model
- **GTK Main Thread**: Handles UI events and widget updates
- **GStreamer Threads**: Pipeline runs in separate GStreamer worker threads
- **Bus Watch**: Runs in main thread via GLib main loop integration
- **Signal Callbacks**: Executed in context of signal emission (typically GStreamer threads for pipeline signals)

## Memory Management
- Weak pointers on buttons prevent dangling references after widget destruction
- GObject reference counting for GStreamer elements and GTK widgets
- Explicit cleanup in shutdown handler and main function
- Bus watch automatically removed when pipeline is destroyed

---

*Generated: November 10, 2025*
