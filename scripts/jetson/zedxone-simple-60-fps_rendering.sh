#!/bin/bash -e

# Example pipeline to acquire a ZED X One 1200p stream at 60 FPS and render it
# using hardware-accelerated display (nv3dsink) on Jetson.
# Uses NV12 zero-copy (stream-type=1) for optimal performance.

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

if zedxonesrc_supports_nv12; then
    gst-launch-1.0 zedxonesrc stream-type=1 camera-resolution=2 camera-fps=60 ! queue ! nv3dsink sync=false
else
    gst-launch-1.0 zedxonesrc stream-type=0 camera-resolution=2 camera-fps=60 ! queue ! nvvideoconvert ! \
        "video/x-raw(memory:NVMM),format=NV12" ! nv3dsink sync=false
fi
