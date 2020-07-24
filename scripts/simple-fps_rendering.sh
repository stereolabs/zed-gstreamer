#!/bin/bash

# Example pipeline to acquire a stream and render it dispaying the current FPS. All default values for each parameter are used

gst-launch-1.0 zedsrc ! autovideoconvert ! fpsdisplaysink
