<h1 align="center">
  ZED GStreamer plugins
</h1>

<h4 align="center">GStreamer plugins for the ZED stereo camera family</h4>

<p align="center">
  <a href="#key-features">Key Features</a> •
  <a href="#build">Build</a> •
  <a href="#plugins-parameters">Plugins parameters</a> • 
  <a href="#example-pipelines">Example pipelines</a> • 
  <a href="#related">Related</a> •
  <a href="#license">License</a>
</p>
<br>

# Key Features
GStreamer plugin package for ZED Cameras. The package is composed of two plugins:

* `zedsrc`: acquires camera color image and depth map and pushes them in a GStreamer pipeline.
* `zedmeta`: GStreamer library to define and handle the ZED metadata (Positional Tracking data, Sensors data, Detected Object data, Detected Skeletons data).
* `zeddemux`: receives a composite `zedsrc` stream (`color left + color right` data or `color left + depth map` + metadata), 
  processes the eventual depth data and pushes them in two separated new streams named `src_left` and `src_aux`. A third source pad is created for metadata to be externally processed.
* `zeddatacsvsink`: example sink plugin that receives ZED metadata, extracts the Positional Tracking and the Sensors Data and save them in a CSV file.
* `zedoddisplaysink`: example sink plugin that receives ZED combined stream with metadata, extracts Object Detection process result and displays it in a 2D window.

## Build

### Prerequisites

 * Stereo camera: [ZED 2](https://www.stereolabs.com/zed-2/), [ZED](https://www.stereolabs.com/zed/), [ZED Mini](https://www.stereolabs.com/zed-mini/)
 * CMake (v3.1+)
 * GStreamer

### GStreamer Installation
 * Follow the installation guide for GStreamer [here](https://gstreamer.freedesktop.org/documentation/installing/index.html?gi-language=c)
 To build on Linux, you also need to install the dev package with :
 `$ sudo apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev`

### Windows installation

 * Install the latest ZED SDK from the [official download page](https://www.stereolabs.com/developers/release/)
 * Install [Git](https://git-scm.com/) or download a ZIP archive
 * Install [CMake](https://cmake.org/)
 * Install [GStreamer distribution](https://gstreamer.freedesktop.org/download/)
  or build from source. The installer should set
  the installation path via the `GSTREAMER_1_0_ROOT_X86_64` environment variable. If
  not set, set the CMake variable `GSTREAMER_ROOT` to your installation, the directory
  containing `bin` and `lib`
 * Install any camera or framegrabber software/SDK for those plugins you wish to
  build. Check `cmake/modules` for any paths you may need to set.
 * Run the following commands from a terminal or command prompt, assuming CMake
  and Git are in your `PATH`.

     ```
     git clone https://github.com/stereolabs/zed-gstreamer.git
     cd zed-gstreamer
     mkdir build
     cd build
     cmake -G "Visual Studio 15 2017 Win64" ..
     ```

To install the plugins, first make sure you've set `CMAKE_INSTALL_PREFIX` properly,
the default might not be desired (e.g., system path). For finer grained control
you can set `PLUGIN_INSTALL_DIR` and related variables to specify exactly where
you want to install plugins

    cmake --build . --target INSTALL

### Linux installation

#### Install prerequisites

* Install the latest ZED SDK from the [official download page](https://www.stereolabs.com/developers/release/)

* Install GCC compiler and build tools

     `$ sudo apt install build-essential`

* Install CMake build system

     `$ sudo apt install cmake`

#### Clone the repository

    $ git clone https://github.com/stereolabs/zed-gstreamer.git
    $ cd zed-gstreamer

#### Build

    $ mkdir build
    $ cd build
    $ cmake -DCMAKE_BUILD_TYPE=Release ..
    $ make
    $ sudo make install 


### Installation test

 * Check `ZED Video Source Plugin` installation inspecting its properties:

      `gst-inspect-1.0 zedsrc`

 * Check `ZED Video Demuxer` installation inspecting its properties:

      `gst-inspect-1.0 zeddemux`

 * Check `ZED CSV Sink Plugin` installation inspecting its properties:

      `gst-inspect-1.0 zeddatacsvsink`

 * Check `ZED Object Detection Display Sink Plugin` installation inspecting its properties:

      `gst-inspect-1.0 zedoddisplaysink`

## Plugins parameters

### `ZED Video Source Plugin` parameters

 * `resolution`: stream resolution - {VGA (3), HD270 (2), HD1080 (1), HD2K (0)}
 * `framerate`: stream framerate - {15, 30, 60, 100}
 * `verbose`: SDK verbose mode - {TRUE, FALSE}
 * `flip`: flip streams vertically - {TRUE, FALSE}
 * `camera-id`: camera ID - [0, 256]
 * `camera-sn`: camera serial number
 * `svo-file-path`: SVO file path for SVO input
 * `stream-ip-addr`: device IP address for remote input
 * `stream-port`: IP port for remote input
 * `stream-type`: type of video stream - {Left image (0), Right image (1), Stereo couple (2), 16 bit depth (3), Left+Depth (4)}
 * `min-depth`: Minimum depth value
 * `max-depth `: Maximum depth value
 * `disable-self-calib`: Disable the self calibration processing when the camera is opened - {TRUE, FALSE}
 * `depth-stability`: Enable depth stabilization - {TRUE, FALSE} 
 * `pos-tracking`: Enable positional tracking - {TRUE, FALSE}
 * `cam-static`: Set to TRUE if the camera is static - {TRUE, FALSE}
 * `coord-system`: 3D Coordinate System - {Image (0) - Left handed, Y up (1) - Right handed, Y up (2) - Right handed, Z up (3) - Left handed, Z up (4) - Right handed, Z up, X fwd (5)}
 * `od-enabled `: Enable Object Detection - {TRUE, FALSE}
 * `od-tracking `: Enable tracking for the detected objects - {TRUE, FALSE} 
 * `od-detection-model`: Object Detection Model - {Multi class (0), Skeleton tracking FAST (1), Skeleton tracking ACCURATE (2)}
 * `od-confidence`: Minimum Detection Confidence - [0,100]

### `ZED Video Demuxer Plugin` parameters

 * `is-depth`: indicates if the bottom stream of a composite `stream-type` of the `ZED Video Source Plugin` is a color image (Right image) or a depth map.
 * `stream-data`: Enable binary data streaming on `src_data` pad - {TRUE, FALSE} 
 
 ### `ZED Data CSV sink Plugin` parameters
 * `location`: Location of the CSV file to write
 * `append`: Append data to an already existing CSV file

## Example pipelines

### Local RGB stream + RGB rendering

    gst-launch-1.0 zedsrc ! autovideoconvert ! fpsdisplaysink

### Local 16 bit Depth stream + Depth rendering

    gst-launch-1.0 zedsrc stream-type=1 ! autovideoconvert ! fpsdisplaysink

### Local Left/Right stream + demux + double RGB rendering

    gst-launch-1.0 zedsrc stream-type=2 ! queue ! zeddemux is-depth=false name=demux demux.src_left ! queue ! autovideoconvert ! fpsdisplaysink  demux.src_aux ! queue ! autovideoconvert ! fpsdisplaysink

### Local Left/Depth stream + demux + double streams rendering

    gst-launch-1.0 zedsrc stream-type=4 ! queue ! zeddemux name=demux demux.src_left ! queue ! autovideoconvert ! fpsdisplaysink  demux.src_aux ! queue ! autovideoconvert ! fpsdisplaysink

### Local Left/Depth stream + demux + double streams rendering + data saving on CSV file

    gst-launch-1.0 zedsrc stream-type=4 ! zeddemux stream-data=TRUE name=demux demux.src_left ! queue ! autovideoconvert ! fpsdisplaysink  demux.src_aux ! queue ! autovideoconvert ! fpsdisplaysink demux.src_data ! queue ! zeddatacsvsink location="${HOME}/test_csv.csv" append=FALSE

### Local Left/Right stream + Multiclass Object Detection result displaying
    
    gst-launch-1.0 zedsrc stream-type=2 od-enabled=true od-detection-model=0 resolution=2 ! zedoddisplaysink

### Local Left/Right stream + Skeleton Tracking result displaying
    
    gst-launch-1.0 zedsrc stream-type=2 od-enabled=true od-detection-model=1 resolution=2 ! zedoddisplaysink

### Local Left/Right stream + Accurate Skeleton Tracking result displaying
    
    gst-launch-1.0 zedsrc stream-type=2 od-enabled=true od-detection-model=2 resolution=2 ! zedoddisplaysink

## Related

- [Stereolabs](https://www.stereolabs.com)
- [ZED 2 multi-sensor camera](https://www.stereolabs.com/zed-2/)
- [ZED SDK](https://www.stereolabs.com/developers/)

## License

This library is licensed under the LGPL License.
