# ZED GStreamer Examples for Jetson

These scripts demonstrate how to use the ZED GStreamer plugin on Jetson platforms with hardware-accelerated encoding.

## Camera Support

The scripts automatically detect your camera type and SDK version to use the optimal pipeline:

### GMSL Cameras (ZED X, ZED X Mini) with SDK 5.2+
- **Zero-copy NV12** (stream-type=6): Direct memory path from camera to encoder
- Maximum performance with minimal CPU overhead
- Requires ZED SDK 5.2+ with Advanced Capture API

### USB Cameras (ZED 2, ZED 2i, ZED Mini) or older SDK
- **BGRA with conversion** (stream-type=0): Uses `nvvideoconvert` to NVMM NV12
- Still hardware accelerated, slightly higher CPU usage
- Works with any ZED SDK version

## Requirements

- NVIDIA Jetson platform (Orin, Xavier, etc.)
- ZED SDK (5.2+ recommended for zero-copy with GMSL cameras)
- GStreamer 1.0 with NVIDIA plugins
- DeepStream SDK (for DeepStream examples only)

## Stream Types

The `zedsrc` plugin supports these stream types:

- `stream-type=0` - **Left BGRA**: Standard left image (works on all cameras)
- `stream-type=1` - **Right BGRA**: Right image
- `stream-type=2` - **Stereo BGRA**: Left and right stacked vertically
- `stream-type=3` - **Depth 16-bit**: Depth map as GRAY16
- `stream-type=4` - **Left + Depth BGRA**: Left and depth stacked
- `stream-type=5` - **Stereo BGRA SBS**: Left and right side-by-side (for VR)
- `stream-type=6` - **Raw NV12 zero-copy**: GMSL cameras only, SDK 5.2+
- `stream-type=7` - **Raw NV12 stereo zero-copy**: Side-by-side stereo, GMSL only
- `stream-type=8` - **Raw NV12 right zero-copy**: Right eye only, GMSL only

## Scripts

### 1. Hardware Encoding Pipeline (`hw_encode_nv12.sh`)

Records ZED camera output to H.265 file using NVIDIA's hardware encoder:

```bash
./hw_encode_nv12.sh [output_file.mp4] [duration_seconds]
```

Features:
- Automatic camera detection (zero-copy for GMSL, converted for USB)
- Hardware H.265 encoding (nvv4l2h265enc)
- Low latency, minimal CPU usage
- Optional preview window

### 2. DeepStream Inference Pipeline (`deepstream_inference.sh`)

Runs object detection on ZED camera feed using DeepStream:

```bash
./deepstream_inference.sh [config_file]
```

Features:
- Works with both GMSL and USB cameras
- YOLOv5/ResNet object detection
- On-screen display with bounding boxes
- Optional recording

### 3. Streaming Scripts

- `srt_stream.sh` - SRT streaming (~50-100ms latency)
- `rtsp_stream.sh` - RTSP streaming (~200ms latency)
- `udp_stream.sh` - Raw UDP/RTP streaming (~30-80ms latency)

All streaming scripts automatically detect camera type and use the optimal pipeline.

## Building

Make sure to build the zed-gstreamer plugin:

```bash
cd /path/to/zed-gstreamer
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

For zero-copy support with GMSL cameras, ensure you have ZED SDK 5.2+ installed.

## Notes

- Zero-copy NV12 (stream-type=6/7) is only available for GMSL cameras with SDK 5.2+
- USB cameras automatically fall back to BGRA with nvvideoconvert
- The scripts will display the detected camera type and stream mode at startup
