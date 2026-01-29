#!/bin/bash
# =============================================================================
# ZED Camera RTSP Streaming Pipeline
# =============================================================================
# Streams ZED camera to RTSP endpoint using hardware H.265 encoding
# with low-latency settings optimized for real-time viewing.
#
# For GMSL cameras (ZED X, ZED X Mini) with SDK 5.2+:
#   - Uses zero-copy NV12 for maximum performance
#
# For USB cameras or older SDK:
#   - Uses BGRA with nvvideoconvert to NVMM NV12
#
# Usage:
#   ./rtsp_stream.sh [port] [fps] [resolution] [bitrate]
#
# Resolution options:
#   0 = HD2K (2208x1242, USB3)
#   1 = HD1080 (1920x1080, USB3/GMSL2)
#   2 = HD1200 (1920x1200, GMSL2)
#   3 = HD720 (1280x720, USB3)
#   4 = SVGA (960x600, GMSL2) - supports up to 120fps
#   5 = VGA (672x376, USB3)
#   6 = Auto (default for camera model)
#
# Examples:
#   ./rtsp_stream.sh                    # Default: port 8554, 30fps, auto, 8Mbps
#   ./rtsp_stream.sh 8554 100 4         # SVGA at 100fps
#   ./rtsp_stream.sh 8554 60 2 12000000 # HD1200 at 60fps, 12Mbps
#
# Client playback (low-latency):
#   ffplay -probesize 32 -analyzeduration 0 -fflags nobuffer -flags low_delay \
#          -framedrop -sync ext -rtsp_transport udp rtsp://<jetson_ip>:8554/zed-stream
#
# =============================================================================

set -e

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

PORT="${1:-8554}"
FPS="${2:-30}"
RESOLUTION="${3:-6}"  # 6 = Auto
BITRATE="${4:-8000000}"  # 8 Mbps default

# Resolution names for display
declare -A RES_NAMES=(
    [0]="HD2K (2208x1242)"
    [1]="HD1080 (1920x1080)"
    [2]="HD1200 (1920x1200)"
    [3]="HD720 (1280x720)"
    [4]="SVGA (960x600)"
    [5]="VGA (672x376)"
    [6]="Auto"
)

IP_ADDR=$(hostname -I | awk '{print $1}')

echo "=============================================="
echo " ZED RTSP Streaming (Low Latency)"
echo "=============================================="
print_camera_info
echo " Encoder: $(get_encoder_info)"
echo " Stream URL: rtsp://$IP_ADDR:$PORT/zed-stream"
echo " Resolution: ${RES_NAMES[$RESOLUTION]:-$RESOLUTION}"
echo " FPS: $FPS"
echo " Bitrate: $((BITRATE / 1000000)) Mbps"
echo "=============================================="
echo ""
echo " Client command (copy & paste):"
if has_hw_h265_encoder || has_sw_h265_encoder; then
    echo " ffplay -probesize 32 -analyzeduration 0 -fflags nobuffer -flags low_delay -framedrop -sync ext -rtsp_transport udp rtsp://$IP_ADDR:$PORT/zed-stream"
else
    echo " ffplay -probesize 32 -analyzeduration 0 -fflags nobuffer -flags low_delay -framedrop -sync ext rtsp://$IP_ADDR:$PORT/zed-stream"
fi
echo ""
echo "=============================================="

# gst-zed-rtsp-launch is installed to /usr/bin by 'make install'
if ! command -v gst-zed-rtsp-launch > /dev/null 2>&1; then
    echo "ERROR: gst-zed-rtsp-launch not found."
    echo "Run 'sudo make install' in the zed-gstreamer build directory."
    exit 1
fi

# Build source pipeline based on camera type
if is_zero_copy_available; then
    SOURCE="zedsrc stream-type=5 camera-resolution=$RESOLUTION camera-fps=$FPS"
else
    SOURCE="zedsrc stream-type=0 camera-resolution=$RESOLUTION camera-fps=$FPS ! nvvideoconvert ! video/x-raw(memory:NVMM),format=NV12"
fi

# Select encoder based on hardware availability
# Priority: HW H.265 > HW H.264 > SW H.265 > SW H.264
if has_hw_h265_encoder; then
    # Hardware H.265 (Orin, Xavier, etc.)
    gst-zed-rtsp-launch -p $PORT -a 0.0.0.0 \
        $SOURCE ! \
        nvv4l2h265enc bitrate=$BITRATE preset-level=4 iframeinterval=$FPS insert-sps-pps=true maxperf-enable=true ! \
        h265parse config-interval=1 ! \
        rtph265pay name=pay0 pt=96
elif has_hw_h264_encoder; then
    # Hardware H.264 (fallback if no H.265)
    gst-zed-rtsp-launch -p $PORT -a 0.0.0.0 \
        $SOURCE ! \
        nvv4l2h264enc bitrate=$BITRATE preset-level=4 iframeinterval=$FPS insert-sps-pps=true maxperf-enable=true ! \
        h264parse config-interval=1 ! \
        rtph264pay name=pay0 pt=96
elif has_sw_h265_encoder; then
    # Software H.265 (Orin Nano - no NVENC)
    echo "WARNING: Using software encoder - expect higher CPU usage and latency"
    # Need to convert from NVMM to system memory for software encoder
    gst-zed-rtsp-launch -p $PORT -a 0.0.0.0 \
        zedsrc stream-type=0 camera-resolution=$RESOLUTION camera-fps=$FPS ! \
        x265enc bitrate=$((BITRATE / 1000)) speed-preset=ultrafast tune=zerolatency key-int-max=$FPS ! \
        h265parse config-interval=1 ! \
        rtph265pay name=pay0 pt=96
elif has_sw_h264_encoder; then
    # Software H.264 (last resort)
    echo "WARNING: Using software encoder - expect higher CPU usage and latency"
    gst-zed-rtsp-launch -p $PORT -a 0.0.0.0 \
        zedsrc stream-type=0 camera-resolution=$RESOLUTION camera-fps=$FPS ! \
        videoconvert ! video/x-raw,format=I420 ! \
        x264enc bitrate=$((BITRATE / 1000)) speed-preset=ultrafast tune=zerolatency key-int-max=$FPS ! \
        h264parse config-interval=1 ! \
        rtph264pay name=pay0 pt=96
else
    echo "ERROR: No video encoder available!"
    echo "Install software encoders: sudo apt install gstreamer1.0-plugins-ugly"
    exit 1
fi
