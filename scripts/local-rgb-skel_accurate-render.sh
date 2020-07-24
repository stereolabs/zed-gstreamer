#!/bin/bash

# Example pipeline to acquire a stream at 720p resolution with RGB, Depth and Object Detection information and 
# displaying the results using `zedoddisplaysink`

# 1) Start `zedsrc` to acquire RGB enabling Skeleton Tracking (ACCURATE).
# 2) Display Object Detection result using the `zedoddisplaysink` sink plugin.

gst-launch-1.0 \
zedsrc stream-type=0 od-enabled=true od-detection-model=2 resolution=2 ! \
zedoddisplaysink
