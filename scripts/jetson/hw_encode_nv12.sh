#!/bin/bash
# =============================================================================
# ZED Camera Hardware Encoding Pipeline
# =============================================================================
# Records or previews ZED camera feed with hardware H.265 encoding.
#
# For GMSL cameras (ZED X, ZED X Mini) with SDK 5.2+:
#   - Uses zero-copy NV12 for maximum performance
#
# For USB cameras or older SDK:
#   - Uses BGRA with $NVVIDCONV to NVMM
#
# For Orin Nano (no NVENC):
#   - Falls back to software encoding (x265enc/x264enc)
#
# Usage:
#   ./hw_encode_nv12.sh [output_file] [duration_seconds] [resolution] [fps] [bitrate]
#
# Examples:
#   ./hw_encode_nv12.sh                          # Preview only
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
DURATION="${2:-0}"
RESOLUTION="${3:-1200}"
FPS="${4:-30}"
BITRATE="${5:-8000000}"

# Auto-set output file for headless systems
if [ -z "$OUTPUT_FILE" ] && [ -z "$DISPLAY" ]; then
    OUTPUT_FILE="output.mp4"
    echo "Info: No display detected, defaulting to recording: $OUTPUT_FILE"
fi

# Map resolution to zedsrc resolution property
case "$RESOLUTION" in
    2K|2k|2208) RES_PROP=0 ;;
    1080|1920)  RES_PROP=1 ;;
    1200)       RES_PROP=2 ;;
    720)        RES_PROP=3 ;;
    SVGA|svga|600) RES_PROP=4 ;;
    VGA|vga|376)   RES_PROP=5 ;;
    *)          RES_PROP=6 ;;  # AUTO
esac

echo "=============================================="
echo " ZED Recording Pipeline"
echo "=============================================="
print_camera_info
echo " Encoder: $(get_encoder_info)"
echo " Resolution: $RESOLUTION (prop=$RES_PROP)"
echo " FPS: $FPS"
echo " Bitrate: $((BITRATE / 1000000)) Mbps"
if [ -n "$OUTPUT_FILE" ]; then
    echo " Output: $OUTPUT_FILE"
    [ "$DURATION" -gt 0 ] && echo " Duration: ${DURATION}s" || echo " Duration: Until Ctrl+C"
else
    echo " Mode: Preview only"
fi
echo "=============================================="

# Check for zedsrc
if ! gst-inspect-1.0 zedsrc > /dev/null 2>&1; then
    echo "ERROR: zedsrc plugin not found!"
    exit 1
fi

warn_if_not_jetson

# Check encoder availability
if ! has_hw_h265_encoder && ! has_hw_h264_encoder && ! has_sw_h265_encoder && ! has_sw_h264_encoder; then
    echo "ERROR: No video encoder available!"
    echo "Install software encoders: sudo apt install gstreamer1.0-plugins-ugly x265"
    exit 1
fi

# Determine encoder type
USE_SW_ENCODER=false
if ! has_hw_h265_encoder && ! has_hw_h264_encoder; then
    USE_SW_ENCODER=true
    echo "WARNING: Using software encoder - expect higher CPU usage"
fi

# Run the appropriate pipeline
run_pipeline() {
    local duration_opt=""
    [ "$DURATION" -gt 0 ] && duration_opt="timeout ${DURATION}s"
    
    if [ -z "$OUTPUT_FILE" ]; then
        # Preview only
        if is_zero_copy_available; then
            gst-launch-1.0 zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=6 ! \
                nv3dsink sync=false
        else
            gst-launch-1.0 zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
                $NVVIDCONV ! "video/x-raw(memory:NVMM),format=NV12" ! nv3dsink sync=false
        fi
        return
    fi
    
    # Recording pipelines
    if has_hw_h265_encoder; then
        # Hardware H.265 encoder
        if is_zero_copy_available; then
            # Zero-copy NV12 path
            if [ -n "$DISPLAY" ] && xset q &>/dev/null 2>&1; then
                $duration_opt gst-launch-1.0 -e \
                    zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=6 ! \
                    tee name=t \
                    t. ! queue ! nvv4l2h265enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                        h265parse ! mp4mux ! filesink location=$OUTPUT_FILE \
                    t. ! queue ! nv3dsink sync=false
            else
                $duration_opt gst-launch-1.0 -e \
                    zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=6 ! \
                    nvv4l2h265enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                    h265parse ! mp4mux ! filesink location=$OUTPUT_FILE
            fi
        else
            # BGRA with conversion
            if [ -n "$DISPLAY" ] && xset q &>/dev/null 2>&1; then
                $duration_opt gst-launch-1.0 -e \
                    zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
                    $NVVIDCONV ! "video/x-raw(memory:NVMM),format=NV12" ! \
                    tee name=t \
                    t. ! queue ! nvv4l2h265enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                        h265parse ! mp4mux ! filesink location=$OUTPUT_FILE \
                    t. ! queue ! nv3dsink sync=false
            else
                $duration_opt gst-launch-1.0 -e \
                    zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
                    $NVVIDCONV ! "video/x-raw(memory:NVMM),format=NV12" ! \
                    nvv4l2h265enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                    h265parse ! mp4mux ! filesink location=$OUTPUT_FILE
            fi
        fi
    elif has_hw_h264_encoder; then
        # Hardware H.264 encoder
        if is_zero_copy_available; then
            $duration_opt gst-launch-1.0 -e \
                zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=6 ! \
                nvv4l2h264enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                h264parse ! mp4mux ! filesink location=$OUTPUT_FILE
        else
            $duration_opt gst-launch-1.0 -e \
                zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
                $NVVIDCONV ! "video/x-raw(memory:NVMM),format=NV12" ! \
                nvv4l2h264enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                h264parse ! mp4mux ! filesink location=$OUTPUT_FILE
        fi
    elif has_sw_h265_encoder; then
        # Software H.265 (Orin Nano)
        $duration_opt gst-launch-1.0 -e \
            zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
            videoconvert ! video/x-raw,format=I420 ! \
            x265enc bitrate=$((BITRATE / 1000)) speed-preset=ultrafast tune=zerolatency ! \
            h265parse ! mp4mux ! filesink location=$OUTPUT_FILE
    elif has_sw_h264_encoder; then
        # Software H.264
        $duration_opt gst-launch-1.0 -e \
            zedsrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
            videoconvert ! video/x-raw,format=I420 ! \
            x264enc bitrate=$((BITRATE / 1000)) speed-preset=ultrafast tune=zerolatency ! \
            h264parse ! mp4mux ! filesink location=$OUTPUT_FILE
    fi
}

run_pipeline || true

echo ""
echo "Pipeline completed."
if [ -n "$OUTPUT_FILE" ] && [ -f "$OUTPUT_FILE" ]; then
    echo "Output saved to: $OUTPUT_FILE"
    ls -lh "$OUTPUT_FILE"
fi
