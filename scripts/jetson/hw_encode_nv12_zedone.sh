#!/bin/bash
# =============================================================================
# ZED X One Hardware H.265/HEVC Encoding Pipeline
# =============================================================================
# This script demonstrates hardware H.265 encoding from ZED X One cameras on
# NVIDIA Jetson platforms.
#
# For ZED X One GS / ZED X One 4K (GMSL cameras):
#   - Uses zero-copy NV12 for maximum performance
#   - Direct path to hardware encoder (no CPU copy)
#
# Requirements:
#   - NVIDIA Jetson platform (Orin, Xavier, etc.)
#   - ZED SDK 5.2+ (with Advanced Capture API for zero-copy)
#   - GStreamer 1.0 with NVIDIA plugins
#
# Usage:
#   ./hw_encode_nv12_zedone.sh [OPTIONS] [output_file] [duration_seconds]
#
# Options:
#   --resolution RES    Camera resolution (4K, QHDPLUS, 1200, 1080, SVGA)
#   --fps FPS           Frame rate (15, 30, 60, 120)
#   --bitrate BITRATE   Encoding bitrate in bps (default: 8000000)
#
# Examples:
#   ./hw_encode_nv12_zedone.sh                              # Preview only
#   ./hw_encode_nv12_zedone.sh output.mp4                   # Record to file
#   ./hw_encode_nv12_zedone.sh output.mp4 60                # Record 60 seconds
#   ./hw_encode_nv12_zedone.sh --resolution 4K output.mp4   # 4K recording
#   ./hw_encode_nv12_zedone.sh --resolution 1200 --fps 60 output.mp4
# =============================================================================

set -e

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# Default parameters
OUTPUT_FILE=""
DURATION=0
RESOLUTION="1200"
FPS="30"
BITRATE="8000000"  # 8 Mbps default

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --resolution)
            RESOLUTION="$2"
            shift 2
            ;;
        --fps)
            FPS="$2"
            shift 2
            ;;
        --bitrate)
            BITRATE="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS] [output_file] [duration_seconds]"
            echo ""
            echo "Options:"
            echo "  --resolution RES    Camera resolution (4K, QHDPLUS, 1200, 1080, SVGA)"
            echo "  --fps FPS           Frame rate (15, 30, 60, 120 for SVGA)"
            echo "  --bitrate BITRATE   Encoding bitrate in bps (default: 8000000)"
            echo ""
            echo "Examples:"
            echo "  $0 output.mp4                     # Record to file"
            echo "  $0 output.mp4 60                  # Record 60 seconds"
            echo "  $0 --resolution 4K output.mp4    # 4K recording"
            exit 0
            ;;
        *)
            # Positional arguments
            if [ -z "$OUTPUT_FILE" ]; then
                OUTPUT_FILE="$1"
            elif [ "$DURATION" -eq 0 ]; then
                DURATION="$1"
            fi
            shift
            ;;
    esac
done

# UX: If no output file is provided and we are on a headless system, 
# default to a file instead of failing to open a window.
if [ -z "$OUTPUT_FILE" ] && [ -z "$DISPLAY" ]; then
    OUTPUT_FILE="output_zedone_h265.mp4"
    echo "Info: No output file specified and no display detected. Defaulting to recording to '$OUTPUT_FILE'"
fi

# Map resolution to zedxonesrc resolution property
# ZED X One resolutions: SVGA (0), HD1080 (1), HD1200 (2), QHDPLUS (3), 4K (4)
case "$RESOLUTION" in
    4K|4k|3840|2160)
        RES_PROP=4
        RES_NAME="4K (3840x2160)"
        ;;
    QHDPLUS|qhdplus|3200|1800)
        RES_PROP=3
        RES_NAME="QHD+ (3200x1800)"
        ;;
    1200|HD1200)
        RES_PROP=2
        RES_NAME="HD1200 (1920x1200)"
        ;;
    1080|HD1080)
        RES_PROP=1
        RES_NAME="HD1080 (1920x1080)"
        ;;
    SVGA|svga|600)
        RES_PROP=0
        RES_NAME="SVGA (960x600)"
        ;;
    *)
        RES_PROP=2  # Default: HD1200
        RES_NAME="HD1200 (1920x1200)"
        ;;
esac

echo "=============================================="
echo " ZED X One Hardware H.265/HEVC Encoding"
echo "=============================================="
echo " Camera: ZED X One (zedxonesrc)"
echo " Resolution: $RES_NAME"
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

# Check if zedxonesrc plugin is available
if ! gst-inspect-1.0 zedxonesrc > /dev/null 2>&1; then
    echo "ERROR: zedxonesrc plugin not found!"
    echo "Please build and install the zed-gstreamer plugin."
    exit 1
fi

# Check if this is a Jetson platform
warn_if_not_jetson

# Check if NV12 zero-copy is available for ZED X One
if gst-inspect-1.0 zedxonesrc 2>/dev/null | grep -q "Raw NV12 zero-copy"; then
    ZERO_COPY_AVAILABLE=true
    # stream-type=1 for NV12 zero-copy in zedxonesrc
    STREAM_TYPE=1
    echo "Using NV12 zero-copy mode (stream-type=$STREAM_TYPE)"
else
    ZERO_COPY_AVAILABLE=false
    # stream-type=0 for BGRA image
    STREAM_TYPE=0
    echo "Zero-copy not available, using BGRA mode (stream-type=$STREAM_TYPE)"
fi

# Build the pipeline based on zero-copy availability
if [ -n "$OUTPUT_FILE" ]; then
    # Recording pipeline
    # Check if we have a display for preview
    if [ -n "$DISPLAY" ] && xset q &>/dev/null 2>&1; then
        # Recording with preview (has display)
        if [ "$ZERO_COPY_AVAILABLE" = true ]; then
            # Zero-copy path - direct to encoder
            if [ "$DURATION" -gt 0 ]; then
                timeout "${DURATION}s" gst-launch-1.0 -e \
                    zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=$STREAM_TYPE ! \
                    tee name=t \
                    t. ! queue ! nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                        h265parse ! mp4mux ! filesink location="$OUTPUT_FILE" \
                    t. ! queue ! nv3dsink sync=false \
                    || true
            else
                gst-launch-1.0 -e \
                    zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=$STREAM_TYPE ! \
                    tee name=t \
                    t. ! queue ! nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                        h265parse ! mp4mux ! filesink location="$OUTPUT_FILE" \
                    t. ! queue ! nv3dsink sync=false
            fi
        else
            # BGRA: Need conversion to NVMM NV12
            if [ "$DURATION" -gt 0 ]; then
                timeout "${DURATION}s" gst-launch-1.0 -e \
                    zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=$STREAM_TYPE ! \
                    nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
                    tee name=t \
                    t. ! queue ! nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                        h265parse ! mp4mux ! filesink location="$OUTPUT_FILE" \
                    t. ! queue ! nv3dsink sync=false \
                    || true
            else
                gst-launch-1.0 -e \
                    zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=$STREAM_TYPE ! \
                    nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
                    tee name=t \
                    t. ! queue ! nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                        h265parse ! mp4mux ! filesink location="$OUTPUT_FILE" \
                    t. ! queue ! nv3dsink sync=false
            fi
        fi
    else
        # Recording only (no display / SSH session)
        echo "No display detected - recording without preview"
        if [ "$ZERO_COPY_AVAILABLE" = true ]; then
            # Zero-copy path
            if [ "$DURATION" -gt 0 ]; then
                timeout "${DURATION}s" gst-launch-1.0 -e \
                    zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=$STREAM_TYPE ! \
                    nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                    h265parse ! mp4mux ! filesink location="$OUTPUT_FILE" \
                    || true
            else
                gst-launch-1.0 -e \
                    zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=$STREAM_TYPE ! \
                    nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                    h265parse ! mp4mux ! filesink location="$OUTPUT_FILE"
            fi
        else
            # Need conversion to NVMM
            if [ "$DURATION" -gt 0 ]; then
                timeout "${DURATION}s" gst-launch-1.0 -e \
                    zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=$STREAM_TYPE ! \
                    nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
                    nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                    h265parse ! mp4mux ! filesink location="$OUTPUT_FILE" \
                    || true
            else
                gst-launch-1.0 -e \
                    zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=$STREAM_TYPE ! \
                    nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
                    nvv4l2h265enc bitrate=$BITRATE preset-level=1 ! \
                    h265parse ! mp4mux ! filesink location="$OUTPUT_FILE"
            fi
        fi
    fi
else
    # Preview only pipeline
    if [ "$ZERO_COPY_AVAILABLE" = true ]; then
        # Zero-copy: NVMM directly to nv3dsink
        gst-launch-1.0 \
            zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=$STREAM_TYPE ! \
            nv3dsink sync=false
    else
        # Need conversion to NVMM for display
        gst-launch-1.0 \
            zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=$STREAM_TYPE ! \
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
