# Dependencies and Version Configuration

## Overview
This document explains which versions of GStreamer and GTK are used in the RTSP Viewer project and how the build system is configured to use them.

---

## Library Versions

### **GStreamer: 1.24.13** (Custom Local Build)
- **Installation Location:** `/home/patrick/.local/gstreamer-1.24/`
- **Source:** Built from source code in `gstreamer-1.24.13/` directory
- **Type:** User-space installation (non-system)

### **GTK: 4.6.9** (System Package)
- **Installation Location:** System libraries (`/usr/lib/x86_64-linux-gnu/`)
- **Source:** Ubuntu 22.04 package repository (`libgtk-4-1`)
- **Type:** System-wide installation

---

## Why Custom GStreamer Installation?

### System vs. Custom Version
- **System GStreamer:** Ubuntu 22.04 ships with GStreamer **1.20.3**
- **Required Version:** This project uses GStreamer **1.24.13**
- **Version Gap:** 4 minor versions newer (1.20 → 1.24)

### Reasons for Newer Version
1. **Bug Fixes:** 1.24.13 includes fixes not present in 1.20.3
2. **Feature Improvements:** Better RTSP handling and lower latency capabilities
3. **Plugin Updates:** Improved compatibility with modern codecs and sinks
4. **gtk4paintablesink:** Better support for GTK4 integration

### Benefits of Local Installation
✅ **No System Conflicts:** Doesn't interfere with system GStreamer packages  
✅ **No Root Required:** Installed in user's home directory  
✅ **Isolated:** Other applications continue using system GStreamer  
✅ **Flexible:** Easy to update or switch versions  

---

## Build System Configuration

### pkg-config Integration

All build tasks use `pkg-config` to automatically locate and configure libraries:

```bash
pkg-config --cflags --libs gstreamer-1.0 gstreamer-video-1.0 gtk4
```

### Compiler Flags (--cflags)

```bash
# GStreamer includes (from custom installation)
-I/home/patrick/.local/gstreamer-1.24/include/gstreamer-1.0
-I/home/patrick/.local/gstreamer-1.24/include/orc-0.4

# GTK4 includes (from system installation)
-I/usr/include/gtk-4.0
-I/usr/include/gio-unix-2.0
-I/usr/include/cairo
-I/usr/include/pango-1.0
-I/usr/include/harfbuzz
-I/usr/include/gdk-pixbuf-2.0
-I/usr/include/graphene-1.0
-I/usr/include/glib-2.0

# Additional architecture-specific paths
-I/usr/include/x86_64-linux-gnu
-I/usr/lib/x86_64-linux-gnu/graphene-1.0/include
-I/usr/lib/x86_64-linux-gnu/glib-2.0/include

# CPU optimization flags
-mfpmath=sse -msse -msse2

# Threading support
-pthread
```

### Linker Flags (--libs)

```bash
# GStreamer libraries (from custom installation)
-L/home/patrick/.local/gstreamer-1.24/lib/x86_64-linux-gnu
-lgstvideo-1.0      # GStreamer video utilities
-lgstbase-1.0       # GStreamer base classes
-lgstreamer-1.0     # GStreamer core library

# GTK4 and dependencies (from system)
-lgtk-4             # GTK4 core
-lpangocairo-1.0    # Text rendering with Cairo
-lpango-1.0         # Text layout
-lharfbuzz          # Font shaping
-lgdk_pixbuf-2.0    # Image loading
-lcairo-gobject     # Cairo GObject bindings
-lcairo             # 2D graphics library
-lgraphene-1.0      # Vector/matrix operations
-lgio-2.0           # GIO (I/O and application support)
-lgobject-2.0       # GObject type system
-lglib-2.0          # GLib core utilities
```

---

## Build Tasks Configuration

### Tasks Defined in `.vscode/tasks.json`

#### 1. Build rtsp_viewer
```bash
g++ -g -std=c++17 src/main.cpp -o rtsp_viewer \
    $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-video-1.0 gtk4)
```

#### 2. C/C++: g++ build active file
```bash
g++ -g -std=c++17 ${file} -o ${fileDirname}/${fileBasenameNoExtension} \
    $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-video-1.0 gtk4)
```

#### 3. C/C++: g++-11 build active file (Default)
```bash
/usr/bin/g++-11 -fdiagnostics-color=always -g ${file} \
    -o ${fileDirname}/${fileBasenameNoExtension} \
    $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-video-1.0 gtk4)
```

All tasks use `bash -lc` to ensure proper environment variable loading (important for finding custom GStreamer installation).

---

## pkg-config Resolution Process

### How pkg-config Finds Libraries

1. **Search Order:**
   - `PKG_CONFIG_PATH` environment variable
   - User's local pkgconfig directories
   - System pkgconfig directories

2. **GStreamer Resolution:**
   - Finds `.pc` files in `~/.local/gstreamer-1.24/lib/x86_64-linux-gnu/pkgconfig/`
   - `gstreamer-1.0.pc` defines version, includes, and libs
   - `gstreamer-video-1.0.pc` adds video-specific functionality

3. **GTK Resolution:**
   - Finds `.pc` file in `/usr/lib/x86_64-linux-gnu/pkgconfig/gtk4.pc`
   - Automatically pulls in dependencies (glib, cairo, pango, etc.)

---

## Required GStreamer Components

### Core Components Used

| Component | Purpose |
|-----------|---------|
| `gstreamer-1.0` | Core GStreamer framework |
| `gstreamer-video-1.0` | Video handling utilities |
| `gst-plugins-base` | Basic plugin set (includes rtpbin) |
| `gst-plugins-good` | Good quality plugins (includes rtph264depay) |
| `gst-plugins-bad` | Beta/experimental plugins (includes nvh264dec) |
| `gst-plugins-ugly` | Plugins with licensing issues |

### Specific Plugins Required

- **rtspsrc** - RTSP client source (plugins-good)
- **rtph264depay** - RTP H.264 depayloader (plugins-good)
- **h264parse** - H.264 stream parser (plugins-bad)
- **nvh264dec** - NVIDIA hardware H.264 decoder (plugins-bad)
- **videoconvert** - Video format converter (plugins-base)
- **gtk4paintablesink** - GTK4 video sink (plugins-good or plugins-bad)

---

## GTK4 Requirements

### System Packages

```bash
# Main GTK4 library
libgtk-4-1

# Development files (headers)
libgtk-4-dev

# Common files
libgtk-4-common

# GTK4 binary tools
libgtk-4-bin
```

### GTK4 Dependencies (Auto-included)

- **GLib 2.0** - Core utilities and object system
- **GIO** - I/O library
- **Cairo** - 2D graphics rendering
- **Pango** - Text layout and rendering
- **GdkPixbuf** - Image loading
- **Graphene** - Vector and matrix operations
- **HarfBuzz** - Font shaping

---

## Verifying Installation

### Check Versions

```bash
# Check GStreamer version
pkg-config --modversion gstreamer-1.0
# Output: 1.24.13

# Check GTK version
pkg-config --modversion gtk4
# Output: 4.6.9

# Check GStreamer plugins
gst-inspect-1.0 rtspsrc
gst-inspect-1.0 nvh264dec
gst-inspect-1.0 gtk4paintablesink
```

### Check Compiler/Linker Flags

```bash
# Show all include paths
pkg-config --cflags gstreamer-1.0 gstreamer-video-1.0 gtk4

# Show all library paths
pkg-config --libs gstreamer-1.0 gstreamer-video-1.0 gtk4
```

### Check Library Paths

```bash
# Find GStreamer libraries
ls -la ~/.local/gstreamer-1.24/lib/x86_64-linux-gnu/*.so

# Find GTK4 libraries
ls -la /usr/lib/x86_64-linux-gnu/libgtk-4.so*
```

---

## Environment Configuration

### Required Environment Variables

For the custom GStreamer installation to be found by pkg-config and the runtime linker:

```bash
# Add to ~/.bashrc or session startup

# pkg-config search path
export PKG_CONFIG_PATH="$HOME/.local/gstreamer-1.24/lib/x86_64-linux-gnu/pkgconfig:$PKG_CONFIG_PATH"

# Library search path (for runtime)
export LD_LIBRARY_PATH="$HOME/.local/gstreamer-1.24/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"

# Binary search path
export PATH="$HOME/.local/gstreamer-1.24/bin:$PATH"

# GStreamer plugin path
export GST_PLUGIN_PATH="$HOME/.local/gstreamer-1.24/lib/x86_64-linux-gnu/gstreamer-1.0"
```

---

## Troubleshooting

### GStreamer Not Found
**Problem:** `pkg-config --modversion gstreamer-1.0` fails or shows wrong version

**Solution:**
1. Check `PKG_CONFIG_PATH` includes custom installation path
2. Verify `.pc` files exist in `~/.local/gstreamer-1.24/lib/x86_64-linux-gnu/pkgconfig/`
3. Source your shell config: `source ~/.bashrc`

### Plugin Not Found at Runtime
**Problem:** Error like "no element 'gtk4paintablesink'"

**Solution:**
1. Check `GST_PLUGIN_PATH` points to plugin directory
2. Verify plugin exists: `gst-inspect-1.0 gtk4paintablesink`
3. Check plugin registry: `rm -rf ~/.cache/gstreamer-1.0/`

### Wrong Library Version Loaded
**Problem:** Application uses system GStreamer instead of custom

**Solution:**
1. Check `LD_LIBRARY_PATH` has custom path first
2. Verify with: `ldd ./rtsp_viewer | grep gstreamer`
3. Should show `~/.local/gstreamer-1.24/` paths

---

## Build From Source (Reference)

If you need to rebuild GStreamer 1.24.13:

```bash
# Navigate to source directory
cd gstreamer-1.24.13/

# Configure build
meson setup build --prefix=$HOME/.local/gstreamer-1.24

# Compile
ninja -C build

# Install to local directory
ninja -C build install
```

---

## Summary

| Component | Version | Source | Location |
|-----------|---------|--------|----------|
| **GStreamer** | 1.24.13 | Custom build | `~/.local/gstreamer-1.24/` |
| **GTK4** | 4.6.9 | System package | `/usr/` |
| **Compiler** | g++-11 | System package | `/usr/bin/g++-11` |
| **C++ Standard** | C++17 | Build config | `-std=c++17` |

**Build System:** pkg-config automatically resolves paths and dependencies  
**Integration:** VS Code tasks configured to use both libraries seamlessly  
**Portability:** Custom GStreamer in user space, no root required  

---

*Last Updated: November 11, 2025*
