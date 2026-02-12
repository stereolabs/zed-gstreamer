#!/bin/bash
# =============================================================================
# ZED Camera UDP Streaming (Lowest Latency)
# =============================================================================
# Raw UDP/RTP has the absolute minimum latency (~30-80ms)
# No session overhead, no muxing, just raw H.265 RTP packets
#
# For GMSL cameras (ZED X, ZED X Mini) with SDK 5.2+:
#   - Uses zero-copy NV12 for maximum performance
#
# For USB cameras or older SDK:
#   - Uses BGRA with $NVVIDCONV to NVMM NV12
#
# Usage:
#   ./udp_stream.sh [client_ip] [port] [fps] [resolution] [bitrate]
#
# Resolution options:
#   0 = HD2K, 1 = HD1080, 2 = HD1200, 3 = HD720, 4 = SVGA, 5 = VGA, 6 = Auto
#
# Examples:
#   ./udp_stream.sh 192.168.6.120           # Stream to specific client
#   ./udp_stream.sh 192.168.6.120 5000 100 4  # SVGA at 100fps
#
# Client playback:
#   gst-launch-1.0 udpsrc port=5000 ! application/x-rtp,encoding-name=H265 ! \
#       rtph265depay ! h265parse ! avdec_h265 ! autovideosink sync=false
#
# =============================================================================

set -e

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

CLIENT_IP="${1:-}"
PORT="${2:-5000}"
FPS="${3:-30}"
RESOLUTION="${4:-6}"
BITRATE="${5:-8000000}"

if [ -z "$CLIENT_IP" ]; then
    echo "Usage: $0 <client_ip> [port] [fps] [resolution] [bitrate]"
    echo ""
    echo "Example: $0 192.168.6.120 5000 100 4"
    exit 1
fi

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

# Determine codec for client instructions
if has_hw_h265_encoder || has_sw_h265_encoder; then
    CODEC="H265"
    CODEC_LOWER="h265"
else
    CODEC="H264"
    CODEC_LOWER="h264"
fi

echo "=============================================="
echo " ZED UDP Streaming (Lowest Latency)"
echo "=============================================="
print_camera_info
echo " Encoder: $(get_encoder_info)"
echo " Sending to: $CLIENT_IP:$PORT"
echo " Resolution: ${RES_NAMES[$RESOLUTION]:-$RESOLUTION}"
echo " FPS: $FPS"
echo " Bitrate: $((BITRATE / 1000000)) Mbps"
echo "=============================================="
echo ""
echo " Client command (run on $CLIENT_IP):"
echo ""
echo " GStreamer (recommended):"
echo "   gst-launch-1.0 udpsrc port=$PORT ! 'application/x-rtp,encoding-name=$CODEC' ! rtp${CODEC_LOWER}depay ! ${CODEC_LOWER}parse ! avdec_${CODEC_LOWER} ! autovideosink sync=false"
echo ""
echo " FFplay:"
echo "   ffplay -fflags nobuffer -flags low_delay -framedrop -protocol_whitelist file,udp,rtp -i udp://@:$PORT"
echo ""
echo "=============================================="

echo "Starting UDP stream..."

# Build source pipeline based on camera type
if is_zero_copy_available; then
    SOURCE="zedsrc stream-type=6 camera-resolution=$RESOLUTION camera-fps=$FPS"
else
    SOURCE="zedsrc stream-type=0 camera-resolution=$RESOLUTION camera-fps=$FPS ! $NVVIDCONV ! video/x-raw(memory:NVMM),format=NV12"
fi

# Select encoder based on hardware availability
if has_hw_h265_encoder; then
    # Hardware H.265
    gst-launch-1.0 -e \
        $SOURCE ! \
        nvv4l2h265enc bitrate=$BITRATE preset-level=4 iframeinterval=$FPS insert-sps-pps=true maxperf-enable=true ! \
        h265parse config-interval=1 ! \
        rtph265pay config-interval=1 ! \
        udpsink host=$CLIENT_IP port=$PORT sync=false async=false
elif has_hw_h264_encoder; then
    # Hardware H.264
    gst-launch-1.0 -e \
        $SOURCE ! \
        nvv4l2h264enc bitrate=$BITRATE preset-level=4 iframeinterval=$FPS insert-sps-pps=true maxperf-enable=true ! \
        h264parse config-interval=1 ! \
        rtph264pay config-interval=1 ! \
        udpsink host=$CLIENT_IP port=$PORT sync=false async=false
elif has_sw_h265_encoder; then
    # Software H.265 (Orin Nano)
    echo "WARNING: Using software encoder - expect higher CPU usage"
    gst-launch-1.0 -e \
        zedsrc stream-type=0 camera-resolution=$RESOLUTION camera-fps=$FPS ! \
        videoconvert ! video/x-raw,format=I420 ! \
        x265enc bitrate=$((BITRATE / 1000)) speed-preset=ultrafast tune=zerolatency key-int-max=$FPS ! \
        h265parse config-interval=1 ! \
        rtph265pay config-interval=1 ! \
        udpsink host=$CLIENT_IP port=$PORT sync=false async=false
elif has_sw_h264_encoder; then
    # Software H.264
    echo "WARNING: Using software encoder - expect higher CPU usage"
    gst-launch-1.0 -e \
        zedsrc stream-type=0 camera-resolution=$RESOLUTION camera-fps=$FPS ! \
        videoconvert ! video/x-raw,format=I420 ! \
        x264enc bitrate=$((BITRATE / 1000)) speed-preset=ultrafast tune=zerolatency key-int-max=$FPS ! \
        h264parse config-interval=1 ! \
        rtph264pay config-interval=1 ! \
        udpsink host=$CLIENT_IP port=$PORT sync=false async=false
else
    echo "ERROR: No video encoder available!"
    exit 1
fi
