:: Example pipeline to acquire a stream and render it dispaying the current FPS using default values for each parameter

gst-launch-1.0 zedsrc ! queue ! autovideoconvert ! queue ! fpsdisplaysink
