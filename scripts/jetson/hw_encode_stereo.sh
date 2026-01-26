#!/bin/bash
# =============================================================================
# ZED Camera Stereo Recording Pipeline (Zero-Copy NV12)
# =============================================================================
# Records both left and right cameras in a side-by-side stereo format
# with hardware H.265 encoding.
#
# Usage:
#   ./hw_encode_stereo.sh [output_file] [duration_seconds]
# =============================================================================

set -e

OUTPUT_FILE="${1:-stereo_output.mp4}"
DURATION="${2:-0}"
FPS="${3:-30}"
BITRATE="${4:-16000000}"  # 16 Mbps for stereo

echo "=============================================="
echo " ZED Zero-Copy Stereo NV12 Recording"
echo "=============================================="
echo " Output: $OUTPUT_FILE"
echo " FPS: $FPS"
echo " Bitrate: $BITRATE bps"
echo "=============================================="

# stream-type=6 is RAW_NV12_STEREO (side-by-side left+right)
# Note: implementation supports zero-copy NVMM
if [ "$DURATION" -gt 0 ]; then
    timeout "${DURATION}s" gst-launch-1.0 -e \
        zedsrc camera-resolution=2 camera-fps=$FPS stream-type=6 ! \
        nvvidconv ! "video/x-raw(memory:NVMM),format=I420" ! \
        nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
        h265parse ! mp4mux ! filesink location=$OUTPUT_FILE \
        || true
else
    gst-launch-1.0 -e \
        zedsrc camera-resolution=2 camera-fps=$FPS stream-type=6 ! \
        nvvidconv ! "video/x-raw(memory:NVMM),format=I420" ! \
        nvvidconv ! "video/x-raw(memory:NVMM),format=I420" ! \
        nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
        h265parse ! mp4mux ! filesink location=$OUTPUT_FILE
fi

echo ""
echo "Stereo recording saved to: $OUTPUT_FILE"
ls -lh "$OUTPUT_FILE"
