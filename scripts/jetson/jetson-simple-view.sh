#!/bin/bash -e
# =============================================================================
# ZED Simple View (Jetson Optimized)
# =============================================================================
# Displays ZED camera feed using hardware-accelerated rendering.
# Automatically uses NV12 zero-copy for GMSL cameras with SDK 5.2+.
#
# Usage:
#   ./jetson-simple-view.sh
# =============================================================================

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

print_camera_info

if is_zero_copy_available; then
    # Zero-copy NV12 path - direct to display
    gst-launch-1.0 zedsrc stream-type=6 ! queue ! nv3dsink sync=false
else
    # BGRA path with conversion to NVMM for display
    gst-launch-1.0 zedsrc stream-type=0 ! queue ! $NVVIDCONV ! \
        "video/x-raw(memory:NVMM),format=NV12" ! nv3dsink sync=false
fi