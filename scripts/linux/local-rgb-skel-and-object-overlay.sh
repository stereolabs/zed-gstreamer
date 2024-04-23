#!/bin/bash -e

# Example pipeline to acquire a stream at default resolution with RGB, Object Detection, and Skeleton Tracking 
# information and displaying the results

# 1) Start `zedsrc` to acquire RGB enabling Skeleton Tracking and Object detection with default parameters.
# 2) Add detected skeletons and objects overlays to the frame
# 3) Convert the stream and display it with FPS information

gst-launch-1.0 \
zedsrc stream-type=0 od-enabled=true bt-enabled=true ! queue ! \
zedodoverlay ! queue ! \
autovideoconvert ! fpsdisplaysink
