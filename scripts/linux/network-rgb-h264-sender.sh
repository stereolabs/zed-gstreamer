#!/bin/bash

# Example pipeline to acquire a stream and send it on local network with H264 encoding

# install x264enc: `sudo apt-get install gstreamer1.0-libav x264 gstreamer1.0-plugins-ugly`

#gst-launch-1.0 zedsrc ! queue ! autovideoconvert ! queue ! fpsdisplaysink

gst-launch-1.0 zedsrc resolution=1 framerate=30 ! queue ! timeoverlay ! identity drop-allocation=true ! tee name=split ! \
 queue ! videoconvert ! queue ! fpsdisplaysink \
 split. ! queue ! videoconvert ! x264enc byte-stream=true tune=zerolatency speed-preset=ultrafast bitrate=3000 qp-min=0 qp-min=0 qp-step=0 ! h264parse ! \
 rtph264pay config-interval=-1 pt=96 ! queue ! udpsink clients=192.168.0.50:5000,192.168.0.52:5000 max-bitrate=3000000 sync=false async=false
