#!/bin/bash
# =============================================================================
# ZED Camera DeepStream Inference Pipeline (Zero-Copy NV12)
# =============================================================================
# This script runs object detection on ZED camera feed using NVIDIA DeepStream
# with zero-copy NV12 input for maximum performance.
#
# Requirements:
#   - NVIDIA Jetson platform with DeepStream SDK installed
#   - ZED SDK 5.2+ with Advanced Capture API enabled
#   - ZED X or ZED X Mini camera (GMSL)
#
# Usage:
#   ./deepstream_inference.sh [config_file] [output_file]
#
# Examples:
#   ./deepstream_inference.sh                           # Use default config, display only
#   ./deepstream_inference.sh my_config.txt             # Custom nvinfer config
#   ./deepstream_inference.sh config.txt output.mp4    # Record with inference overlay
# =============================================================================

set -e

CONFIG_FILE="${1:-}"
OUTPUT_FILE="${2:-}"
FPS="${3:-30}"

echo "=============================================="
echo " ZED DeepStream Zero-Copy Inference"
echo "=============================================="
echo " FPS: $FPS"
if [ -n "$CONFIG_FILE" ]; then
    echo " Config: $CONFIG_FILE"
else
    echo " Config: Using default (PeopleNet)"
fi
if [ -n "$OUTPUT_FILE" ]; then
    echo " Output: $OUTPUT_FILE"
else
    echo " Output: Display only"
fi
echo "=============================================="

# Check for DeepStream (nvvideoconvert is a DeepStream element)
if ! gst-inspect-1.0 nvinfer > /dev/null 2>&1; then
    echo "ERROR: nvinfer plugin not found!"
    echo ""
    echo "DeepStream SDK is required. Install with:"
    echo ""
    echo "  # Find available DeepStream version:"
    echo "  apt-cache search deepstream"
    echo ""
    echo "  # Install (e.g., deepstream-7.0 or deepstream-7.1):"
    echo "  sudo apt update"
    echo "  sudo apt install deepstream-7.0"
    echo ""
    echo "  # Or download from NVIDIA:"
    echo "  # https://developer.nvidia.com/deepstream-getting-started"
    echo ""
    echo "After installation, you may need to:"
    echo "  export LD_LIBRARY_PATH=/opt/nvidia/deepstream/deepstream/lib:\$LD_LIBRARY_PATH"
    echo "  export GST_PLUGIN_PATH=/opt/nvidia/deepstream/deepstream/lib/gst-plugins"
    echo ""
    exit 1
fi

if ! gst-inspect-1.0 nvvideoconvert > /dev/null 2>&1; then
    echo "ERROR: nvvideoconvert plugin not found!"
    echo ""
    echo "DeepStream SDK is required. Find available version with:"
    echo "  apt-cache search deepstream"
    echo ""
    echo "For non-DeepStream pipelines, use hw_encode_nv12.sh instead."
    exit 1
fi

# Check for zedsrc
if ! gst-inspect-1.0 zedsrc > /dev/null 2>&1; then
    echo "ERROR: zedsrc plugin not found!"
    echo "Please build and install the zed-gstreamer plugin."
    exit 1
fi

# Create default config if none provided
if [ -z "$CONFIG_FILE" ]; then
    CONFIG_FILE="/tmp/zed_deepstream_config.txt"
    cat > "$CONFIG_FILE" << 'EOF'
[property]
gpu-id=0
net-scale-factor=0.0039215697906911373
model-engine-file=/opt/nvidia/deepstream/deepstream/samples/models/Primary_Detector/resnet10.caffemodel_b1_gpu0_int8.engine
labelfile-path=/opt/nvidia/deepstream/deepstream/samples/models/Primary_Detector/labels.txt
int8-calib-file=/opt/nvidia/deepstream/deepstream/samples/models/Primary_Detector/cal_trt.bin
batch-size=1
process-mode=1
model-color-format=0
network-mode=1
num-detected-classes=4
interval=0
gie-unique-id=1
output-blob-names=conv2d_bbox;conv2d_cov/Sigmoid

[class-attrs-all]
pre-cluster-threshold=0.2
topk=20
nms-iou-threshold=0.5
EOF
    echo "Created default config at: $CONFIG_FILE"
fi

# Build the pipeline
if [ -n "$OUTPUT_FILE" ]; then
    # Recording pipeline with inference overlay
    gst-launch-1.0 -e \
        zedsrc camera-resolution=2 camera-fps=$FPS stream-type=5 ! \
        video/x-raw,format=NV12 ! \
        nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
        m.sink_0 nvstreammux name=m batch-size=1 width=1920 height=1200 ! \
        nvinfer config-file-path=$CONFIG_FILE ! \
        nvvideoconvert ! "video/x-raw(memory:NVMM),format=RGBA" ! \
        nvdsosd ! \
        tee name=t \
        t. ! queue ! nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
            nvv4l2h265enc bitrate=8000000 ! h265parse ! mp4mux ! filesink location=$OUTPUT_FILE \
        t. ! queue ! nv3dsink sync=false
else
    # Display only pipeline
    gst-launch-1.0 \
        zedsrc camera-resolution=2 camera-fps=$FPS stream-type=5 ! \
        video/x-raw,format=NV12 ! \
        nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
        m.sink_0 nvstreammux name=m batch-size=1 width=1920 height=1200 ! \
        nvinfer config-file-path=$CONFIG_FILE ! \
        nvvideoconvert ! "video/x-raw(memory:NVMM),format=RGBA" ! \
        nvdsosd ! \
        nv3dsink sync=false
fi

echo ""
echo "DeepStream pipeline completed."
if [ -n "$OUTPUT_FILE" ] && [ -f "$OUTPUT_FILE" ]; then
    echo "Output saved to: $OUTPUT_FILE"
    ls -lh "$OUTPUT_FILE"
fi
