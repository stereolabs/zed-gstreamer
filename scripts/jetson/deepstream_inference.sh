#!/bin/bash
# =============================================================================
# ZED Camera DeepStream Inference Pipeline
# =============================================================================
# This script runs object detection on ZED camera feed using NVIDIA DeepStream.
#
# For GMSL cameras (ZED X, ZED X Mini):
#   - Uses zero-copy NV12 for maximum performance
#
# For USB cameras (ZED 2, ZED 2i, ZED Mini):
#   - Uses BGRA with nvvideoconvert to NVMM NV12
#
# Requirements:
#   - NVIDIA Jetson platform with DeepStream SDK installed
#   - ZED SDK 5.2+ (with Advanced Capture API for GMSL zero-copy)
#   - ZED camera (any model)
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

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

CONFIG_FILE="${1:-}"
OUTPUT_FILE="${2:-}"
FPS="${3:-30}"

# Handle headless environments (no DISPLAY) by defaulting to file output
if [ -z "$OUTPUT_FILE" ] && [ -z "$DISPLAY" ]; then
    OUTPUT_FILE="zed_deepstream_output.mp4"
    echo "WARNING: No DISPLAY detected. Defaulting output to: $OUTPUT_FILE"
fi

echo "=============================================="
echo " ZED DeepStream Inference"
echo "=============================================="
print_camera_info
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
    CONFIG_FILE="zed_deepstream_config.txt"
    
    # Try to find standard DeepStream config
    # We use the 'deepstream' symlink to be version agnostic
    DS_ROOT="/opt/nvidia/deepstream/deepstream"
    DS_SAMPLE_CONFIG="$DS_ROOT/samples/configs/deepstream-app/config_infer_primary.txt"
    
    if [ -f "$DS_SAMPLE_CONFIG" ]; then
        echo "Found DeepStream sample config: $DS_SAMPLE_CONFIG"
        cp "$DS_SAMPLE_CONFIG" "$CONFIG_FILE"
        chmod u+w "$CONFIG_FILE"

        # Patch the config file:
        # 1. Update relative paths for models to absolute paths
        sed -i "s|\.\./\.\./models|$DS_ROOT/samples/models|g" "$CONFIG_FILE"
        
        # 2. Change batch-size from 30 (default) to 1 (our stream)
        sed -i "s/batch-size=30/batch-size=1/g" "$CONFIG_FILE"
        
        # 3. Redirect the engine file to the current directory (writeable)
        ENGINE_FILE="$(pwd)/resnet18_trafficcamnet.etlt_b1_gpu0_int8.engine"
        echo "Setting engine file to: $ENGINE_FILE"
        
        # Remove any existing model-engine-file definition
        sed -i "/model-engine-file=/d" "$CONFIG_FILE"
        
        # Add the new definition after [property]
        sed -i "/\[property\]/a model-engine-file=$ENGINE_FILE" "$CONFIG_FILE"
        
        echo "Created patched config at: $CONFIG_FILE"
        echo "Engine file will be saved to: $ENGINE_FILE"
    else
        echo "WARNING: DeepStream sample config not found at $DS_SAMPLE_CONFIG"
        echo "Creating a minimal fallback config..."
        # Fallback to a minimal manually created config if the sample is missing
        cat > "$CONFIG_FILE" << EOF
[property]
gpu-id=0
net-scale-factor=0.0039215697906911373
model-engine-file=$PWD/resnet18_trafficcamnet.etlt_b1_gpu0_int8.engine
labelfile-path=$DS_ROOT/samples/models/Primary_Detector/labels.txt
int8-calib-file=$DS_ROOT/samples/models/Primary_Detector/cal_trt.bin
force-implicit-batch-dim=1
batch-size=1
process-mode=1
model-color-format=0
network-mode=1
num-detected-classes=4
interval=0
gie-unique-id=1
output-blob-names=output_cov/Sigmoid;output_bbox/BiasAdd
cluster-mode=2
infer-dims=3;544;960
tlt-model-key=tlt_encode
tlt-encoded-model=$DS_ROOT/samples/models/Primary_Detector/resnet18_trafficcamnet.etlt
[class-attrs-all]
topk=20
nms-iou-threshold=0.5
pre-cluster-threshold=0.2
EOF
    fi
    
    echo "--- DEBUG: Final Config Content ---"
    cat "$CONFIG_FILE"
    echo "--- DEBUG: End Config Content ---"
fi

# Build the pipeline based on zero-copy availability
if [ -n "$OUTPUT_FILE" ]; then
    # Recording pipeline with inference overlay
    if is_zero_copy_available; then
        # Zero-copy NV12 path
        gst-launch-1.0 -e \
            zedsrc camera-resolution=2 camera-fps=$FPS stream-type=6 ! \
            "video/x-raw(memory:NVMM),format=NV12" ! \
            nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
            m.sink_0 nvstreammux name=m batch-size=1 width=1920 height=1200 ! \
            nvinfer config-file-path=$CONFIG_FILE ! \
            nvvideoconvert ! "video/x-raw(memory:NVMM),format=RGBA" ! \
            nvdsosd ! \
            nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
            nvv4l2h265enc bitrate=8000000 ! h265parse ! mp4mux ! filesink location=$OUTPUT_FILE
    else
        # BGRA with conversion path (USB or older SDK)
        gst-launch-1.0 -e \
            zedsrc camera-resolution=2 camera-fps=$FPS stream-type=0 ! \
            nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
            m.sink_0 nvstreammux name=m batch-size=1 width=1920 height=1200 ! \
            nvinfer config-file-path=$CONFIG_FILE ! \
            nvvideoconvert ! "video/x-raw(memory:NVMM),format=RGBA" ! \
            nvdsosd ! \
            nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
            nvv4l2h265enc bitrate=8000000 ! h265parse ! mp4mux ! filesink location=$OUTPUT_FILE
    fi
else
    # Display only pipeline
    if is_zero_copy_available; then
        # Zero-copy NV12 path
        gst-launch-1.0 \
            zedsrc camera-resolution=2 camera-fps=$FPS stream-type=6 ! \
            "video/x-raw(memory:NVMM),format=NV12" ! \
            nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
            m.sink_0 nvstreammux name=m batch-size=1 width=1920 height=1200 ! \
            nvinfer config-file-path=$CONFIG_FILE ! \
            nvvideoconvert ! "video/x-raw(memory:NVMM),format=RGBA" ! \
            nvdsosd ! \
            nv3dsink sync=false
    else
        # BGRA with conversion path (USB or older SDK)
        gst-launch-1.0 \
            zedsrc camera-resolution=2 camera-fps=$FPS stream-type=0 ! \
            nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" ! \
            m.sink_0 nvstreammux name=m batch-size=1 width=1920 height=1200 ! \
            nvinfer config-file-path=$CONFIG_FILE ! \
            nvvideoconvert ! "video/x-raw(memory:NVMM),format=RGBA" ! \
            nvdsosd ! \
            nv3dsink sync=false
    fi
fi

echo ""
echo "DeepStream pipeline completed."
if [ -n "$OUTPUT_FILE" ] && [ -f "$OUTPUT_FILE" ]; then
    echo "Output saved to: $OUTPUT_FILE"
    ls -lh "$OUTPUT_FILE"
fi
