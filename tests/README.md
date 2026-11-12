# Buffer and caps regression tests

Run `tests/run-regressions.sh` from a checkout with a C++ compiler and the GStreamer
video development package installed. These tests do not require the ZED SDK or a camera.

For AddressSanitizer, run:

```sh
CXXFLAGS='-g -fsanitize=address -fno-omit-frame-pointer' tests/run-regressions.sh
```

The tests exercise both metadata/video arrival orders, larger replacement frames,
metadata timestamps, downstream failures, input ownership, pending-buffer cleanup,
reduced/HDR caps, and odd-width depth conversion with padded output rows.

On a connected camera, additionally check odd-width source depth and reduced stereo
negotiation using the locally built plugins (adjust `camera-id` for the device):

```sh
gst-launch-1.0 zedsrc camera-id=0 stream-type=3 max-working-res-w=641 max-working-res-h=480 num-buffers=2 ! fakesink
gst-launch-1.0 zedsrc camera-id=0 stream-type=2 max-working-res-w=640 max-working-res-h=480 num-buffers=2 ! zeddemux name=d d.src_left ! queue ! fakesink d.src_aux ! queue ! fakesink
```
