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
#   - Uses BGRA with nvvideoconvert to NVMM NV12
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

echo "=============================================="
echo " ZED UDP Streaming (Lowest Latency)"
echo "=============================================="
print_camera_info
echo " Sending to: $CLIENT_IP:$PORT"
echo " Resolution: ${RES_NAMES[$RESOLUTION]:-$RESOLUTION}"
echo " FPS: $FPS"
echo " Bitrate: $((BITRATE / 1000000)) Mbps"
echo "=============================================="
echo ""
echo " Client command (run on $CLIENT_IP):"
echo ""
echo " GStreamer (recommended):"
echo "   gst-launch-1.0 udpsrc port=$PORT ! 'application/x-rtp,encoding-name=H265' ! rtph265depay ! h265parse ! avdec_h265 ! autovideosink sync=false"
echo ""
echo " FFplay:"
echo "   ffplay -fflags nobuffer -flags low_delay -framedrop -protocol_whitelist file,udp,rtp -i udp://@:$PORT"
echo ""
echo "=============================================="

echo "Starting UDP stream..."

# Generate SDP file for client
SDP_CONTENT="v=0
o=- 0 0 IN IP4 $CLIENT_IP
s=ZED Stream
c=IN IP4 $CLIENT_IP
t=0 0
m=video $PORT RTP/AVP 96
a=rtpmap:96 H265/90000"

echo ""
echo " If using ffplay, create this SDP file on client:"
echo "-----"
echo "$SDP_CONTENT"
echo "-----"
echo " Save as stream.sdp and run:"
echo "   ffplay -fflags nobuffer -flags low_delay -framedrop -protocol_whitelist file,udp,rtp stream.sdp"
echo ""

# Direct UDP/RTP stream - absolute minimum latency
# No RTSP session, no MPEG-TS muxing, just raw RTP packets
if is_zero_copy_available; then
    # Zero-copy NV12 path
    gst-launch-1.0 -e \
        zedsrc stream-type=5 camera-resolution=$RESOLUTION camera-fps=$FPS ! \
        nvv4l2h265enc bitrate=$BITRATE preset-level=4 iframeinterval=$FPS insert-sps-pps=true maxperf-enable=true ! \
        h265parse config-interval=1 ! \
        rtph265pay config-interval=1 ! \
        udpsink host=$CLIENT_IP port=$PORT sync=false async=false
else
    # BGRA with conversion path (USB or older SDK)
    gst-launch-1.0 -e \
        zedsrc stream-type=0 camera-resolution=$RESOLUTION camera-fps=$FPS ! \
        nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
        nvv4l2h265enc bitrate=$BITRATE preset-level=4 iframeinterval=$FPS insert-sps-pps=true maxperf-enable=true ! \
        h265parse config-interval=1 ! \
        rtph265pay config-interval=1 ! \
        udpsink host=$CLIENT_IP port=$PORT sync=false async=false
fi
