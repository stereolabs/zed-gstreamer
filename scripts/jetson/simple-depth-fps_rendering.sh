#!/bin/bash -e

# Example pipeline to acquire a NEURAL depth stream and render it using
# hardware-accelerated display (nv3dsink) on Jetson.
# Depth stream is rendered as 16-bit greyscale, converted via GPU.

gst-launch-1.0 zedsrc stream-type=3 depth-mode=4 ! queue ! \
    nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! nv3dsink sync=false
