#!/bin/bash -e

# Example pipeline to acquire a stream at default resolution with RGB Left and Right 
# side-by-side information and displaying the results on screen with FPS information

# 1) Start `zedsrc` to acquire RGB left and right side-by-side stream (stream-type=5)
# 2) Display the SBS stream with FPS info
# Note: stream-type=5 is LEFT_RIGHT_SBS (BGRA side-by-side)

gst-launch-1.0 \
zedsrc stream-type=5 ! queue ! autovideoconvert ! fpsdisplaysink
