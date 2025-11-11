# RTSP Viewer

Low-latency RTSP stream viewer using GStreamer 1.24.13 and GTK4.

## Features

- Hardware-accelerated H.264 decoding (NVIDIA GPU)
- Optimized for low latency (~10-15ms glass-to-glass)
- UDP-only transport for minimum delay
- GTK4 GUI with start/stop controls

## Prerequisites

- C++ compiler (g++ 11 or higher)
- GStreamer 1.24.13 (custom build in `~/.local/gstreamer-1.24/`)
- GTK4 4.6+
- pkg-config
- NVIDIA GPU with hardware decoder support

## Building the Project

### Using VS Code (Recommended)

Press `Ctrl+Shift+B` to build using the default task.

### Using Command Line

```bash
g++ -g -std=c++17 src/main.cpp -o rtsp_viewer \
    $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-video-1.0 gtk4)
```

## Running the Application

```bash
# With default URL (rtsp://192.168.1.100:8554/quality_h264)
./rtsp_viewer

# With custom URL
./rtsp_viewer rtsp://your-camera-ip:8554/stream

# With custom URL and latency (in milliseconds)
./rtsp_viewer rtsp://your-camera-ip:8554/stream 10
```

## Documentation

See the `docs/` folder for detailed documentation:
- `function_descriptions.md` - Detailed function documentation
- `dependencies_and_versions.md` - GStreamer and GTK version info
- `build_tools_guide.md` - Build system explanation

## Project Structure

```
.
├── src/
│   └── main.cpp          # Main application source
├── docs/                 # Documentation
├── .vscode/
│   └── tasks.json        # VS Code build tasks
├── rtsp_viewer           # Compiled executable
└── README.md
```

## License

See LICENSE file for details.
