#!/bin/bash
# =============================================================================
# ZED X One Hardware Encoding Pipeline
# =============================================================================
# Records or previews ZED X One camera feed with hardware H.265 encoding.
#
# For ZED X One with SDK 5.2+:
#   - Uses zero-copy NV12 for maximum performance (stream-type=1)
#
# For older SDK:
#   - Uses BGRA with nvvideoconvert to NVMM (stream-type=0)
#
# For Orin Nano (no NVENC):
#   - Falls back to software encoding (x265enc/x264enc)
#
# Usage:
#   ./hw_encode_nv12_zedone.sh [OPTIONS] [output_file] [duration_seconds]
#
# Options:
#   --resolution RES    Camera resolution (4K, QHDPLUS, 1200, 1080, SVGA)
#   --fps FPS           Frame rate (15, 30, 60, 120 for SVGA)
#   --bitrate BITRATE   Encoding bitrate in bps (default: 8000000)
#
# Examples:
#   ./hw_encode_nv12_zedone.sh                              # Preview only
#   ./hw_encode_nv12_zedone.sh output.mp4                   # Record to file
#   ./hw_encode_nv12_zedone.sh output.mp4 60                # Record 60 seconds
#   ./hw_encode_nv12_zedone.sh --resolution 4K output.mp4   # 4K recording
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
BITRATE="8000000"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --resolution) RESOLUTION="$2"; shift 2 ;;
        --fps) FPS="$2"; shift 2 ;;
        --bitrate) BITRATE="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS] [output_file] [duration_seconds]"
            echo "Options: --resolution, --fps, --bitrate"
            exit 0
            ;;
        *)
            if [ -z "$OUTPUT_FILE" ]; then OUTPUT_FILE="$1"
            elif [ "$DURATION" -eq 0 ]; then DURATION="$1"
            fi
            shift
            ;;
    esac
done

# Auto-set output for headless
if [ -z "$OUTPUT_FILE" ] && [ -z "$DISPLAY" ]; then
    OUTPUT_FILE="output_zedone.mp4"
    echo "Info: No display detected, defaulting to: $OUTPUT_FILE"
fi

# Map resolution
case "$RESOLUTION" in
    4K|4k|3840|2160) RES_PROP=4; RES_NAME="4K (3840x2160)" ;;
    QHDPLUS|qhdplus|3200|1800) RES_PROP=3; RES_NAME="QHD+ (3200x1800)" ;;
    1200|HD1200) RES_PROP=2; RES_NAME="HD1200 (1920x1200)" ;;
    1080|HD1080) RES_PROP=1; RES_NAME="HD1080 (1920x1080)" ;;
    SVGA|svga|600) RES_PROP=0; RES_NAME="SVGA (960x600)" ;;
    *) RES_PROP=2; RES_NAME="HD1200 (1920x1200)" ;;
esac

echo "=============================================="
echo " ZED X One Recording Pipeline"
echo "=============================================="
echo " Encoder: $(get_encoder_info)"
echo " Resolution: $RES_NAME"
echo " FPS: $FPS"
echo " Bitrate: $((BITRATE / 1000000)) Mbps"
if [ -n "$OUTPUT_FILE" ]; then
    echo " Output: $OUTPUT_FILE"
    [ "$DURATION" -gt 0 ] && echo " Duration: ${DURATION}s" || echo " Duration: Until Ctrl+C"
else
    echo " Mode: Preview only"
fi
echo "=============================================="

# Check for zedxonesrc
if ! gst-inspect-1.0 zedxonesrc > /dev/null 2>&1; then
    echo "ERROR: zedxonesrc plugin not found!"
    exit 1
fi

warn_if_not_jetson

# Check encoder availability
if ! has_hw_h265_encoder && ! has_hw_h264_encoder && ! has_sw_h265_encoder && ! has_sw_h264_encoder; then
    echo "ERROR: No video encoder available!"
    echo "Install software encoders: sudo apt install gstreamer1.0-plugins-ugly x265"
    exit 1
fi

# Check for zero-copy support in zedxonesrc
ZERO_COPY_AVAILABLE=false
if gst-inspect-1.0 zedxonesrc 2>/dev/null | grep -q "Raw NV12 zero-copy"; then
    ZERO_COPY_AVAILABLE=true
    STREAM_TYPE=1
    echo "Using NV12 zero-copy mode (stream-type=1)"
else
    STREAM_TYPE=0
    echo "Using BGRA mode (stream-type=0)"
fi

# Determine if using software encoder
if ! has_hw_h265_encoder && ! has_hw_h264_encoder; then
    echo "WARNING: Using software encoder - expect higher CPU usage"
fi

# Run the appropriate pipeline
run_pipeline() {
    if [ -z "$OUTPUT_FILE" ]; then
        # Preview only
        if [ "$ZERO_COPY_AVAILABLE" = true ]; then
            gst-launch-1.0 zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=1 ! \
                nv3dsink sync=false
        else
            gst-launch-1.0 zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
                nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! nv3dsink sync=false
        fi
        return
    fi
    
    # Recording pipelines
    if has_hw_h265_encoder; then
        # Hardware H.265
        if [ "$ZERO_COPY_AVAILABLE" = true ]; then
            if [ -n "$DISPLAY" ] && xset q &>/dev/null 2>&1; then
                gst-launch-1.0 -e \
                    zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=1 ! \
                    tee name=t \
                    t. ! queue ! nvv4l2h265enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                        h265parse ! mp4mux ! filesink location="$OUTPUT_FILE" \
                    t. ! queue ! nv3dsink sync=false
            else
                gst-launch-1.0 -e \
                    zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=1 ! \
                    nvv4l2h265enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                    h265parse ! mp4mux ! filesink location="$OUTPUT_FILE"
            fi
        else
            if [ -n "$DISPLAY" ] && xset q &>/dev/null 2>&1; then
                gst-launch-1.0 -e \
                    zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
                    nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
                    tee name=t \
                    t. ! queue ! nvv4l2h265enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                        h265parse ! mp4mux ! filesink location="$OUTPUT_FILE" \
                    t. ! queue ! nv3dsink sync=false
            else
                gst-launch-1.0 -e \
                    zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
                    nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
                    nvv4l2h265enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                    h265parse ! mp4mux ! filesink location="$OUTPUT_FILE"
            fi
        fi
    elif has_hw_h264_encoder; then
        # Hardware H.264
        if [ "$ZERO_COPY_AVAILABLE" = true ]; then
            gst-launch-1.0 -e \
                zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=1 ! \
                nvv4l2h264enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                h264parse ! mp4mux ! filesink location="$OUTPUT_FILE"
        else
            gst-launch-1.0 -e \
                zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
                nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
                nvv4l2h264enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                h264parse ! mp4mux ! filesink location="$OUTPUT_FILE"
        fi
    elif has_sw_h265_encoder; then
        # Software H.265 (Orin Nano)
        gst-launch-1.0 -e \
            zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
            videoconvert ! video/x-raw,format=I420 ! \
            x265enc bitrate=$((BITRATE / 1000)) speed-preset=ultrafast tune=zerolatency ! \
            h265parse ! mp4mux ! filesink location="$OUTPUT_FILE"
    elif has_sw_h264_encoder; then
        # Software H.264
        gst-launch-1.0 -e \
            zedxonesrc camera-resolution=$RES_PROP camera-fps=$FPS stream-type=0 ! \
            videoconvert ! video/x-raw,format=I420 ! \
            x264enc bitrate=$((BITRATE / 1000)) speed-preset=ultrafast tune=zerolatency ! \
            h264parse ! mp4mux ! filesink location="$OUTPUT_FILE"
    fi
}

if [ "$DURATION" -gt 0 ]; then
    timeout "${DURATION}s" bash -c "$(declare -f run_pipeline has_hw_h265_encoder has_hw_h264_encoder has_sw_h265_encoder has_sw_h264_encoder); RES_PROP=$RES_PROP FPS=$FPS BITRATE=$BITRATE OUTPUT_FILE='$OUTPUT_FILE' ZERO_COPY_AVAILABLE=$ZERO_COPY_AVAILABLE DISPLAY='$DISPLAY' run_pipeline" || true
else
    run_pipeline || true
fi

echo ""
echo "Pipeline completed."
if [ -n "$OUTPUT_FILE" ] && [ -f "$OUTPUT_FILE" ]; then
    echo "Output saved to: $OUTPUT_FILE"
    ls -lh "$OUTPUT_FILE"
fi
