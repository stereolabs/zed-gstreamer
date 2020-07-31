#!/bin/bash

# Example pipeline to start an RGB RTSP server

# install RTSP libraries to compile RTSP server:
# `sudo apt install libgstrtspserver-1.0-0 libgstrtspserver-1.0-dev`

./gst-rtsp-launch "( zedsrc resolution=2 ! videoconvert ! video/x-raw, format=(string)I420 ! x264enc byte-stream=false tune=zerolatency speed-preset=ultrafast bitrate=3000 qp-min=0 qp-min=0 qp-step=0 ! rtph264pay config-interval=-1 pt=96 name=pay0 )"


