#!/bin/bash -e
# =============================================================================
# H.264 UDP Stream Receiver
# =============================================================================
# Receives H.264 video stream over UDP and displays it.
# Uses hardware decoding on Jetson for optimal performance.
#
# Usage:
#   ./network-rgb-h264-receiver.sh [port]
#
# Example sender (on another machine or same machine):
#   gst-launch-1.0 zedsrc ! nvvideoconvert ! nvv4l2h264enc ! rtph264pay ! \
#       udpsink host=<receiver_ip> port=5000
# =============================================================================

PORT="${1:-5000}"

echo "Listening for H.264 stream on UDP port $PORT..."

# Try hardware decoder first (nvv4l2decoder), fall back to software (avdec_h264)
if gst-inspect-1.0 nvv4l2decoder > /dev/null 2>&1; then
    echo "Using hardware H.264 decoder"
    gst-launch-1.0 udpsrc port=$PORT ! \
        "application/x-rtp,clock-rate=90000,payload=96" ! \
        queue ! rtph264depay ! h264parse ! nvv4l2decoder ! \
        queue ! nv3dsink sync=false
else
    echo "Using software H.264 decoder"
    gst-launch-1.0 udpsrc port=$PORT ! \
        "application/x-rtp,clock-rate=90000,payload=96" ! \
        queue ! rtph264depay ! h264parse ! avdec_h264 ! \
        queue ! autovideoconvert ! fpsdisplaysink
fi