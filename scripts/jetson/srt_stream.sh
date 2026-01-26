#!/bin/bash
# =============================================================================
# ZED Camera SRT Streaming (Ultra Low Latency)
# =============================================================================
# SRT provides ~50-100ms latency vs ~200ms for RTSP
#
# Usage:
#   ./srt_stream.sh [port] [fps] [resolution] [bitrate] [latency_ms]
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
#   ./srt_stream.sh                    # Default: port 8888, 30fps, auto res, 8Mbps
#   ./srt_stream.sh 8888 100 4         # SVGA at 100fps
#   ./srt_stream.sh 8888 120 4 4000000 # SVGA at 120fps, 4Mbps
#
# Client playback:
#   ffplay -fflags nobuffer -flags low_delay -framedrop srt://<jetson_ip>:8888
#
# =============================================================================

set -e

PORT="${1:-8888}"
FPS="${2:-30}"
RESOLUTION="${3:-6}"  # 6 = Auto
BITRATE="${4:-8000000}"
SRT_LATENCY="${5:-50}"  # SRT latency in ms

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
echo " ZED SRT Streaming (Ultra Low Latency)"
echo "=============================================="
echo " Stream URL: srt://$IP_ADDR:$PORT"
echo " Resolution: ${RES_NAMES[$RESOLUTION]:-$RESOLUTION}"
echo " FPS: $FPS"
echo " Bitrate: $((BITRATE / 1000000)) Mbps"
echo " SRT Latency: ${SRT_LATENCY}ms"
echo "=============================================="
echo ""
echo " Client command (copy & paste):"
echo "   ffplay -fflags nobuffer -flags low_delay -framedrop srt://$IP_ADDR:$PORT"
echo ""
echo "=============================================="

# Check for SRT support
if ! gst-inspect-1.0 srtsink > /dev/null 2>&1; then
    echo "ERROR: SRT plugin not found. Install with:"
    echo "  sudo apt install gstreamer1.0-plugins-bad"
    exit 1
fi

echo "Starting SRT server (listener mode)..."

# SRT streaming with MPEG-TS container
gst-launch-1.0 -e \
    zedsrc stream-type=5 camera-resolution=$RESOLUTION camera-fps=$FPS ! \
    nvv4l2h265enc bitrate=$BITRATE preset-level=4 iframeinterval=$FPS insert-sps-pps=true maxperf-enable=true ! \
    h265parse config-interval=1 ! \
    mpegtsmux alignment=7 ! \
    srtsink uri="srt://:$PORT" latency=$SRT_LATENCY mode=listener wait-for-connection=true
