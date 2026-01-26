#!/bin/bash
# =============================================================================
# ZED Camera Hardware Encoding Pipeline
# =============================================================================
# This script demonstrates hardware H.265 encoding from ZED cameras on
# NVIDIA Jetson platforms.
#
# For GMSL cameras (ZED X, ZED X Mini):
#   - Uses zero-copy NV12 for maximum performance
#   - Direct path to hardware encoder
#
# For USB cameras (ZED 2, ZED 2i, ZED Mini):
#   - Uses BGRA with nvvideoconvert to NVMM
#   - Slightly higher CPU usage but still hardware accelerated
#
# Requirements:
#   - NVIDIA Jetson platform (Orin, Xavier, etc.)
#   - ZED SDK 5.2+ (with Advanced Capture API for GMSL zero-copy)
#   - GStreamer 1.0 with NVIDIA plugins
#
# Usage:
#   ./hw_encode_nv12.sh [output_file] [duration_seconds] [resolution] [fps]
#
# Examples:
#   ./hw_encode_nv12.sh                          # Preview (or default record if headless)
#   ./hw_encode_nv12.sh output.mp4               # Record to file
#   ./hw_encode_nv12.sh output.mp4 60            # Record 60 seconds
#   ./hw_encode_nv12.sh output.mp4 0 1200 30     # 1920x1200@30fps, infinite
# =============================================================================

set -e

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# Default parameters
OUTPUT_FILE="${1:-}"

# UX: If no output file is provided and we are on a headless system, 
# default to a file instead of failing to open a window.
if [ -z "$OUTPUT_FILE" ] && [ -z "$DISPLAY" ]; then
    OUTPUT_FILE="output.mp4"
    echo "Info: No output file specified and no display detected. Defaulting to recording to '$OUTPUT_FILE'"
fi

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
echo " ZED Hardware Encoding Pipeline"
echo "=============================================="
print_camera_info
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
warn_if_not_jetson

# Determine stream type based on camera and SDK support
STREAM_TYPE=$(get_optimal_stream_type)

# Build the pipeline based on zero-copy availability
# Zero-copy NV12 (stream-type=5) - direct path to hardware encoder (GMSL + SDK 5.2+)
# BGRA (stream-type=0) with nvvideoconvert to NVMM NV12 (USB or older SDK)
if [ -n "$OUTPUT_FILE" ]; then
    # Recording pipeline
    # Check if we have a display for preview
    if [ -n "$DISPLAY" ] && xset q &>/dev/null 2>&1; then
        # Recording with preview (has display)
        if is_zero_copy_available; then
            # Zero-copy path
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
            # USB: Need conversion to NVMM
            if [ "$DURATION" -gt 0 ]; then
                timeout "${DURATION}s" gst-launch-1.0 -e \
                    zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
                    nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
                    tee name=t \
                    t. ! queue ! nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                        h265parse ! mp4mux ! filesink location=$OUTPUT_FILE \
                    t. ! queue ! nv3dsink sync=false \
                    || true
            else
                gst-launch-1.0 -e \
                    zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
                    nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
                    tee name=t \
                    t. ! queue ! nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                        h265parse ! mp4mux ! filesink location=$OUTPUT_FILE \
                    t. ! queue ! nv3dsink sync=false
            fi
        fi
    else
        # Recording only (no display / SSH session)
        echo "No display detected - recording without preview"
        if is_zero_copy_available; then
            # Zero-copy path
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
        else
            # Need conversion to NVMM (USB or older SDK)
            if [ "$DURATION" -gt 0 ]; then
                timeout "${DURATION}s" gst-launch-1.0 -e \
                    zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
                    nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
                    nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                    h265parse ! mp4mux ! filesink location=$OUTPUT_FILE \
                    || true
            else
                gst-launch-1.0 -e \
                    zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
                    nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
                    nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                    h265parse ! mp4mux ! filesink location=$OUTPUT_FILE
            fi
        fi
    fi
else
    # Preview only pipeline
    if is_zero_copy_available; then
        # Zero-copy: NVMM directly to nv3dsink
        gst-launch-1.0 \
            zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=5 ! \
            nv3dsink sync=false
    else
        # Need conversion to NVMM for display
        gst-launch-1.0 \
            zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
            nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
            nv3dsink sync=false
    fi
fi

echo ""
echo "Pipeline completed."
if [ -n "$OUTPUT_FILE" ] && [ -f "$OUTPUT_FILE" ]; then
    echo "Output saved to: $OUTPUT_FILE"
    ls -lh "$OUTPUT_FILE"
fi
