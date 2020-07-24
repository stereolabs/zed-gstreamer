#!/bin/bash

# Example pipeline to acquire a depth stream and render it dispaying the current FPS. All default values for each parameter are used

gst-launch-1.0 zedsrc stream-type=3 ! autovideoconvert ! fpsdisplaysink
