#!/bin/bash -e
# =============================================================================
# RTSP Stream Receiver (Low Latency)
# =============================================================================
# Connects to an RTSP server and displays the stream with hardware decoding.
#
# Usage:
#   ./rtsp-h264-receiver.sh [rtsp_url]
#
# Examples:
#   ./rtsp-h264-receiver.sh                              # Default: local server
#   ./rtsp-h264-receiver.sh rtsp://192.168.1.100:8554/zed-stream
# =============================================================================

HOST_IPS=(`hostname -I`)
DEFAULT_URL="rtsp://${HOST_IPS[0]}:8554/zed-stream"
RTSP_URL="${1:-$DEFAULT_URL}"

echo "Connecting to: $RTSP_URL"

# Try hardware decoder with low-latency settings
if gst-inspect-1.0 nvv4l2decoder > /dev/null 2>&1; then
    echo "Using hardware H.265/H.264 decoder"
    gst-launch-1.0 rtspsrc location="$RTSP_URL" latency=0 buffer-mode=auto ! \
        queue ! decodebin ! queue ! nv3dsink sync=false
else
    echo "Using software decoder"
    gst-launch-1.0 rtspsrc location="$RTSP_URL" latency=0 ! \
        queue ! decodebin ! queue ! fpsdisplaysink sync=false
fi