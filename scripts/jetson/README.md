# ZED GStreamer Zero-Copy Examples for Jetson

These scripts demonstrate how to use the ZED GStreamer plugin with zero-copy NV12 output on Jetson platforms (GMSL cameras only).

## Requirements

- NVIDIA Jetson platform (Orin, Xavier, etc.)
- ZED SDK 5.2 or newer
- ZED X or ZED X Mini camera (GMSL interface)
- GStreamer 1.0 with NVIDIA plugins
- DeepStream SDK (for DeepStream examples)

## Stream Types

The `zedsrc` plugin now supports two new stream types for zero-copy NV12 output:

- `stream-type=5` - **Raw NV12 zero-copy**: Single camera NV12 output
- `stream-type=6` - **Raw NV12 stereo zero-copy**: Side-by-side stereo NV12 output

## Scripts

### 1. Hardware Encoding Pipeline (`hw_encode_nv12.sh`)

Records ZED camera output to H.265 file using NVIDIA's hardware encoder:

```bash
./hw_encode_nv12.sh [output_file.mp4] [duration_seconds]
```

Features:
- Zero-copy NV12 from ZED camera
- Hardware H.265 encoding (nvv4l2h265enc)
- Low latency, minimal CPU usage
- Optional preview window

### 2. DeepStream Inference Pipeline (`deepstream_inference.sh`)

Runs object detection on ZED camera feed using DeepStream:

```bash
./deepstream_inference.sh [config_file]
```

Features:
- Zero-copy NV12 directly to NVIDIA inference
- YOLOv5/ResNet object detection
- On-screen display with bounding boxes
- Optional recording

## Building

Make sure to build the zed-gstreamer plugin with Advanced Capture API enabled:

```bash
cd /path/to/zed-gstreamer
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

## Notes

- Zero-copy NV12 is only available for GMSL cameras (ZED X, ZED X Mini)
- USB cameras will not work with stream-type 5 or 6
- The NV12 format is more efficient for hardware encoding and inference than BGRA
