#!/bin/bash
# =============================================================================
# ZED Camera Stereo Recording Pipeline
# =============================================================================
# Records both left and right cameras in a side-by-side stereo format
# with hardware H.265 encoding.
#
# For GMSL cameras (ZED X, ZED X Mini) with SDK 5.2+:
#   - Uses zero-copy NV12 stereo for maximum performance (stream-type=7)
#
# For USB cameras or older SDK:
#   - Uses BGRA left+right demuxed with nvvideoconvert to NVMM NV12
#
# For Orin Nano (no NVENC):
#   - Falls back to software encoding (x265enc/x264enc)
#
# Usage:
#   ./hw_encode_stereo.sh [output_file] [duration_seconds] [fps] [bitrate]
#
# Examples:
#   ./hw_encode_stereo.sh                           # Default settings
#   ./hw_encode_stereo.sh stereo.mp4 60             # 60 second recording
#   ./hw_encode_stereo.sh stereo.mp4 0 60 20000000  # 60fps, 20Mbps
# =============================================================================

set -e

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

OUTPUT_FILE="${1:-stereo_output.mp4}"
DURATION="${2:-0}"
FPS="${3:-30}"
BITRATE="${4:-16000000}"  # 16 Mbps for stereo

echo "=============================================="
echo " ZED Stereo Recording"
echo "=============================================="
print_camera_info
echo " Encoder: $(get_encoder_info)"
echo " Output: $OUTPUT_FILE"
echo " Duration: ${DURATION}s (0=unlimited, Ctrl+C to stop)"
echo " FPS: $FPS"
echo " Bitrate: $((BITRATE / 1000000)) Mbps"
echo "=============================================="

# Check encoder availability
if ! has_hw_h265_encoder && ! has_hw_h264_encoder && ! has_sw_h265_encoder && ! has_sw_h264_encoder; then
    echo "ERROR: No video encoder available!"
    echo "Install software encoders: sudo apt install gstreamer1.0-plugins-ugly x265"
    exit 1
fi

if ! has_hw_h265_encoder && ! has_hw_h264_encoder; then
    echo "WARNING: Using software encoder - expect higher CPU usage"
fi

run_pipeline() {
    if has_hw_h265_encoder; then
        if is_zero_copy_available; then
            # Zero-copy NV12 stereo path (GMSL + SDK 5.2+)
            echo "Using zero-copy NV12 stereo mode with HW H.265..."
            gst-launch-1.0 -e \
                zedsrc camera-resolution=2 camera-fps=$FPS stream-type=7 ! \
                nvv4l2h265enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                h265parse ! mp4mux ! filesink location=$OUTPUT_FILE
        else
            # BGRA with conversion (USB cameras or older SDK)
            echo "Using BGRA mode with HW H.265..."
            gst-launch-1.0 -e \
                zedsrc camera-resolution=2 camera-fps=$FPS stream-type=2 ! \
                zeddemux stream-data=false name=demux \
                demux.src_left ! queue ! nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
                nvv4l2h265enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                h265parse ! mp4mux name=mux ! filesink location=$OUTPUT_FILE \
                demux.src_aux ! queue ! nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
                nvv4l2h265enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                h265parse ! mux.
        fi
    elif has_hw_h264_encoder; then
        if is_zero_copy_available; then
            echo "Using zero-copy NV12 stereo mode with HW H.264..."
            gst-launch-1.0 -e \
                zedsrc camera-resolution=2 camera-fps=$FPS stream-type=7 ! \
                nvv4l2h264enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                h264parse ! mp4mux ! filesink location=$OUTPUT_FILE
        else
            echo "Using BGRA mode with HW H.264..."
            gst-launch-1.0 -e \
                zedsrc camera-resolution=2 camera-fps=$FPS stream-type=2 ! \
                zeddemux stream-data=false name=demux \
                demux.src_left ! queue ! nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
                nvv4l2h264enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                h264parse ! mp4mux name=mux ! filesink location=$OUTPUT_FILE \
                demux.src_aux ! queue ! nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
                nvv4l2h264enc bitrate=$BITRATE preset-level=1 maxperf-enable=true ! \
                h264parse ! mux.
        fi
    elif has_sw_h265_encoder; then
        # Software H.265 (Orin Nano) - single stream, no stereo mux
        echo "Using software H.265 (stereo not supported, recording left only)..."
        gst-launch-1.0 -e \
            zedsrc camera-resolution=2 camera-fps=$FPS stream-type=0 ! \
            videoconvert ! \
            x265enc bitrate=$((BITRATE / 1000)) speed-preset=ultrafast tune=zerolatency ! \
            h265parse ! mp4mux ! filesink location=$OUTPUT_FILE
    elif has_sw_h264_encoder; then
        # Software H.264 - single stream
        echo "Using software H.264 (stereo not supported, recording left only)..."
        gst-launch-1.0 -e \
            zedsrc camera-resolution=2 camera-fps=$FPS stream-type=0 ! \
            videoconvert ! video/x-raw,format=I420 ! \
            x264enc bitrate=$((BITRATE / 1000)) speed-preset=ultrafast tune=zerolatency ! \
            h264parse ! mp4mux ! filesink location=$OUTPUT_FILE
    fi
}

if [ "$DURATION" -gt 0 ]; then
    timeout "${DURATION}s" bash -c "$(declare -f run_pipeline is_zero_copy_available detect_gmsl_camera zedsrc_supports_nv12 has_hw_h265_encoder has_hw_h264_encoder has_sw_h265_encoder has_sw_h264_encoder); FPS=$FPS BITRATE=$BITRATE OUTPUT_FILE=$OUTPUT_FILE run_pipeline" || true
else
    run_pipeline || true
fi

echo ""
echo "Stereo recording saved to: $OUTPUT_FILE"
if [ -f "$OUTPUT_FILE" ]; then
    ls -lh "$OUTPUT_FILE"
fi
