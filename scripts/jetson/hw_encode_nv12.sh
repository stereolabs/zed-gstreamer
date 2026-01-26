#!/bin/bash
# =============================================================================
# ZED Camera Hardware Encoding Pipeline (Zero-Copy NV12)
# =============================================================================
# This script demonstrates zero-copy NV12 capture from ZED GMSL cameras
# with hardware H.265 encoding on NVIDIA Jetson platforms.
#
# Requirements:
#   - NVIDIA Jetson platform (Orin, Xavier, etc.)
#   - ZED SDK 5.2+ with Advanced Capture API enabled
#   - ZED X or ZED X Mini camera (GMSL)
#   - GStreamer 1.0 with NVIDIA plugins
#
# Usage:
#   ./hw_encode_nv12.sh [output_file] [duration_seconds] [resolution] [fps]
#
# Examples:
#   ./hw_encode_nv12.sh                          # Preview only
#   ./hw_encode_nv12.sh output.mp4               # Record to file
#   ./hw_encode_nv12.sh output.mp4 60            # Record 60 seconds
#   ./hw_encode_nv12.sh output.mp4 0 1200 30     # 1920x1200@30fps, infinite
# =============================================================================

set -e

# Default parameters
OUTPUT_FILE="${1:-}"
DURATION="${2:-0}"
RESOLUTION="${3:-1200}"  # HD1200 by default
FPS="${4:-30}"
BITRATE="${5:-8000000}"  # 8 Mbps default

# Map resolution to zedsrc resolution property
case "$RESOLUTION" in
    2K|2k|2208)
        RES_PROP=0
        ;;
    1080|1920)
        RES_PROP=1
        ;;
    1200)
        RES_PROP=2
        ;;
    720)
        RES_PROP=3
        ;;
    SVGA|svga|600)
        RES_PROP=4
        ;;
    VGA|vga|376)
        RES_PROP=5
        ;;
    *)
        RES_PROP=6  # AUTO
        ;;
esac

echo "=============================================="
echo " ZED Zero-Copy NV12 Hardware Encoding"
echo "=============================================="
echo " Resolution: $RESOLUTION (prop=$RES_PROP)"
echo " FPS: $FPS"
echo " Bitrate: $BITRATE bps"
if [ -n "$OUTPUT_FILE" ]; then
    echo " Output: $OUTPUT_FILE"
    if [ "$DURATION" -gt 0 ]; then
        echo " Duration: ${DURATION}s"
    else
        echo " Duration: Until Ctrl+C"
    fi
else
    echo " Mode: Preview only (no recording)"
fi
echo "=============================================="

# Check if zedsrc plugin is available
if ! gst-inspect-1.0 zedsrc > /dev/null 2>&1; then
    echo "ERROR: zedsrc plugin not found!"
    echo "Please build and install the zed-gstreamer plugin."
    exit 1
fi

# Check if this is a Jetson platform
if [ ! -f /etc/nv_tegra_release ]; then
    echo "WARNING: This script is designed for NVIDIA Jetson platforms."
    echo "Zero-copy NV12 may not be available on this system."
fi

# Build and run the pipeline
# stream-type=5 outputs video/x-raw(memory:NVMM) with NV12 format
# This provides true zero-copy from ZED GMSL camera to NVIDIA hardware encoder
if [ -n "$OUTPUT_FILE" ]; then
    # Recording pipeline
    # Check if we have a display for preview
    if [ -n "$DISPLAY" ] && xset q &>/dev/null 2>&1; then
        # Recording with preview (has display)
        if [ "$DURATION" -gt 0 ]; then
            timeout "${DURATION}s" gst-launch-1.0 -e \
                zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=5 ! \
                tee name=t \
                t. ! queue ! nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                    h265parse ! mp4mux ! filesink location=$OUTPUT_FILE \
                t. ! queue ! nv3dsink sync=false \
                || true
        else
            gst-launch-1.0 -e \
                zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=5 ! \
                tee name=t \
                t. ! queue ! nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                    h265parse ! mp4mux ! filesink location=$OUTPUT_FILE \
                t. ! queue ! nv3dsink sync=false
        fi
    else
        # Recording only (no display / SSH session)
        echo "No display detected - recording without preview"
        if [ "$DURATION" -gt 0 ]; then
            timeout "${DURATION}s" gst-launch-1.0 -e \
                zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=5 ! \
                nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                h265parse ! mp4mux ! filesink location=$OUTPUT_FILE \
                || true
        else
            gst-launch-1.0 -e \
                zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=5 ! \
                nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                h265parse ! mp4mux ! filesink location=$OUTPUT_FILE
        fi
    fi
else
    # Preview only pipeline (NVMM directly to nv3dsink)
    gst-launch-1.0 \
        zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=5 ! \
        nv3dsink sync=false
fi

echo ""
echo "Pipeline completed."
if [ -n "$OUTPUT_FILE" ] && [ -f "$OUTPUT_FILE" ]; then
    echo "Output saved to: $OUTPUT_FILE"
    ls -lh "$OUTPUT_FILE"
fi
