#!/bin/bash

# Example pipeline to acquire a stream at default resolution with RGB and Skeleton Tracking information and 
# displaying the results using `zedoddisplaysink`


# 1) Start `zedsrc` to acquire RGB enabling Skeleton Tracking (FAST).
# 2) Display Object Detection result using the `zedoddisplaysink` sink plugin.

gst-launch-1.0 \
zedsrc stream-type=0 od-enabled=true od-detection-model=1 resolution=2 ! \
zedoddisplaysink
