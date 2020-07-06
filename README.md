# zed-gstreamer
GStreamer source plugin for ZED Cameras

## Example pipelines

### RGB stream + FPS rendering

`$  gst-launch-1.0 zedsrc ! autovideoconvert ! fpsdisplaysink`

### 16 bit Depth stream + FPS rendering

`$  gst-launch-1.0 zedsrc stream-type=1 ! autovideoconvert ! fpsdisplaysink`

### Left/Right stream + Demux + FPS rendering

`$ gst-launch-1.0 zedsrc stream-type=2 ! queue ! zeddemux is-depth=false name=demux demux.src_left ! queue ! autovideoconvert ! fpsdisplaysink  demux.src_aux ! queue ! autovideoconvert ! fpsdisplaysink`

### Left/Depth stream + Demux + FPS rendering

`$ gst-launch-1.0 zedsrc stream-type=4 ! queue ! zeddemux name=demux demux.src_left ! queue ! autovideoconvert ! fpsdisplaysink  demux.src_aux ! queue ! autovideoconvert ! fpsdisplaysink`
