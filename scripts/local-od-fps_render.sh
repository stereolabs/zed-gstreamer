#!/bin/bash

# Example pipeline to acquire a stream at 720p resolution with RGB, Depth and Object Detection information and 
# displaying the data on screen

# 1) Start `zedsrc` to acquire RGB and depth at 720p resolution enabling Skeleton Tracking (FAST).
# 2) Define `zeddemux` object named `demux` to demux the `zedsrc` composite stream creating two single streams: RGB and Depth.
# 3) Rescale the Depth stream to VGA resolution and render it using `fpsdisplaysink`.
# 4) Display Object Detection result using the `zedoddisplaysink` sink plugin.
# 4) Display the depth stream with FPS info using the `fpsdisplaysink` sink plugin.

gst-launch-1.0 \
zedsrc stream-type=4 resolution=2 od-detection-model=1 od-enabled=true ! \
zeddemux name=demux \
demux.src_left ! queue ! zedoddisplaysink \
demux.src_aux ! queue ! autovideoconvert ! fpsdisplaysink
