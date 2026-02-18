#!/bin/bash
# =============================================================================
# ZED GStreamer Common Functions
# =============================================================================
# This file provides common utility functions for ZED GStreamer scripts.
# Source this file in your script: source "$(dirname "$0")/common.sh"
#
# Key features:
#   - Camera type detection (GMSL vs USB)
#   - Automatic stream-type selection based on camera type
#   - Pipeline building helpers that work for both camera types
# =============================================================================

# --- Auto-detect nvvidconv vs nvvideoconvert ---
# JP5 (L4T R35.x) has "nvvidconv", JP6+ (L4T R36.x) has "nvvideoconvert"
get_nvvidconv_element() {
    if gst-inspect-1.0 nvvideoconvert > /dev/null 2>&1; then
        echo "nvvideoconvert"
    elif gst-inspect-1.0 nvvidconv > /dev/null 2>&1; then
        echo "nvvidconv"
    else
        echo ""
    fi
}
NVVIDCONV=$(get_nvvidconv_element)

# --- Ensure DISPLAY is set for EGL-CUDA interop ---
if [ -z "$DISPLAY" ]; then
    export DISPLAY=:0
fi

# Check if zedsrc plugin supports NV12 zero-copy (stream-type=6)
# This requires ZED SDK 5.2+ with Advanced Capture API compiled in
zedsrc_supports_nv12() {
    # Check if stream-type=6 (NV12) is available in zedsrc
    if gst-inspect-1.0 zedsrc 2>/dev/null | grep -q "Raw NV12 zero-copy"; then
        return 0
    fi
    return 1
}

# Check if zedxonesrc plugin supports NV12 zero-copy (stream-type=1)
zedxonesrc_supports_nv12() {
    if gst-inspect-1.0 zedxonesrc 2>/dev/null | grep -q "NV12 zero-copy"; then
        return 0
    fi
    return 1
}

# Detect if a GMSL camera (ZED X / ZED X Mini) is available
# Returns 0 if GMSL camera is detected, 1 if USB camera or no camera
detect_gmsl_camera() {
    # Method 1: Check via ZED SDK's detection (preferred)
    # The SDK would have created device nodes for GMSL cameras under /dev/video*
    # that are associated with the NVIDIA Tegra VI (Video Input) driver
    
    if [ -d "/sys/class/video4linux" ]; then
        for dev in /sys/class/video4linux/video*; do
            if [ -L "$dev/device/driver" ]; then
                driver=$(readlink -f "$dev/device/driver" | xargs basename)
                if [[ "$driver" == *"tegra"* ]] || [[ "$driver" == *"vi"* ]] || [[ "$driver" == *"argus"* ]]; then
                    return 0  # GMSL camera found
                fi
            fi
        done
    fi
    
    # Method 2: Check if any device reports as GMSL via v4l2
    if command -v v4l2-ctl &> /dev/null; then
        for dev in /dev/video*; do
            if [ -e "$dev" ]; then
                card=$(v4l2-ctl -d "$dev" --info 2>/dev/null | grep "Card type" | cut -d':' -f2)
                if echo "$card" | grep -qi "zed.*x\|gmsl\|vi-output\|argus"; then
                    return 0  # GMSL camera found
                fi
            fi
        done
    fi
    
    # Method 3: Check for USB ZED cameras via lsusb
    if lsusb 2>/dev/null | grep -qi "2b03:"; then
        return 1  # USB camera found
    fi
    
    # Default: assume USB camera (safer fallback)
    return 1
}

# Check if USB ZED camera is present
detect_usb_camera() {
    if lsusb 2>/dev/null | grep -qi "2b03:\|stereolabs"; then
        return 0
    fi
    return 1
}

# Get the appropriate stream type for the connected camera
# Returns: stream-type value (0 for USB/BGRA, 6 for GMSL/NV12)
# Zero-copy NV12 requires:
#   1. GMSL camera (ZED X / ZED X Mini)
#   2. ZED SDK 5.2+ with Advanced Capture API
#   3. Plugin compiled with SL_ENABLE_ADVANCED_CAPTURE_API
get_optimal_stream_type() {
    if detect_gmsl_camera && zedsrc_supports_nv12; then
        echo "6"  # Zero-copy NV12 for GMSL cameras with SDK support
    else
        echo "0"  # BGRA for USB cameras or older SDK
    fi
}

# Get camera type as a string
get_camera_type() {
    if detect_gmsl_camera; then
        echo "GMSL"
    elif detect_usb_camera; then
        echo "USB"
    else
        echo "UNKNOWN"
    fi
}

# Check if zero-copy is available (GMSL camera + SDK support)
is_zero_copy_available() {
    if detect_gmsl_camera && zedsrc_supports_nv12; then
        return 0
    fi
    return 1
}

# Print camera information banner
print_camera_info() {
    local camera_type=$(get_camera_type)
    local stream_type=$(get_optimal_stream_type)
    
    echo " Camera Type: $camera_type"
    if [ "$stream_type" = "6" ]; then
        echo " Stream Mode: Zero-copy NV12 (stream-type=$stream_type)"
    else
        echo " Stream Mode: BGRA with conversion (stream-type=$stream_type)"
        if [ "$camera_type" = "GMSL" ]; then
            echo " Note: Zero-copy NV12 not available (requires ZED SDK 5.2+)"
        else
            echo " Note: USB cameras don't support zero-copy NV12"
        fi
    fi
}

# Build a zedsrc element string with appropriate settings
# Arguments: resolution fps [additional_props]
# Example: build_zedsrc_element 2 30 "camera-id=0"
build_zedsrc_element() {
    local resolution="${1:-6}"  # Default: Auto
    local fps="${2:-30}"
    local extra_props="${3:-}"
    local stream_type=$(get_optimal_stream_type)
    
    local element="zedsrc stream-type=$stream_type camera-resolution=$resolution camera-fps=$fps"
    if [ -n "$extra_props" ]; then
        element="$element $extra_props"
    fi
    echo "$element"
}

# Build video caps filter based on camera type
# For GMSL: video/x-raw(memory:NVMM),format=NV12
# For USB: video/x-raw,format=BGRA (needs conversion for NVMM pipeline)
build_video_caps() {
    if detect_gmsl_camera; then
        echo '"video/x-raw(memory:NVMM),format=NV12"'
    else
        echo '"video/x-raw,format=BGRA"'
    fi
}

# Build a conversion element for USB cameras to get to NVMM
# For GMSL: Returns empty (no conversion needed)
# For USB: Returns the conversion pipeline to get to NVMM NV12
build_nvmm_converter() {
    if detect_gmsl_camera; then
        echo ""  # GMSL already outputs NVMM NV12
    else
        # USB cameras output BGRA on system memory, need to convert to NVMM NV12
        echo "! $NVVIDCONV ! 'video/x-raw(memory:NVMM),format=NV12'"
    fi
}

# Build a complete encoding pipeline source section
# This handles both GMSL (zero-copy) and USB (with conversion) cameras
# Arguments: resolution fps
build_encode_source() {
    local resolution="${1:-6}"
    local fps="${2:-30}"
    
    if detect_gmsl_camera; then
        # GMSL: Direct zero-copy path
        echo "zedsrc stream-type=6 camera-resolution=$resolution camera-fps=$fps"
    else
        # USB: Need conversion to NVMM
        echo "zedsrc stream-type=0 camera-resolution=$resolution camera-fps=$fps ! $NVVIDCONV ! 'video/x-raw(memory:NVMM),format=NV12'"
    fi
}

# Check if we're running on a Jetson platform
is_jetson() {
    if [ -f /etc/nv_tegra_release ]; then
        return 0
    fi
    return 1
}

# Print a warning if not on Jetson
warn_if_not_jetson() {
    if ! is_jetson; then
        echo "WARNING: This script is designed for NVIDIA Jetson platforms."
        echo "         Hardware acceleration may not be available."
        echo ""
    fi
}

# Check required GStreamer plugins
check_gst_plugin() {
    local plugin="$1"
    if ! gst-inspect-1.0 "$plugin" > /dev/null 2>&1; then
        echo "ERROR: GStreamer plugin '$plugin' not found!"
        return 1
    fi
    return 0
}

# Check if nvvidconv/nvvideoconvert is available
has_nvvidconv() {
    [ -n "$NVVIDCONV" ]
}

# Check if hardware H.265 encoder is available
# Orin Nano only has NVDEC (decoder), no NVENC (encoder)
has_hw_h265_encoder() {
    if gst-inspect-1.0 nvv4l2h265enc > /dev/null 2>&1 && [ -n "$NVVIDCONV" ]; then
        # Plugin exists, but check if it actually works (Orin Nano has the plugin but no HW)
        # Try a quick encode test using the correct nvvidconv element for this platform
        if timeout 2 gst-launch-1.0 videotestsrc num-buffers=1 ! \
            "video/x-raw,width=320,height=240" ! $NVVIDCONV ! \
            "video/x-raw(memory:NVMM),format=NV12" ! nvv4l2h265enc ! \
            fakesink > /dev/null 2>&1; then
            return 0
        fi
    fi
    return 1
}

# Check if hardware H.264 encoder is available
has_hw_h264_encoder() {
    if gst-inspect-1.0 nvv4l2h264enc > /dev/null 2>&1 && [ -n "$NVVIDCONV" ]; then
        if timeout 2 gst-launch-1.0 videotestsrc num-buffers=1 ! \
            "video/x-raw,width=320,height=240" ! $NVVIDCONV ! \
            "video/x-raw(memory:NVMM),format=NV12" ! nvv4l2h264enc ! \
            fakesink > /dev/null 2>&1; then
            return 0
        fi
    fi
    return 1
}

# Check if software x265 encoder is available
has_sw_h265_encoder() {
    gst-inspect-1.0 x265enc > /dev/null 2>&1
}

# Check if software x264 encoder is available
has_sw_h264_encoder() {
    gst-inspect-1.0 x264enc > /dev/null 2>&1
}

# Get encoder info string for display
get_encoder_info() {
    if has_hw_h265_encoder; then
        echo "Hardware H.265 (nvv4l2h265enc)"
    elif has_hw_h264_encoder; then
        echo "Hardware H.264 (nvv4l2h264enc)"
    elif has_sw_h265_encoder; then
        echo "Software H.265 (x265enc) - slower"
    elif has_sw_h264_encoder; then
        echo "Software H.264 (x264enc) - slower"
    else
        echo "No encoder available!"
    fi
}

# Validate all required plugins for encoding pipelines
check_encoding_plugins() {
    local missing=0
    
    if ! check_gst_plugin zedsrc; then
        echo "       Please build and install the zed-gstreamer plugin."
        missing=1
    fi
    
    if ! has_nvvidconv; then
        echo "       NVIDIA GStreamer plugins (nvvidconv/nvvideoconvert) not found."
        missing=1
    fi
    
    # Check for at least one encoder
    if ! has_hw_h265_encoder && ! has_hw_h264_encoder && ! has_sw_h265_encoder && ! has_sw_h264_encoder; then
        echo "       No video encoder available!"
        echo "       For software encoding: sudo apt install gstreamer1.0-plugins-ugly"
        missing=1
    fi
    
    return $missing
}
