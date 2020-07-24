#!/bin/bash

# Example pipeline to acquire a stream at 720p resolution with RGB, Depth and Object Detection information and 
# displaying the results using `zedoddisplaysink`

# 1) Start `zedsrc` to acquire RGB left and right streams
# 2) Demux the composite left/right stream
# 3) Render left stream with FPS info
# 4) Render right stream with FPS info

gst-launch-1.0 \
zedsrc stream-type=4 ! queue ! \
zeddemux name=demux \
demux.src_left ! queue ! autovideoconvert ! fpsdisplaysink \
demux.src_aux ! queue ! autovideoconvert ! fpsdisplaysink
