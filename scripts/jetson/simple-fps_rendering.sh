#!/bin/bash -e

# Example pipeline to acquire a ZED stream and render it using
# hardware-accelerated display (nv3dsink) on Jetson.
# Uses NV12 zero-copy for GMSL cameras (ZED X / ZED X Mini) when SDK 5.2+
# is available, otherwise falls back to BGRA with GPU conversion.

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

if is_zero_copy_available; then
    gst-launch-1.0 zedsrc stream-type=6 ! queue ! nv3dsink sync=false
else
    gst-launch-1.0 zedsrc stream-type=0 ! queue ! nvvideoconvert ! \
        "video/x-raw(memory:NVMM),format=NV12" ! nv3dsink sync=false
fi
