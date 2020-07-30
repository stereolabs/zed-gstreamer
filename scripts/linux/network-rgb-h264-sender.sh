#!/bin/bash

# Example pipeline to acquire a stream and send it on local network with H264 encoding

# install x264enc: `sudo apt-get install gstreamer1.0-libav x264 gstreamer1.0-plugins-ugly`

#gst-launch-1.0 zedsrc ! queue ! autovideoconvert ! queue ! fpsdisplaysink

gst-launch-1.0 zedsrc resolution=2 ! autovideoconvert ! timeoverlay ! tee name=split ! \
 queue ! fpsdisplaysink \
 split. ! queue ! x264enc threads=4 tune=zerolatency ! h264parse ! \
 rtph264pay config-interval=1 pt=96 ! udpsink host=127.0.0.1 port=5000 sync=false async=false
