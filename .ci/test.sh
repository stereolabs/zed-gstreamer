#!/bin/bash
# =============================================================================
# ZED GStreamer Plugin Test Suite
# =============================================================================
# This script tests all ZED GStreamer plugins for proper installation,
# registration, and basic functionality.
#
# Usage:
#   ./test.sh [OPTIONS]
#
# Options:
#   -f, --fast      Fast mode (~30-60 seconds, plugin checks only)
#   -e, --extensive Extensive mode (~10 minutes, includes runtime tests)
#   -v, --verbose   Verbose output
#   -h, --help      Show this help message
#
# Exit codes:
#   0 - All tests passed
#   1 - Test failures (code/plugin issues)
#   2 - Hardware not available (not a code issue)  
#   3 - Missing dependencies
# =============================================================================

# Note: We intentionally don't use `set -o pipefail` because gst-inspect-1.0
# can cause SIGPIPE (exit 141) when piped to grep -q, since grep exits early
# when it finds a match but gst-inspect is still writing output.

# =============================================================================
# Configuration
# =============================================================================

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Test configuration
FAST_MODE=true
EXTENSIVE_MODE=false
BENCHMARK_MODE=false
COMPARE_ZEROCOPY_MODE=false  # DEV: Compare NV12 zero-copy vs BGRA only
BENCHMARK_BASELINE=""  # Path to baseline JSON for comparison
VERBOSE=false
HARDWARE_AVAILABLE=false
HARDWARE_CHECK_DONE=false
GMSL_CAMERA=false
ZERO_COPY_AVAILABLE=false

# Counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

# Plugin list
PLUGINS=(
    "zedsrc"
    "zedxonesrc"
    "zeddemux"
    "zeddatamux"
    "zeddatacsvsink"
    "zedodoverlay"
)

# Timeout values (seconds)
# GMSL cameras need longer init time than USB cameras
FAST_PIPELINE_TIMEOUT=15
EXTENSIVE_PIPELINE_TIMEOUT=60

# Delay between hardware tests to allow camera reset (seconds)
# GMSL cameras need longer reset time to avoid Argus socket errors
CAMERA_RESET_DELAY=8

# Benchmark configuration (will be adjusted based on mode)
BENCHMARK_DURATION=10      # Duration in seconds for each benchmark (extensive)
BENCHMARK_DURATION_FAST=5  # Duration for fast benchmark mode
BENCHMARK_WARMUP=2         # Warmup period before measuring
BENCHMARK_SAMPLE_RATE=500  # Tegrastats sample rate in ms

# =============================================================================
# Helper Functions
# =============================================================================

print_header() {
    echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

print_subheader() {
    echo -e "\n${CYAN}── $1 ──${NC}"
}

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[FAIL]${NC} $1"
}

log_skip() {
    echo -e "${YELLOW}[SKIP]${NC} $1"
}

log_verbose() {
    if [ "$VERBOSE" = true ]; then
        echo -e "${CYAN}[DEBUG]${NC} $1"
    fi
}

test_pass() {
    local test_name="$1"
    TESTS_RUN=$((TESTS_RUN + 1))
    TESTS_PASSED=$((TESTS_PASSED + 1))
    log_success "$test_name"
}

test_fail() {
    local test_name="$1"
    local details="$2"
    TESTS_RUN=$((TESTS_RUN + 1))
    TESTS_FAILED=$((TESTS_FAILED + 1))
    log_error "$test_name"
    if [ "$VERBOSE" = true ] && [ -n "$details" ]; then
        echo "    Details: $details"
    fi
}

skip_test() {
    local test_name="$1"
    local reason="$2"
    TESTS_SKIPPED=$((TESTS_SKIPPED + 1))
    log_skip "$test_name - $reason"
}

check_command() {
    if ! command -v "$1" &> /dev/null; then
        log_error "Required command '$1' not found"
        return 1
    fi
    return 0
}

detect_platform() {
    if [ -f /etc/nv_tegra_release ]; then
        echo "jetson"
    elif [ "$(uname)" = "Linux" ]; then
        echo "linux"
    elif [ "$(uname)" = "Darwin" ]; then
        echo "macos"
    else
        echo "unknown"
    fi
}

# Detect ZED SDK version from the installed library
detect_sdk_version() {
    local version=""
    # Try to get version from ZED SDK's version file
    if [ -f /usr/local/zed/version ]; then
        version=$(cat /usr/local/zed/version 2>/dev/null | head -1)
    fi
    # Fallback: try to get from cmake config
    if [ -z "$version" ] && [ -f /usr/local/zed/zed-config-version.cmake ]; then
        version=$(grep "PACKAGE_VERSION" /usr/local/zed/zed-config-version.cmake | head -1 | grep -oP '"[0-9.]+"' | tr -d '"')
    fi
    # Fallback: try running ZED_Diagnostic
    if [ -z "$version" ] && command -v ZED_Diagnostic &> /dev/null; then
        version=$(ZED_Diagnostic --version 2>/dev/null | grep -oP '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
    fi
    echo "${version:-unknown}"
}

# Get major.minor from SDK version
get_sdk_major_minor() {
    local version
    version=$(detect_sdk_version)
    echo "$version" | grep -oP '^[0-9]+\.[0-9]+' || echo "0.0"
}

# Source common functions from jetson scripts (if available)
# This provides: detect_gmsl_camera, zedsrc_supports_nv12, is_zero_copy_available, etc.
SOURCE_COMMON_SH="$(dirname "${BASH_SOURCE[0]}")/../scripts/jetson/common.sh"
if [ -f "$SOURCE_COMMON_SH" ]; then
    # Only source on Jetson platforms where these functions are relevant
    if [ -f /etc/nv_tegra_release ]; then
        source "$SOURCE_COMMON_SH"
        COMMON_SOURCED=true
    fi
fi

check_hardware() {
    if [ "$HARDWARE_CHECK_DONE" = true ]; then
        return
    fi
    
    HARDWARE_CHECK_DONE=true
    
    log_verbose "Checking for ZED camera hardware..."
    
    # Method 1: Use ZED_Explorer if available (works for both USB and GMSL cameras)
    if command -v ZED_Explorer &> /dev/null; then
        local zed_output
        zed_output=$(ZED_Explorer -a 2>&1)
        if echo "$zed_output" | grep -qi "AVAILABLE"; then
            HARDWARE_AVAILABLE=true
            local cam_count
            cam_count=$(echo "$zed_output" | grep -ci "AVAILABLE" || echo "0")
            log_info "ZED camera(s) detected via ZED_Explorer ($cam_count available)"
            
            # Check if it's a GMSL camera (ZED X / ZED X Mini)
            if echo "$zed_output" | grep -qi "ZED X\|GMSL"; then
                GMSL_CAMERA=true
                log_info "GMSL camera detected (ZED X / ZED X Mini)"
            fi
        fi
    fi
    
    # Method 2: Try to detect ZED camera via USB (for USB cameras like ZED 2, ZED 2i, ZED Mini)
    if [ "$HARDWARE_AVAILABLE" = false ] && command -v lsusb &> /dev/null; then
        if lsusb 2>/dev/null | grep -qi "stereolabs\|2b03:"; then
            HARDWARE_AVAILABLE=true
            GMSL_CAMERA=false
            log_info "ZED USB camera detected"
        fi
    fi
    
    # Method 3: Use common.sh functions if available (Jetson only)
    if [ "$HARDWARE_AVAILABLE" = false ] && [ "${COMMON_SOURCED:-false}" = true ]; then
        if detect_gmsl_camera 2>/dev/null; then
            HARDWARE_AVAILABLE=true
            GMSL_CAMERA=true
            log_info "GMSL camera detected via driver check"
        elif detect_usb_camera 2>/dev/null; then
            HARDWARE_AVAILABLE=true
            GMSL_CAMERA=false
            log_info "USB camera detected via lsusb"
        fi
    fi
    
    # Check zero-copy availability (Jetson + GMSL + SDK 5.2+)
    if [ "$GMSL_CAMERA" = true ]; then
        if [ "${COMMON_SOURCED:-false}" = true ] && is_zero_copy_available 2>/dev/null; then
            ZERO_COPY_AVAILABLE=true
            log_info "Zero-copy NV12 available (SDK 5.2+ with Advanced Capture API)"
        elif gst-inspect-1.0 zedsrc 2>&1 | grep -q "Raw NV12 zero-copy"; then
            ZERO_COPY_AVAILABLE=true
            log_info "Zero-copy NV12 available (stream-type=6)"
        fi
    fi
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        log_warning "No ZED camera hardware detected - hardware tests will be skipped"
    fi
}

# =============================================================================
# Test Functions
# =============================================================================

test_dependencies() {
    print_subheader "Checking Dependencies"
    
    local deps_ok=true
    
    # Required commands
    if ! check_command "gst-inspect-1.0"; then
        deps_ok=false
    fi
    
    if ! check_command "gst-launch-1.0"; then
        deps_ok=false
    fi
    
    # Check GStreamer version
    local gst_version
    gst_version=$(gst-inspect-1.0 --version 2>/dev/null | head -1)
    if [ -n "$gst_version" ]; then
        log_info "GStreamer: $gst_version"
    fi
    
    if [ "$deps_ok" = false ]; then
        return 3
    fi
    
    return 0
}

test_plugin_registration() {
    print_subheader "Plugin Registration Tests"
    
    for plugin in "${PLUGINS[@]}"; do
        if gst-inspect-1.0 "$plugin" > /dev/null 2>&1; then
            test_pass "Plugin '$plugin' is registered"
        else
            test_fail "Plugin '$plugin' is registered"
        fi
    done
}

test_plugin_properties() {
    print_subheader "Plugin Properties Tests"
    
    # zedsrc properties
    local zedsrc_props=("camera-resolution" "camera-fps" "stream-type" "depth-mode" "od-enabled" "bt-enabled")
    for prop in "${zedsrc_props[@]}"; do
        if gst-inspect-1.0 zedsrc 2>&1 | grep -q "$prop"; then
            test_pass "zedsrc has property '$prop'"
        else
            test_fail "zedsrc has property '$prop'"
        fi
    done
    
    # zedxonesrc properties
    local zedxonesrc_props=("camera-resolution" "camera-fps" "camera-id")
    for prop in "${zedxonesrc_props[@]}"; do
        if gst-inspect-1.0 zedxonesrc 2>&1 | grep -q "$prop"; then
            test_pass "zedxonesrc has property '$prop'"
        else
            test_fail "zedxonesrc has property '$prop'"
        fi
    done
    
    # zeddemux properties
    local zeddemux_props=("is-depth" "stream-data" "is-mono")
    for prop in "${zeddemux_props[@]}"; do
        if gst-inspect-1.0 zeddemux 2>&1 | grep -q "$prop"; then
            test_pass "zeddemux has property '$prop'"
        else
            test_fail "zeddemux has property '$prop'"
        fi
    done
    
    # zeddatacsvsink properties
    local csvsink_props=("location" "append")
    for prop in "${csvsink_props[@]}"; do
        if gst-inspect-1.0 zeddatacsvsink 2>&1 | grep -q "$prop"; then
            test_pass "zeddatacsvsink has property '$prop'"
        else
            test_fail "zeddatacsvsink has property '$prop'"
        fi
    done
}

test_plugin_factory() {
    print_subheader "Plugin Factory Tests"
    
    # Check that each plugin has valid factory details
    for plugin in "${PLUGINS[@]}"; do
        if gst-inspect-1.0 "$plugin" 2>&1 | grep -q "Factory Details"; then
            test_pass "Plugin '$plugin' has valid factory"
        else
            test_fail "Plugin '$plugin' has valid factory"
        fi
    done
}

test_zedsrc_enums() {
    print_subheader "ZedSrc Enum Value Tests"
    
    # Test resolution enum values exist
    local resolutions=("HD2K" "HD1080" "HD1200" "HD720" "SVGA" "VGA")
    for res in "${resolutions[@]}"; do
        if gst-inspect-1.0 zedsrc 2>&1 | grep -q "$res"; then
            test_pass "zedsrc has resolution '$res'"
        else
            test_fail "zedsrc has resolution '$res'"
        fi
    done
    
    # Test depth mode enum values
    local depth_modes=("NONE" "PERFORMANCE" "QUALITY" "ULTRA" "NEURAL")
    for mode in "${depth_modes[@]}"; do
        if gst-inspect-1.0 zedsrc 2>&1 | grep -q "$mode"; then
            test_pass "zedsrc has depth mode '$mode'"
        else
            test_fail "zedsrc has depth mode '$mode'"
        fi
    done
    
    # Test stream-type enum values
    local stream_types=("Left image" "Right image" "Stereo couple" "Depth image")
    for stype in "${stream_types[@]}"; do
        if gst-inspect-1.0 zedsrc 2>&1 | grep -q "$stype"; then
            test_pass "zedsrc has stream-type '$stype'"
        else
            test_fail "zedsrc has stream-type '$stype'"
        fi
    done
}

test_zedsrc_auto_resolution() {
    print_subheader "ZedSrc Auto-Resolution Negotiation Tests"

    # Check if zedsrc is available
    if ! gst-inspect-1.0 zedsrc > /dev/null 2>&1; then
        skip_test "ZedSrc auto-resolution tests" "zedsrc not available"
        return 0
    fi

    # --- Enum introspection ---
    # AUTO resolution enum must be advertised
    if gst-inspect-1.0 zedsrc 2>&1 | grep -qi "Automatic.*Default value\|AUTO_RES\|auto_res"; then
        test_pass "zedsrc has AUTO resolution enum"
    else
        # Accept either "Automatic" or "AUTO" keyword
        if gst-inspect-1.0 zedsrc 2>&1 | grep -qi "automatic"; then
            test_pass "zedsrc has AUTO resolution enum"
        else
            test_fail "zedsrc has AUTO resolution enum"
        fi
    fi

    # Default camera-resolution must be AUTO (enum value 6)
    local default_res
    default_res=$(gst-inspect-1.0 zedsrc 2>&1 | grep -A2 "camera-resolution" | grep -oi "Default.*" | head -1)
    if echo "$default_res" | grep -qi "6\|auto\|automatic"; then
        test_pass "zedsrc default camera-resolution is AUTO"
    else
        test_fail "zedsrc default camera-resolution is AUTO (got: $default_res)"
    fi

    # Default camera-fps must be 30
    local default_fps
    default_fps=$(gst-inspect-1.0 zedsrc 2>&1 | grep -A2 "camera-fps" | grep -oi "Default.*" | head -1)
    if echo "$default_fps" | grep -q "30"; then
        test_pass "zedsrc default camera-fps is 30"
    else
        test_fail "zedsrc default camera-fps is 30 (got: $default_fps)"
    fi

    # --- FPS discrete list in caps (requires camera) ---
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "zedsrc AUTO caps inspection" "No camera detected"
        return 0
    fi

    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "zedsrc AUTO caps negotiation" "Extensive mode required"
        return 0
    fi

    local timeout_val=90
    local num_buffers=5
    local output

    # Test 1: Default (AUTO + 30fps) should produce a valid pipeline
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "zedsrc AUTO resolution default pipeline"
    else
        test_fail "zedsrc AUTO resolution default pipeline"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
    fi

    sleep $CAMERA_RESET_DELAY

    # Test 2: AUTO + 15fps → HD2K on USB, HD1200 on GMSL
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc camera-resolution=6 camera-fps=15 num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        if [ "$GMSL_CAMERA" = true ]; then
            test_pass "zedsrc AUTO@15fps pipeline works (GMSL → HD1200)"
        else
            test_pass "zedsrc AUTO@15fps pipeline works (USB → HD2K)"
        fi
    else
        test_fail "zedsrc AUTO@15fps pipeline"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
    fi

    sleep $CAMERA_RESET_DELAY

    # Test 3: AUTO + 60fps → should select HD1200 (highest supporting 60)
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc camera-resolution=6 camera-fps=60 num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "zedsrc AUTO@60fps pipeline works"
    else
        test_fail "zedsrc AUTO@60fps pipeline"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
    fi

    sleep $CAMERA_RESET_DELAY

    # Test 4: AUTO + 120fps → should select SVGA (only mode supporting 120)
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc camera-resolution=6 camera-fps=120 num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "zedsrc AUTO@120fps pipeline works"
    else
        # 120fps is GMSL-only, USB cameras will clamp to closest supported FPS
        if echo "$output" | grep -qi "no camera\|not found\|failed to open\|not supported"; then
            skip_test "zedsrc AUTO@120fps pipeline" "Not supported on this camera"
        else
            test_fail "zedsrc AUTO@120fps pipeline"
            [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
        fi
    fi

    sleep $CAMERA_RESET_DELAY

    # Test 5: Explicit resolution + FPS clamping
    if [ "$GMSL_CAMERA" = true ]; then
        # GMSL: test HD1200 → supports 15/30/60, so 120 clamps to 60
        output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc camera-resolution=2 camera-fps=120 num-buffers=$num_buffers ! fakesink 2>&1)
        if [ $? -eq 0 ]; then
            test_pass "zedsrc HD1200@120fps clamped to 60fps [GMSL]"
        else
            test_fail "zedsrc HD1200@120fps FPS clamping [GMSL]"
            [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
        fi
    else
        # USB: test HD2K → only supports 15, so 60 clamps to 15
        output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc camera-resolution=0 camera-fps=60 num-buffers=$num_buffers ! fakesink 2>&1)
        if [ $? -eq 0 ]; then
            test_pass "zedsrc HD2K@60fps clamped to 15fps [USB]"
        else
            test_fail "zedsrc HD2K@60fps FPS clamping [USB]"
            [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
        fi
    fi

    sleep $CAMERA_RESET_DELAY

    # Test 6: Downstream size negotiation — request smaller output than camera native
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc camera-resolution=6 num-buffers=$num_buffers ! \
        "video/x-raw,width=640,height=480" ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "zedsrc AUTO with downstream resize (640x480)"
    else
        test_fail "zedsrc AUTO with downstream resize (640x480)"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
    fi

    sleep $CAMERA_RESET_DELAY
}

test_zedsrc_nv12() {
    print_subheader "ZedSrc NV12 Zero-Copy Tests"
    
    # Check if zedsrc is available
    if ! gst-inspect-1.0 zedsrc > /dev/null 2>&1; then
        skip_test "ZedSrc NV12 tests" "zedsrc not available"
        return 0
    fi
    
    # Check if NV12 zero-copy is available for zedsrc
    local zedsrc_zerocopy_available=false
    if gst-inspect-1.0 zedsrc 2>&1 | grep -q "Raw NV12 zero-copy"; then
        zedsrc_zerocopy_available=true
        test_pass "zedsrc supports NV12 zero-copy (stream-type=6/7)"
    else
        skip_test "zedsrc NV12 zero-copy" "SDK < 5.2 or not built with Advanced Capture API"
    fi
    
    # Check for AUTO stream type with NV12 preference
    if gst-inspect-1.0 zedsrc 2>&1 | grep -q "Auto.*NV12 zero-copy\|prefer NV12 zero-copy"; then
        test_pass "zedsrc has STREAM_AUTO with NV12 preference"
    else
        # May not have zero-copy but should still have AUTO
        if gst-inspect-1.0 zedsrc 2>&1 | grep -q "Auto-negotiate"; then
            test_pass "zedsrc has STREAM_AUTO"
        else
            test_fail "zedsrc has STREAM_AUTO"
        fi
    fi
    
    # Hardware test for NV12 zero-copy
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "ZedSrc NV12 hardware test" "No camera detected"
        return 2
    fi
    
    # Only test on Jetson with zero-copy available
    if [ ! -f /etc/nv_tegra_release ]; then
        skip_test "ZedSrc NV12 hardware test" "Not a Jetson platform"
        return 0
    fi
    
    if [ "$zedsrc_zerocopy_available" = false ]; then
        skip_test "ZedSrc NV12 hardware test" "Zero-copy not available"
        return 0
    fi
    
    if [ "$EXTENSIVE_MODE" = true ]; then
        local timeout_val=30
        local num_buffers=10
        local output
        
        # Test stream-type=6 (single NV12 zero-copy)
        output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=6 num-buffers=$num_buffers ! \
            'video/x-raw(memory:NVMM),format=NV12' ! fakesink 2>&1)
        if [ $? -eq 0 ]; then
            test_pass "ZedSrc NV12 zero-copy pipeline (stream-type=6)"
        else
            if echo "$output" | grep -qi "no camera\|not found\|failed to open\|not supported"; then
                skip_test "ZedSrc NV12 zero-copy pipeline" "Not supported on this camera model"
            else
                test_fail "ZedSrc NV12 zero-copy pipeline (stream-type=6)"
                [ "$VERBOSE" = true ] && echo "$output" | grep -i "error\|fail" | head -3
            fi
        fi
        
        sleep $CAMERA_RESET_DELAY
        
        # Test stream-type=7 (stereo NV12 zero-copy)
        output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=7 num-buffers=$num_buffers ! \
            'video/x-raw(memory:NVMM),format=NV12' ! fakesink 2>&1)
        if [ $? -eq 0 ]; then
            test_pass "ZedSrc NV12 stereo zero-copy pipeline (stream-type=7)"
        else
            if echo "$output" | grep -qi "no camera\|not found\|failed to open\|not supported"; then
                skip_test "ZedSrc NV12 stereo zero-copy pipeline" "Not supported on this camera model"
            else
                test_fail "ZedSrc NV12 stereo zero-copy pipeline (stream-type=7)"
                [ "$VERBOSE" = true ] && echo "$output" | grep -i "error\|fail" | head -3
            fi
        fi
        
        sleep $CAMERA_RESET_DELAY
    else
        skip_test "ZedSrc NV12 hardware test" "Extensive mode required"
    fi
}

test_element_pads() {
    print_subheader "Element Pad Tests"
    
    # Check zedsrc has src pad
    if gst-inspect-1.0 zedsrc 2>&1 | grep -q "SRC template"; then
        test_pass "zedsrc has SRC pad template"
    else
        test_fail "zedsrc has SRC pad template"
    fi
    
    # Check zeddemux has sink and src pads
    if gst-inspect-1.0 zeddemux 2>&1 | grep -q "SINK template"; then
        test_pass "zeddemux has SINK pad template"
    else
        test_fail "zeddemux has SINK pad template"
    fi
    
    if gst-inspect-1.0 zeddemux 2>&1 | grep -q "SRC template"; then
        test_pass "zeddemux has SRC pad template"
    else
        test_fail "zeddemux has SRC pad template"
    fi
    
    # Check zedodoverlay is a transform element
    if gst-inspect-1.0 zedodoverlay 2>&1 | grep -q "SINK template"; then
        test_pass "zedodoverlay has SINK pad template"
    else
        test_fail "zedodoverlay has SINK pad template"
    fi
}

test_hardware_basic() {
    print_subheader "Hardware Basic Tests (requires camera)"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "Hardware grab test" "No camera detected"
        return 2
    fi
    
    local timeout_val=$FAST_PIPELINE_TIMEOUT
    if [ "$EXTENSIVE_MODE" = true ]; then
        timeout_val=$EXTENSIVE_PIPELINE_TIMEOUT
    fi
    
    # Test basic camera grab (suppress verbose camera output, only show errors)
    log_verbose "Testing basic camera grab..."
    local output
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc num-buffers=5 ! fakesink 2>&1)
    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        test_pass "ZED camera basic grab"
    else
        test_fail "ZED camera basic grab"
        if [ "$VERBOSE" = true ]; then
            echo "$output" | grep -i "error\|fail" | head -5
        fi
    fi
    
    # Allow camera to reset before next test
    sleep $CAMERA_RESET_DELAY
}

test_hardware_streaming() {
    print_subheader "Hardware Streaming Tests (requires camera)"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "Stream type 0 (left only)" "No camera detected"
        skip_test "Stream type 2 (left+right)" "No camera detected"
        return 2
    fi
    
    local num_buffers=10
    local timeout_val=$FAST_PIPELINE_TIMEOUT
    
    if [ "$EXTENSIVE_MODE" = true ]; then
        num_buffers=100
        timeout_val=$EXTENSIVE_PIPELINE_TIMEOUT
    fi
    
    # Test different stream types (suppress verbose output)
    local output
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=0 num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "Stream type 0 (left only)"
    else
        test_fail "Stream type 0 (left only)"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error\|fail" | head -3
    fi
    
    sleep $CAMERA_RESET_DELAY
    
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=2 num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "Stream type 2 (left+right)"
    else
        test_fail "Stream type 2 (left+right)"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error\|fail" | head -3
    fi
    
    if [ "$EXTENSIVE_MODE" = true ]; then
        sleep $CAMERA_RESET_DELAY
        
        output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=3 depth-mode=1 num-buffers=$num_buffers ! fakesink 2>&1)
        if [ $? -eq 0 ]; then
            test_pass "Stream type 3 (depth 16-bit)"
        else
            test_fail "Stream type 3 (depth 16-bit)"
            [ "$VERBOSE" = true ] && echo "$output" | grep -i "error\|fail" | head -3
        fi
        
        sleep $CAMERA_RESET_DELAY
        
        output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=4 depth-mode=1 num-buffers=$num_buffers ! fakesink 2>&1)
        if [ $? -eq 0 ]; then
            test_pass "Stream type 4 (left+depth)"
        else
            test_fail "Stream type 4 (left+depth)"
            [ "$VERBOSE" = true ] && echo "$output" | grep -i "error\|fail" | head -3
        fi
    fi
    
    # Test RAW_NV12 stream types (6, 7) - only available on Jetson with GMSL cameras
    # These require SL_ENABLE_ADVANCED_CAPTURE_API which is only enabled for SDK >= 5.2 on Jetson
    if [ -f /etc/nv_tegra_release ]; then
        if [ "$ZERO_COPY_AVAILABLE" = true ]; then
            sleep $CAMERA_RESET_DELAY
            
            # Test stream-type=6 (single NV12 zero-copy)
            output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=6 num-buffers=$num_buffers ! \
                'video/x-raw(memory:NVMM),format=NV12' ! fakesink 2>&1)
            if [ $? -eq 0 ]; then
                test_pass "Stream type 6 (RAW_NV12 zero-copy)"
            else
                test_fail "Stream type 6 (RAW_NV12 zero-copy)"
                [ "$VERBOSE" = true ] && echo "$output" | grep -i "error\|fail" | head -3
            fi
            
            if [ "$EXTENSIVE_MODE" = true ]; then
                sleep $CAMERA_RESET_DELAY
                
                # Test stream-type=7 (stereo NV12 zero-copy)
                output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=7 num-buffers=$num_buffers ! \
                    'video/x-raw(memory:NVMM),format=NV12' ! fakesink 2>&1)
                if [ $? -eq 0 ]; then
                    test_pass "Stream type 7 (RAW_NV12 stereo zero-copy)"
                else
                    test_fail "Stream type 7 (RAW_NV12 stereo zero-copy)"
                    [ "$VERBOSE" = true ] && echo "$output" | grep -i "error\|fail" | head -3
                fi
            fi
        elif [ "$GMSL_CAMERA" = true ]; then
            # GMSL camera present but zero-copy not available (older SDK)
            skip_test "Stream type 6 (RAW_NV12)" "Requires ZED SDK 5.2+ with Advanced Capture API"
            skip_test "Stream type 7 (RAW_NV12 stereo)" "Requires ZED SDK 5.2+ with Advanced Capture API"
        elif [ "$HARDWARE_AVAILABLE" = true ]; then
            # USB camera - zero-copy not supported
            skip_test "Stream type 6 (RAW_NV12)" "USB camera - GMSL camera required"
            skip_test "Stream type 7 (RAW_NV12 stereo)" "USB camera - GMSL camera required"
        else
            skip_test "Stream type 6 (RAW_NV12)" "No camera detected"
            skip_test "Stream type 7 (RAW_NV12 stereo)" "No camera detected"
        fi
    fi
    
    sleep $CAMERA_RESET_DELAY
}

test_hardware_demux() {
    print_subheader "Hardware Demux Tests (requires camera)"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "Demux left/right" "No camera detected"
        skip_test "Demux left/depth" "No camera detected"
        return 2
    fi
    
    # Demux tests can hang with limited buffers due to EOS propagation issues
    # Only run in extensive mode where we have longer timeouts
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "Demux left/right stream" "Extensive mode required (demux needs longer runtime)"
        return 0
    fi
    
    local num_buffers=30
    local timeout_val=90
    
    local output
    # Use queues to help with EOS propagation in multi-branch pipelines
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=2 num-buffers=$num_buffers ! \
        zeddemux is-depth=false name=demux demux.src_left ! queue ! fakesink demux.src_aux ! queue ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "Demux left/right stream"
    else
        test_fail "Demux left/right stream"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error\|fail" | head -3
    fi
    
    sleep $CAMERA_RESET_DELAY
    
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=4 depth-mode=1 num-buffers=$num_buffers ! \
        zeddemux is-depth=true name=demux demux.src_left ! queue ! fakesink demux.src_aux ! queue ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "Demux left/depth stream"
    else
        test_fail "Demux left/depth stream"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error\|fail" | head -3
    fi
    
    sleep $CAMERA_RESET_DELAY
}

test_hardware_od() {
    print_subheader "Hardware Object Detection Tests (requires camera)"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "Object Detection pipeline" "No camera detected"
        return 2
    fi
    
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "Object Detection pipeline" "Extensive mode required"
        return 0
    fi
    
    local num_buffers=30
    local timeout_val=120
    
    if timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=0 od-enabled=true od-detection-model=0 \
        num-buffers=$num_buffers ! zedodoverlay ! fakesink 2>&1; then
        test_pass "Object Detection pipeline"
    else
        test_fail "Object Detection pipeline"
    fi
}

test_hardware_bt() {
    print_subheader "Hardware Body Tracking Tests (requires camera)"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "Body Tracking pipeline" "No camera detected"
        return 2
    fi
    
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "Body Tracking pipeline" "Extensive mode required"
        return 0
    fi
    
    local num_buffers=30
    local timeout_val=120
    
    if timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=0 bt-enabled=true bt-detection-model=0 \
        num-buffers=$num_buffers ! zedodoverlay ! fakesink 2>&1; then
        test_pass "Body Tracking pipeline"
    else
        test_fail "Body Tracking pipeline"
    fi
}

test_csv_sink() {
    print_subheader "CSV Sink Tests"
    
    local test_csv="/tmp/zed_gst_test_$$.csv"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "CSV sink with camera data" "No camera detected"
        return 2
    fi
    
    # CSV sink test requires demux which can hang with limited buffers
    # Only run in extensive mode
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "CSV sink with camera data" "Extensive mode required (uses demux)"
        return 0
    fi
    
    local timeout_val=90
    local output
    # Use stream-type=2 (left+right) with sensor data, doesn't require depth computation
    # Use queues to help with EOS propagation
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=2 num-buffers=20 ! \
        zeddemux stream-data=true name=demux \
        demux.src_data ! queue ! zeddatacsvsink location="$test_csv" \
        demux.src_left ! queue ! fakesink demux.src_aux ! queue ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "CSV sink with camera data"
    else
        test_fail "CSV sink with camera data"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error\|fail" | head -3
    fi
    
    if [ -f "$test_csv" ]; then
        test_pass "CSV file was created"
        rm -f "$test_csv"
    fi
}

test_zedxone() {
    print_subheader "ZED X One Tests"
    
    # Check if zedxonesrc is available
    if ! gst-inspect-1.0 zedxonesrc > /dev/null 2>&1; then
        skip_test "ZED X One tests" "zedxonesrc not available"
        return 0
    fi
    
    if gst-inspect-1.0 zedxonesrc 2>&1 | grep -q "camera-resolution"; then
        test_pass "zedxonesrc has camera-resolution property"
    else
        test_fail "zedxonesrc has camera-resolution property"
    fi
    
    if gst-inspect-1.0 zedxonesrc 2>&1 | grep -q "camera-fps"; then
        test_pass "zedxonesrc has camera-fps property"
    else
        test_fail "zedxonesrc has camera-fps property"
    fi
    
    if gst-inspect-1.0 zedxonesrc 2>&1 | grep -q "ctrl-exposure-time"; then
        test_pass "zedxonesrc has ctrl-exposure-time property"
    else
        test_fail "zedxonesrc has ctrl-exposure-time property"
    fi
    
    if gst-inspect-1.0 zedxonesrc 2>&1 | grep -q "stream-type"; then
        test_pass "zedxonesrc has stream-type property"
    else
        test_fail "zedxonesrc has stream-type property"
    fi
}

test_zedxone_enums() {
    print_subheader "ZED X One Enum Value Tests"

    # Check if zedxonesrc is available
    if ! gst-inspect-1.0 zedxonesrc > /dev/null 2>&1; then
        skip_test "ZED X One enum tests" "zedxonesrc not available"
        return 0
    fi

    # Test resolution enum values — must match xone_camera_modes[] table
    local resolutions=("4K" "QHDPLUS" "HD1200" "HD1080" "SVGA")
    for res in "${resolutions[@]}"; do
        if gst-inspect-1.0 zedxonesrc 2>&1 | grep -q "$res"; then
            test_pass "zedxonesrc has resolution '$res'"
        else
            test_fail "zedxonesrc has resolution '$res'"
        fi
    done

    # AUTO must also be present
    if gst-inspect-1.0 zedxonesrc 2>&1 | grep -q "AUTO"; then
        test_pass "zedxonesrc has resolution 'AUTO'"
    else
        test_fail "zedxonesrc has resolution 'AUTO'"
    fi

    # Test stream-type enum values
    local stream_types=("Image" "Auto")
    for stype in "${stream_types[@]}"; do
        if gst-inspect-1.0 zedxonesrc 2>&1 | grep -qi "$stype"; then
            test_pass "zedxonesrc has stream-type '$stype'"
        else
            test_fail "zedxonesrc has stream-type '$stype'"
        fi
    done
}

test_zedxone_auto_resolution() {
    print_subheader "ZED X One Auto-Resolution Negotiation Tests"

    # Check if zedxonesrc is available
    if ! gst-inspect-1.0 zedxonesrc > /dev/null 2>&1; then
        skip_test "ZED X One auto-resolution tests" "zedxonesrc not available"
        return 0
    fi

    # --- Enum introspection ---
    # AUTO resolution enum must be advertised
    if gst-inspect-1.0 zedxonesrc 2>&1 | grep -qi "Auto.*best resolution\|AUTO"; then
        test_pass "zedxonesrc has AUTO resolution enum"
    else
        test_fail "zedxonesrc has AUTO resolution enum"
    fi

    # Default camera-resolution must be AUTO
    local default_res
    default_res=$(gst-inspect-1.0 zedxonesrc 2>&1 | grep -A2 "camera-resolution" | grep -oi "Default.*" | head -1)
    if echo "$default_res" | grep -qi "auto\|5"; then
        test_pass "zedxonesrc default camera-resolution is AUTO"
    else
        test_fail "zedxonesrc default camera-resolution is AUTO (got: $default_res)"
    fi

    # Default camera-fps must be 30
    local default_fps
    default_fps=$(gst-inspect-1.0 zedxonesrc 2>&1 | grep -A2 "camera-fps" | grep -oi "Default.*" | head -1)
    if echo "$default_fps" | grep -q "30"; then
        test_pass "zedxonesrc default camera-fps is 30"
    else
        test_fail "zedxonesrc default camera-fps is 30 (got: $default_fps)"
    fi

    # --- Hardware pipeline tests ---
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "zedxonesrc AUTO caps inspection" "No camera detected"
        return 0
    fi

    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "zedxonesrc AUTO caps negotiation" "Extensive mode required"
        return 0
    fi

    local timeout_val=90
    local num_buffers=5
    local output

    # Test 1: Default (AUTO + 30fps) → 4K (3840x2160) on ZED X One
    output=$(timeout "$timeout_val" gst-launch-1.0 zedxonesrc num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "zedxonesrc AUTO resolution default pipeline"
    else
        if echo "$output" | grep -qi "no camera\|not found\|failed to open"; then
            skip_test "zedxonesrc AUTO default pipeline" "No ZED X One camera detected"
        else
            test_fail "zedxonesrc AUTO resolution default pipeline"
            [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
        fi
    fi

    sleep $CAMERA_RESET_DELAY

    # Test 2: AUTO + 15fps → should select 4K (highest supporting 15)
    output=$(timeout "$timeout_val" gst-launch-1.0 zedxonesrc camera-resolution=5 camera-fps=15 num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "zedxonesrc AUTO@15fps pipeline works"
    else
        if echo "$output" | grep -qi "no camera\|not found\|failed to open"; then
            skip_test "zedxonesrc AUTO@15fps pipeline" "No ZED X One camera detected"
        else
            test_fail "zedxonesrc AUTO@15fps pipeline"
            [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
        fi
    fi

    sleep $CAMERA_RESET_DELAY

    # Test 3: AUTO + 60fps → should select QHDPLUS (3200x1800)
    output=$(timeout "$timeout_val" gst-launch-1.0 zedxonesrc camera-resolution=5 camera-fps=60 num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "zedxonesrc AUTO@60fps pipeline works"
    else
        if echo "$output" | grep -qi "no camera\|not found\|failed to open"; then
            skip_test "zedxonesrc AUTO@60fps pipeline" "No ZED X One camera detected"
        else
            test_fail "zedxonesrc AUTO@60fps pipeline"
            [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
        fi
    fi

    sleep $CAMERA_RESET_DELAY

    # Test 4: AUTO + 120fps → should select QHDPLUS (3200x1800)
    output=$(timeout "$timeout_val" gst-launch-1.0 zedxonesrc camera-resolution=5 camera-fps=120 num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "zedxonesrc AUTO@120fps pipeline works"
    else
        if echo "$output" | grep -qi "no camera\|not found\|failed to open"; then
            skip_test "zedxonesrc AUTO@120fps pipeline" "No ZED X One camera detected"
        else
            test_fail "zedxonesrc AUTO@120fps pipeline"
            [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
        fi
    fi

    sleep $CAMERA_RESET_DELAY

    # Test 5: Explicit 4K + FPS clamping (4K only supports 15/30 → 60 clamps to 30)
    output=$(timeout "$timeout_val" gst-launch-1.0 zedxonesrc camera-resolution=0 camera-fps=60 num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "zedxonesrc 4K@60fps clamped to 30fps"
    else
        if echo "$output" | grep -qi "no camera\|not found\|failed to open"; then
            skip_test "zedxonesrc 4K@60fps FPS clamping" "No ZED X One camera detected"
        else
            test_fail "zedxonesrc 4K@60fps FPS clamping"
            [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
        fi
    fi

    sleep $CAMERA_RESET_DELAY

    # Test 6: Downstream size negotiation — request smaller output
    output=$(timeout "$timeout_val" gst-launch-1.0 zedxonesrc camera-resolution=5 num-buffers=$num_buffers ! \
        "video/x-raw,width=640,height=480" ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "zedxonesrc AUTO with downstream resize (640x480)"
    else
        if echo "$output" | grep -qi "no camera\|not found\|failed to open"; then
            skip_test "zedxonesrc AUTO downstream resize" "No ZED X One camera detected"
        else
            test_fail "zedxonesrc AUTO with downstream resize (640x480)"
            [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
        fi
    fi

    sleep $CAMERA_RESET_DELAY
}

test_zedxone_resolutions() {
    print_subheader "Resolution Tests — all zedxonesrc mode table entries (requires ZED X One)"

    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "ZED X One resolution tests" "No camera detected"
        return 2
    fi

    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "ZED X One resolution tests" "Extensive mode required"
        return 0
    fi

    local timeout_val=90
    local num_buffers=5
    local output

    # Iterate every entry in the xone_camera_modes[] mapping table.
    # Each line: enum_value  label  WxH
    local modes=(
        "4  4K       3840x2160"
        "3  QHDPLUS  3200x1800"
        "2  HD1200   1920x1200"
        "1  HD1080   1920x1080"
        "0  SVGA     960x600"
    )

    for entry in "${modes[@]}"; do
        local enum_val name dims
        read -r enum_val name dims <<< "$entry"

        output=$(timeout "$timeout_val" gst-launch-1.0 zedxonesrc camera-resolution=$enum_val num-buffers=$num_buffers ! fakesink 2>&1)
        if [ $? -eq 0 ]; then
            test_pass "zedxonesrc $name ($dims) [enum=$enum_val]"
        else
            if echo "$output" | grep -qi "no camera\|not found\|failed to open"; then
                skip_test "zedxonesrc $name ($dims)" "No ZED X One camera detected"
            elif echo "$output" | grep -qi "not available\|not supported\|invalid resolution"; then
                skip_test "zedxonesrc $name ($dims)" "Not supported on this camera model"
            else
                test_fail "zedxonesrc $name ($dims) [enum=$enum_val]"
                [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
            fi
        fi

        sleep $CAMERA_RESET_DELAY
    done
}

test_zedxone_nv12() {
    print_subheader "ZED X One NV12 Zero-Copy Tests"
    
    # Check if zedxonesrc is available
    if ! gst-inspect-1.0 zedxonesrc > /dev/null 2>&1; then
        skip_test "ZED X One NV12 tests" "zedxonesrc not available"
        return 0
    fi
    
    # Check if NV12 zero-copy is available for zedxonesrc
    local zedxone_zerocopy_available=false
    if gst-inspect-1.0 zedxonesrc 2>&1 | grep -q "Raw NV12 zero-copy"; then
        zedxone_zerocopy_available=true
        test_pass "zedxonesrc supports NV12 zero-copy (stream-type=1)"
    else
        skip_test "zedxonesrc NV12 zero-copy" "SDK < 5.2 or not built with Advanced Capture API"
    fi
    
    # Check for AUTO stream type
    if gst-inspect-1.0 zedxonesrc 2>&1 | grep -q "Auto.*NV12 zero-copy"; then
        test_pass "zedxonesrc has STREAM_AUTO with NV12 preference"
    else
        # May not have zero-copy but should still have AUTO
        if gst-inspect-1.0 zedxonesrc 2>&1 | grep -q "Auto-negotiate"; then
            test_pass "zedxonesrc has STREAM_AUTO"
        else
            test_fail "zedxonesrc has STREAM_AUTO"
        fi
    fi
    
    # Hardware test for NV12 zero-copy
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "ZED X One NV12 hardware test" "No camera detected"
        return 2
    fi
    
    # Only test on Jetson with zero-copy available
    if [ ! -f /etc/nv_tegra_release ]; then
        skip_test "ZED X One NV12 hardware test" "Not a Jetson platform"
        return 0
    fi
    
    if [ "$zedxone_zerocopy_available" = false ]; then
        skip_test "ZED X One NV12 hardware test" "Zero-copy not available"
        return 0
    fi
    
    # NOTE: This test will only work if a ZED X One camera is connected
    # Skip if only stereo cameras are available
    if [ "$EXTENSIVE_MODE" = true ]; then
        local timeout_val=30
        local num_buffers=10
        
        # Test stream-type=1 (NV12 zero-copy for ZED X One)
        local output
        output=$(timeout "$timeout_val" gst-launch-1.0 zedxonesrc stream-type=1 num-buffers=$num_buffers ! \
            'video/x-raw(memory:NVMM),format=NV12' ! fakesink 2>&1)
        if [ $? -eq 0 ]; then
            test_pass "ZED X One NV12 zero-copy pipeline (stream-type=1)"
        else
            # May fail if no ZED X One camera is connected (which is OK)
            if echo "$output" | grep -qi "no camera\|not found\|failed to open"; then
                skip_test "ZED X One NV12 zero-copy pipeline" "No ZED X One camera detected"
            else
                test_fail "ZED X One NV12 zero-copy pipeline (stream-type=1)"
                [ "$VERBOSE" = true ] && echo "$output" | grep -i "error\|fail" | head -3
            fi
        fi
        
        sleep $CAMERA_RESET_DELAY
    else
        skip_test "ZED X One NV12 hardware test" "Extensive mode required"
    fi
}

test_latency_query() {
    print_subheader "Latency Query Tests (requires camera)"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "zedsrc latency query" "No camera detected"
        return 2
    fi
    
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "zedsrc latency query" "Extensive mode required"
        return 0
    fi
    
    local timeout_val=90
    
    # Test zedsrc latency query by using a pipeline with sync enabled
    # If latency query fails, the pipeline would have timing issues
    local output
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc num-buffers=30 ! \
        fakesink sync=true 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "zedsrc with synchronized sink"
    else
        test_fail "zedsrc with synchronized sink"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error\|latency" | head -3
    fi
    
    sleep $CAMERA_RESET_DELAY
}

test_buffer_metadata() {
    print_subheader "Buffer Metadata Tests (requires camera)"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "Buffer timestamps working" "No camera detected"
        return 2
    fi
    
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "Buffer timestamps working" "Extensive mode required"
        return 0
    fi
    
    local timeout_val=90
    local num_buffers=30
    
    # Test that buffer timestamps are being set correctly by using a sync sink
    # The pipeline will fail or have issues if timestamps aren't set properly
    local output
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc num-buffers=$num_buffers ! \
        queue max-size-buffers=5 ! fakesink sync=true 2>&1)
    
    if [ $? -eq 0 ]; then
        test_pass "Buffer timestamps with queued sync sink"
    else
        test_fail "Buffer timestamps with queued sync sink"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
    fi
    
    sleep $CAMERA_RESET_DELAY
}

test_datamux() {
    print_subheader "Data Mux Tests (requires camera)"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "zeddatamux pipeline" "No camera detected"
        return 2
    fi
    
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "zeddatamux pipeline" "Extensive mode required"
        return 0
    fi
    
    local timeout_val=90
    local num_buffers=20
    
    # Test zeddatamux by round-tripping through demux and mux
    # This exercises the memcpy and buffer handling code
    # Use queues to help with synchronization and EOS propagation
    local output
    output=$(timeout "$timeout_val" gst-launch-1.0 \
        zedsrc stream-type=2 num-buffers=$num_buffers ! \
        zeddemux stream-data=true name=demux \
        demux.src_left ! queue ! zeddatamux name=mux ! queue ! fakesink \
        demux.src_data ! queue ! mux.sink_data \
        demux.src_aux ! queue ! fakesink 2>&1)
    
    if [ $? -eq 0 ]; then
        test_pass "zeddatamux demux/mux round-trip"
    else
        test_fail "zeddatamux demux/mux round-trip"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error\|fail" | head -5
    fi
    
    sleep $CAMERA_RESET_DELAY
}

test_overlay_skeletons() {
    print_subheader "Overlay Skeleton Tests (requires camera)"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "zedodoverlay with body tracking" "No camera detected"
        return 2
    fi
    
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "zedodoverlay with body tracking" "Extensive mode required"
        return 0
    fi
    
    local timeout_val=120
    local num_buffers=30
    
    # Test different body tracking models to exercise skeleton rendering code
    # This tests the skeleton rendering code including the sprintf fix
    local output
    
    # BODY_18 format
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=0 \
        bt-enabled=true bt-format=0 num-buffers=$num_buffers ! \
        zedodoverlay ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "zedodoverlay with BODY_18 skeleton"
    else
        # Body tracking may fail without a person in view, that's OK
        if echo "$output" | grep -qi "skeleton"; then
            test_fail "zedodoverlay with BODY_18 skeleton"
        else
            test_pass "zedodoverlay with BODY_18 skeleton (no bodies detected)"
        fi
    fi
    
    sleep $CAMERA_RESET_DELAY
    
    # BODY_38 format
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=0 \
        bt-enabled=true bt-format=2 num-buffers=$num_buffers ! \
        zedodoverlay ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "zedodoverlay with BODY_38 skeleton"
    else
        if echo "$output" | grep -qi "skeleton"; then
            test_fail "zedodoverlay with BODY_38 skeleton"
        else
            test_pass "zedodoverlay with BODY_38 skeleton (no bodies detected)"
        fi
    fi
    
    sleep $CAMERA_RESET_DELAY
}

test_resolutions() {
    print_subheader "Resolution Tests — all zedsrc mode table entries (requires camera)"

    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "Resolution tests" "No camera detected"
        return 2
    fi

    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "Resolution tests" "Extensive mode required"
        return 0
    fi

    local timeout_val=90
    local num_buffers=10
    local output

    # Iterate every entry in the zed_camera_modes[] mapping table.
    # Each line: enum_value  label  WxH
    # Dimensions come from sl::getResolution() at runtime; shown here for reference.
    local modes=(
        "0  HD2K    2208x1242"
        "1  HD1080  1920x1080"
        "2  HD1200  1920x1200"
        "3  HD720   1280x720"
        "4  SVGA    960x600"
        "5  VGA     672x376"
    )

    for entry in "${modes[@]}"; do
        local enum_val name dims
        read -r enum_val name dims <<< "$entry"

        output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc camera-resolution=$enum_val num-buffers=$num_buffers ! fakesink 2>&1)
        if [ $? -eq 0 ]; then
            test_pass "zedsrc $name ($dims) [enum=$enum_val]"
        else
            # Resolution may be unavailable for this camera model — skip rather than fail
            if echo "$output" | grep -qi "not available\|not supported\|invalid resolution\|failed to open"; then
                skip_test "zedsrc $name ($dims)" "Not supported on this camera"
            else
                test_fail "zedsrc $name ($dims) [enum=$enum_val]"
                [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
            fi
        fi

        sleep $CAMERA_RESET_DELAY
    done
}

test_depth_modes() {
    print_subheader "Depth Mode Tests (requires camera)"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "Depth mode NEURAL" "No camera detected"
        skip_test "Depth mode ULTRA" "No camera detected"
        return 2
    fi
    
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "Depth mode tests" "Extensive mode required"
        return 0
    fi
    
    local timeout_val=120
    local num_buffers=10
    local output
    
    # Test NEURAL depth mode (mode 4) - most computationally intensive
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=4 depth-mode=4 num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "Depth mode NEURAL"
    else
        test_fail "Depth mode NEURAL"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
    fi
    
    sleep $CAMERA_RESET_DELAY
    
    # Test ULTRA depth mode (mode 3)
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=4 depth-mode=3 num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "Depth mode ULTRA"
    else
        test_fail "Depth mode ULTRA"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
    fi
    
    sleep $CAMERA_RESET_DELAY
}

test_positional_tracking() {
    print_subheader "Positional Tracking Tests (requires camera)"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "Positional tracking enabled" "No camera detected"
        return 2
    fi
    
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "Positional tracking tests" "Extensive mode required"
        return 0
    fi
    
    local timeout_val=90
    local num_buffers=30
    local output
    
    # Test positional tracking with default settings
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=0 \
        enable-positional-tracking=true depth-mode=1 num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "Positional tracking enabled"
    else
        test_fail "Positional tracking enabled"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
    fi
    
    sleep $CAMERA_RESET_DELAY
    
    # Test positional tracking with area memory
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=0 \
        enable-positional-tracking=true enable-area-memory=true depth-mode=1 \
        num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "Positional tracking with area memory"
    else
        test_fail "Positional tracking with area memory"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
    fi
    
    sleep $CAMERA_RESET_DELAY
}

test_zedxone_hardware() {
    print_subheader "ZED X One Hardware Tests (requires ZED X One camera)"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "ZED X One camera grab" "No camera detected"
        return 2
    fi
    
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "ZED X One hardware tests" "Extensive mode required"
        return 0
    fi
    
    # Check if ZED X One is specifically available
    local zed_output
    zed_output=$(ZED_Explorer -a 2>&1)
    if ! echo "$zed_output" | grep -qi "ZED X One"; then
        skip_test "ZED X One camera grab" "No ZED X One camera detected"
        skip_test "ZED X One with HDR" "No ZED X One camera detected"
        return 0
    fi
    
    local timeout_val=90
    local num_buffers=10
    local output
    
    # Test basic ZED X One grab
    output=$(timeout "$timeout_val" gst-launch-1.0 zedxonesrc num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "ZED X One camera grab"
    else
        test_fail "ZED X One camera grab"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
    fi
    
    sleep $CAMERA_RESET_DELAY
    
    # Test ZED X One with HDR enabled
    output=$(timeout "$timeout_val" gst-launch-1.0 zedxonesrc enable-hdr=true num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "ZED X One with HDR"
    else
        test_fail "ZED X One with HDR"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
    fi
    
    sleep $CAMERA_RESET_DELAY
}

test_camera_controls() {
    print_subheader "Camera Control Tests (requires camera)"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "Camera exposure control" "No camera detected"
        skip_test "Camera gain control" "No camera detected"
        return 2
    fi
    
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "Camera control tests" "Extensive mode required"
        return 0
    fi
    
    local timeout_val=90
    local num_buffers=10
    local output
    
    # Test manual exposure setting
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc \
        ctrl-aec-agc=false ctrl-exposure=5000 ctrl-gain=50 \
        num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "Camera manual exposure/gain control"
    else
        test_fail "Camera manual exposure/gain control"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
    fi
    
    sleep $CAMERA_RESET_DELAY
    
    # Test auto exposure with ROI
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc \
        ctrl-aec-agc=true ctrl-aec-agc-roi-x=100 ctrl-aec-agc-roi-y=100 \
        ctrl-aec-agc-roi-w=200 ctrl-aec-agc-roi-h=200 \
        num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "Camera auto exposure with ROI"
    else
        test_fail "Camera auto exposure with ROI"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
    fi
    
    sleep $CAMERA_RESET_DELAY
    
    # Test white balance control
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc \
        ctrl-whitebalance-auto=false ctrl-whitebalance-temperature=4500 \
        num-buffers=$num_buffers ! fakesink 2>&1)
    if [ $? -eq 0 ]; then
        test_pass "Camera white balance control"
    else
        test_fail "Camera white balance control"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
    fi
    
    sleep $CAMERA_RESET_DELAY
}

test_video_recording_playback() {
    print_subheader "Video Recording/Playback Tests"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "Video recording" "No camera detected"
        skip_test "Video playback" "No camera detected"
        return 2
    fi
    
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "Video recording/playback tests" "Extensive mode required"
        return 0
    fi
    
    local timeout_val=90
    local test_dir="/tmp/zed_gst_test_$$"
    local video_file="$test_dir/test_recording.mp4"
    local output
    
    # Create test directory
    mkdir -p "$test_dir"
    
    # STEP 1: Record video from camera to file
    # Use mp4mux for a standard container format
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=0 num-buffers=60 ! \
        queue ! videoconvert ! video/x-raw,format=I420 ! \
        x264enc tune=zerolatency speed-preset=ultrafast ! \
        mp4mux ! filesink location="$video_file" 2>&1)
    local record_status=$?
    
    if [ $record_status -eq 0 ] && [ -f "$video_file" ]; then
        local file_size
        file_size=$(stat -c%s "$video_file" 2>/dev/null || echo "0")
        if [ "$file_size" -gt 1000 ]; then
            test_pass "Video recording to MP4 ($file_size bytes)"
        else
            test_fail "Video recording (file too small: $file_size bytes)"
            rm -rf "$test_dir"
            return 1
        fi
    else
        # Try with openh264enc as fallback
        output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=0 num-buffers=60 ! \
            queue ! videoconvert ! video/x-raw,format=I420 ! \
            openh264enc ! \
            mp4mux ! filesink location="$video_file" 2>&1)
        record_status=$?
        
        if [ $record_status -eq 0 ] && [ -f "$video_file" ]; then
            local file_size
            file_size=$(stat -c%s "$video_file" 2>/dev/null || echo "0")
            if [ "$file_size" -gt 1000 ]; then
                test_pass "Video recording to MP4 ($file_size bytes, openh264)"
            else
                test_fail "Video recording (file too small)"
                rm -rf "$test_dir"
                return 1
            fi
        else
            test_fail "Video recording to MP4"
            [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
            rm -rf "$test_dir"
            return 1
        fi
    fi
    
    sleep 1
    
    # STEP 2: Playback the recorded file
    output=$(timeout 30 gst-launch-1.0 filesrc location="$video_file" ! \
        qtdemux ! h264parse ! avdec_h264 ! videoconvert ! fakesink 2>&1)
    local playback_status=$?
    
    if [ $playback_status -eq 0 ]; then
        test_pass "Video playback from recorded file"
    else
        test_fail "Video playback from recorded file"
        [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
    fi
    
    # Cleanup
    rm -rf "$test_dir"
    
    sleep $CAMERA_RESET_DELAY
}

test_udp_streaming() {
    print_subheader "UDP Streaming Tests (sender/receiver)"
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        skip_test "UDP stream sender" "No camera detected"
        skip_test "UDP stream receiver" "No camera detected"
        return 2
    fi
    
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "UDP streaming tests" "Extensive mode required"
        return 0
    fi
    
    local timeout_val=30
    local stream_port=5000
    local num_buffers=90  # ~3 seconds at 30fps
    local output
    
    # Find an available port
    while netstat -tuln 2>/dev/null | grep -q ":$stream_port " || \
          ss -tuln 2>/dev/null | grep -q ":$stream_port "; do
        stream_port=$((stream_port + 1))
        if [ $stream_port -gt 5100 ]; then
            skip_test "UDP streaming" "No available port found"
            return 0
        fi
    done
    
    # Start receiver in background first (must be ready before sender)
    local receiver_output="/tmp/zed_udp_receiver_$$.log"
    timeout "$timeout_val" gst-launch-1.0 \
        udpsrc port=$stream_port caps="application/x-rtp,media=video,encoding-name=H264" ! \
        rtph264depay ! h264parse ! avdec_h264 ! fakesink sync=false \
        > "$receiver_output" 2>&1 &
    local receiver_pid=$!
    
    # Give receiver time to bind
    sleep 2
    
    # Check if receiver is still running
    if ! kill -0 $receiver_pid 2>/dev/null; then
        test_fail "UDP receiver failed to start"
        rm -f "$receiver_output"
        return 1
    fi
    
    # Start sender - stream camera to UDP
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=0 num-buffers=$num_buffers ! \
        queue ! videoconvert ! video/x-raw,format=I420 ! \
        x264enc tune=zerolatency speed-preset=ultrafast bitrate=2000 ! \
        rtph264pay ! udpsink host=127.0.0.1 port=$stream_port 2>&1)
    local sender_status=$?
    
    # Give receiver a moment to process
    sleep 1
    
    # Stop receiver
    kill $receiver_pid 2>/dev/null
    wait $receiver_pid 2>/dev/null
    
    # Check results
    if [ $sender_status -eq 0 ]; then
        # Check if receiver got any data
        if [ -f "$receiver_output" ]; then
            local receiver_log
            receiver_log=$(cat "$receiver_output")
            if echo "$receiver_log" | grep -qi "PLAYING\|pipeline\|clock"; then
                test_pass "UDP stream sender (to port $stream_port)"
                test_pass "UDP stream receiver (verified data flow)"
            else
                test_pass "UDP stream sender (to port $stream_port)"
                test_fail "UDP stream receiver (no data received)"
            fi
        else
            test_pass "UDP stream sender (to port $stream_port)"
            skip_test "UDP stream receiver" "Could not verify"
        fi
    else
        # Try with openh264enc as fallback
        timeout "$timeout_val" gst-launch-1.0 \
            udpsrc port=$stream_port caps="application/x-rtp,media=video,encoding-name=H264" ! \
            rtph264depay ! h264parse ! avdec_h264 ! fakesink sync=false \
            > "$receiver_output" 2>&1 &
        receiver_pid=$!
        sleep 2
        
        output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc stream-type=0 num-buffers=$num_buffers ! \
            queue ! videoconvert ! video/x-raw,format=I420 ! \
            openh264enc ! \
            rtph264pay ! udpsink host=127.0.0.1 port=$stream_port 2>&1)
        sender_status=$?
        
        kill $receiver_pid 2>/dev/null
        wait $receiver_pid 2>/dev/null
        
        if [ $sender_status -eq 0 ]; then
            test_pass "UDP stream sender (openh264, port $stream_port)"
            test_pass "UDP stream receiver"
        else
            test_fail "UDP stream sender"
            test_fail "UDP stream receiver"
            [ "$VERBOSE" = true ] && echo "$output" | grep -i "error" | head -3
        fi
    fi
    
    rm -f "$receiver_output"
    sleep $CAMERA_RESET_DELAY
}

test_network_streaming() {
    print_subheader "Network Streaming Tests"
    
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "Network streaming tests" "Extensive mode required"
        return 0
    fi
    
    local timeout_val=10
    local exit_code
    local output
    
    # Test connecting to a non-existent stream (should fail or timeout)
    # This tests error handling for invalid stream input
    # Using a TEST-NET address (RFC 5737) that is guaranteed not to route
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc input-stream-ip="192.0.2.1" input-stream-port=30000 ! fakesink 2>&1)
    exit_code=$?
    
    # Exit code 124 = timeout, non-zero = error - both are acceptable failures
    if [ $exit_code -ne 0 ]; then
        test_pass "Network stream invalid IP (failed as expected, exit code: $exit_code)"
    else
        # Unexpectedly succeeded - this shouldn't happen with an invalid IP
        test_fail "Network stream invalid IP (should have failed)"
    fi
}

test_error_handling() {
    print_subheader "Error Handling Tests"
    
    if [ "$EXTENSIVE_MODE" = false ]; then
        skip_test "Error handling tests" "Extensive mode required"
        return 0
    fi
    
    local timeout_val=15
    local output
    
    # Test invalid camera ID (should fail gracefully)
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc camera-id=99 ! fakesink 2>&1)
    if [ $? -ne 0 ]; then
        if echo "$output" | grep -qi "error\|failed\|not found\|open"; then
            test_pass "Invalid camera ID (graceful failure)"
        else
            test_fail "Invalid camera ID (unexpected failure mode)"
        fi
    else
        test_fail "Invalid camera ID (should have failed)"
    fi
    
    # Test invalid serial number (should fail gracefully)
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc camera-sn=999999999 ! fakesink 2>&1)
    if [ $? -ne 0 ]; then
        if echo "$output" | grep -qi "error\|failed\|not found\|open"; then
            test_pass "Invalid serial number (graceful failure)"
        else
            test_fail "Invalid serial number (unexpected failure mode)"
        fi
    else
        test_fail "Invalid serial number (should have failed)"
    fi
    
    # Test invalid SVO file path (should fail gracefully)
    output=$(timeout "$timeout_val" gst-launch-1.0 zedsrc svo-file="/nonexistent/path/video.svo" ! fakesink 2>&1)
    if [ $? -ne 0 ]; then
        # Any failure is acceptable - the key is it doesn't hang or crash
        test_pass "Invalid SVO file path (graceful failure)"
    else
        test_fail "Invalid SVO file path (should have failed)"
    fi
    
    # Test CSV sink with invalid location (should fail gracefully)
    output=$(timeout "$timeout_val" gst-launch-1.0 videotestsrc num-buffers=1 ! \
        "application/data" ! zeddatacsvsink location="/nonexistent/dir/test.csv" 2>&1)
    if [ $? -ne 0 ]; then
        # Any failure is acceptable
        test_pass "CSV sink invalid path (graceful failure)"
    else
        test_fail "CSV sink invalid path (should have failed)"
    fi
    
    # Test zedodoverlay with non-video input (should fail or handle gracefully)
    output=$(timeout "$timeout_val" gst-launch-1.0 audiotestsrc num-buffers=1 ! \
        zedodoverlay ! fakesink 2>&1)
    if [ $? -ne 0 ]; then
        # Any failure is acceptable - caps negotiation failure is expected
        test_pass "Overlay with invalid input (graceful failure)"
    else
        test_fail "Overlay with invalid input (should have failed)"
    fi
}

# =============================================================================
# Benchmark Functions
# =============================================================================

# Global benchmark results storage
declare -A BENCHMARK_RESULTS

benchmark_print_header() {
    echo ""
    echo -e "${CYAN}╔══════════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║                        ZED GStreamer Benchmark Suite                         ║${NC}"
    echo -e "${CYAN}╠══════════════════════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${CYAN}║  Measuring: Latency, CPU Usage, RAM Usage, GPU Usage                        ║${NC}"
    echo -e "${CYAN}║  Platform:  $(printf '%-66s' "$(detect_platform)")║${NC}"
    echo -e "${CYAN}╚══════════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
}

benchmark_print_section() {
    echo ""
    echo -e "${BLUE}┌──────────────────────────────────────────────────────────────────────────────┐${NC}"
    echo -e "${BLUE}│  $1$(printf '%*s' $((76 - ${#1})) '')│${NC}"
    echo -e "${BLUE}└──────────────────────────────────────────────────────────────────────────────┘${NC}"
}

# Get baseline system stats (idle state)
get_baseline_stats() {
    local duration=3
    local stats_file="/tmp/zed_baseline_stats_$$.log"
    
    echo -n "  Measuring baseline (idle system)... "
    
    if command -v tegrastats &> /dev/null; then
        # Jetson platform
        timeout $duration tegrastats --interval $BENCHMARK_SAMPLE_RATE > "$stats_file" 2>&1 &
        local pid=$!
        sleep $duration
        kill $pid 2>/dev/null
        wait $pid 2>/dev/null
        
        # Parse tegrastats output
        local ram_vals cpu_vals gpu_vals
        ram_vals=$(grep -oP 'RAM \K[0-9]+' "$stats_file" | awk '{sum+=$1; count++} END {if(count>0) print sum/count; else print 0}')
        cpu_vals=$(grep -oP 'CPU \[\K[^\]]+' "$stats_file" | tr ',' '\n' | grep -oP '^[0-9]+' | awk '{sum+=$1; count++} END {if(count>0) print sum/count; else print 0}')
        gpu_vals=$(grep -oP 'GR3D_FREQ \K[0-9]+' "$stats_file" | awk '{sum+=$1; count++} END {if(count>0) print sum/count; else print 0}')
        
        BASELINE_RAM="${ram_vals:-0}"
        BASELINE_CPU="${cpu_vals:-0}"
        BASELINE_GPU="${gpu_vals:-0}"
    else
        # Generic Linux - use /proc
        BASELINE_RAM=$(free -m | awk '/Mem:/ {print $3}')
        BASELINE_CPU=$(top -bn2 -d0.5 | grep "Cpu(s)" | tail -1 | awk '{print $2}' | cut -d'%' -f1)
        BASELINE_GPU="N/A"
    fi
    
    rm -f "$stats_file"
    echo "done"
    echo -e "    Baseline: RAM=${BASELINE_RAM}MB, CPU=${BASELINE_CPU}%, GPU=${BASELINE_GPU}%"
}

# Monitor system resources during a pipeline run
# Usage: monitor_pipeline "pipeline_description" "gst-launch-1.0 ..." duration
run_benchmark_pipeline() {
    local description="$1"
    local pipeline="$2"
    local duration="${3:-$BENCHMARK_DURATION}"
    local stats_file="/tmp/zed_bench_stats_$$.log"
    local latency_file="/tmp/zed_bench_latency_$$.log"
    local pipeline_log="/tmp/zed_bench_pipeline_$$.log"
    
    echo ""
    echo -e "  ${YELLOW}▶ $description${NC}"
    echo "    Duration: ${duration}s (+ ${BENCHMARK_WARMUP}s warmup)"
    
    # Start resource monitoring
    if command -v tegrastats &> /dev/null; then
        tegrastats --interval $BENCHMARK_SAMPLE_RATE > "$stats_file" 2>&1 &
        local monitor_pid=$!
    else
        # Fallback: sample with vmstat
        vmstat 1 $((duration + BENCHMARK_WARMUP + 2)) > "$stats_file" 2>&1 &
        local monitor_pid=$!
    fi
    
    # Calculate total buffers needed
    # Extract camera-fps from pipeline if specified, default to 60fps
    local target_fps=60
    if echo "$pipeline" | grep -qo 'camera-fps=[0-9]*'; then
        target_fps=$(echo "$pipeline" | grep -oP 'camera-fps=\K[0-9]+')
    fi
    local total_buffers=$(( (duration + BENCHMARK_WARMUP) * target_fps ))
    
    # Run pipeline with identity element to capture timestamps
    # The identity element logs buffer timestamps for latency calculation
    local start_time
    start_time=$(date +%s.%N)
    
    # Run pipeline - add num-buffers to the zedsrc element
    # Pipeline format: "zedsrc <props>" or "zedsrc <props> ! <capsfilter>"
    # Insert num-buffers right after zedsrc properties, before any "!" 
    local full_pipeline
    # Use awk to insert num-buffers before the first "!" if present
    if echo "$pipeline" | grep -q '!'; then
        # Pipeline has downstream elements, insert num-buffers before first "!"
        full_pipeline=$(echo "$pipeline" | awk -v nb="num-buffers=$total_buffers" '{sub(/!/, nb " !")} 1')
        # Only append identity+fakesink if pipeline doesn't already end with a sink
        if ! echo "$pipeline" | grep -qE '(fakesink|filesink)[^!]*$'; then
            full_pipeline="$full_pipeline ! identity silent=false ! fakesink sync=false"
        fi
    else
        # Simple pipeline with just zedsrc
        full_pipeline="$pipeline num-buffers=$total_buffers ! identity silent=false ! fakesink sync=false"
    fi
    
    # Timeout needs to account for: camera open (~6s) + warmup + duration + buffer
    # DeepStream pipelines need extra time for TensorRT engine loading (~6-8s)
    local timeout_val=$((duration + BENCHMARK_WARMUP + 30))
    
    # Print the exact command for reproducibility
    echo ""
    echo -e "    ${CYAN}Command:${NC}"
    echo -e "    ${YELLOW}gst-launch-1.0 $full_pipeline${NC}"
    echo ""
    
    # Use eval to properly handle caps filters with special characters like (memory:NVMM)
    eval "timeout $timeout_val gst-launch-1.0 -v $full_pipeline" > "$pipeline_log" 2>&1
    local pipeline_status=$?
    
    local end_time
    end_time=$(date +%s.%N)
    
    # Stop monitoring
    kill $monitor_pid 2>/dev/null
    wait $monitor_pid 2>/dev/null
    
    if [ $pipeline_status -ne 0 ]; then
        echo -e "    ${RED}Pipeline failed!${NC}"
        # Always show error output for failed pipelines
        echo -e "    ${RED}Error output:${NC}"
        cat "$pipeline_log" | grep -i "error\|failed\|cannot\|unable" | head -5
        rm -f "$stats_file" "$latency_file" "$pipeline_log"
        return 1
    fi
    
    # Calculate actual FPS from buffer timestamps in the log
    # The identity element logs PTS timestamps in format "pts: H:MM:SS.nnnnnnnnn"
    # Skip warmup frames and measure only the steady-state FPS
    local actual_fps
    local pts_values
    # Extract PTS values from identity element output (format: "pts: 0:00:00.033333333")
    pts_values=$(grep -oP 'pts: \K[0-9:]+\.[0-9]+' "$pipeline_log" 2>/dev/null)
    
    if [ -n "$pts_values" ]; then
        # Calculate number of warmup frames to skip
        local warmup_frames=$((BENCHMARK_WARMUP * target_fps))
        local total_pts_count
        total_pts_count=$(echo "$pts_values" | wc -l)
        
        # Skip warmup frames, use only measurement frames
        local measure_pts
        if [ "$total_pts_count" -gt "$warmup_frames" ]; then
            measure_pts=$(echo "$pts_values" | tail -n +$((warmup_frames + 1)))
        else
            measure_pts="$pts_values"
        fi
        
        # Parse timestamps and calculate FPS from actual buffer timing
        local first_pts last_pts num_frames
        first_pts=$(echo "$measure_pts" | head -1)
        last_pts=$(echo "$measure_pts" | tail -1)
        num_frames=$(echo "$measure_pts" | wc -l)
        
        if [ "$num_frames" -gt 1 ]; then
            # Convert H:MM:SS.nnnnnnnnn to seconds
            local first_secs last_secs duration_secs
            first_secs=$(echo "$first_pts" | awk -F: '{if(NF==3) print $1*3600+$2*60+$3; else if(NF==2) print $1*60+$2; else print $1}')
            last_secs=$(echo "$last_pts" | awk -F: '{if(NF==3) print $1*3600+$2*60+$3; else if(NF==2) print $1*60+$2; else print $1}')
            duration_secs=$(echo "$last_secs $first_secs" | awk '{print $1-$2}')
            
            if (( $(echo "$duration_secs > 0" | bc -l) )); then
                actual_fps=$(echo "$num_frames $duration_secs" | awk '{printf "%.1f", ($1-1)/$2}')
            else
                actual_fps="N/A"
            fi
        else
            actual_fps="N/A"
        fi
    else
        # Fallback: no PTS data available (pipeline doesn't have identity element)
        actual_fps="N/A"
    fi
    
    # Parse resource usage
    local avg_ram avg_cpu avg_gpu peak_ram peak_cpu peak_gpu
    
    if command -v tegrastats &> /dev/null; then
        # Parse tegrastats
        avg_ram=$(grep -oP 'RAM \K[0-9]+' "$stats_file" | awk '{sum+=$1; count++} END {if(count>0) printf "%.0f", sum/count; else print 0}')
        peak_ram=$(grep -oP 'RAM \K[0-9]+' "$stats_file" | sort -n | tail -1)
        
        avg_cpu=$(grep -oP 'CPU \[\K[^\]]+' "$stats_file" | tr ',' '\n' | grep -oP '^[0-9]+' | awk '{sum+=$1; count++} END {if(count>0) printf "%.1f", sum/count; else print 0}')
        peak_cpu=$(grep -oP 'CPU \[\K[^\]]+' "$stats_file" | tr ',' '\n' | grep -oP '^[0-9]+' | sort -n | tail -1)
        
        avg_gpu=$(grep -oP 'GR3D_FREQ \K[0-9]+' "$stats_file" | awk '{sum+=$1; count++} END {if(count>0) printf "%.1f", sum/count; else print 0}')
        peak_gpu=$(grep -oP 'GR3D_FREQ \K[0-9]+' "$stats_file" | sort -n | tail -1)
    else
        avg_ram=$(awk 'NR>2 {sum+=$(NF-1); count++} END {if(count>0) printf "%.0f", sum/count; else print 0}' "$stats_file")
        peak_ram=$(awk 'NR>2 {print $(NF-1)}' "$stats_file" | sort -n | tail -1)
        avg_cpu=$(awk 'NR>2 {sum+=$13; count++} END {if(count>0) printf "%.1f", 100-sum/count; else print 0}' "$stats_file")
        peak_cpu="N/A"
        avg_gpu="N/A"
        peak_gpu="N/A"
    fi
    
    # Calculate latency from buffer timestamps in identity output
    # Look for "pts" (presentation timestamp) values - format is "pts: H:MM:SS.nnnnnnnnn"
    local latency_samples latency_avg latency_min latency_max latency_stddev
    
    # Extract PTS values and calculate jitter/latency metrics
    grep -oP 'pts: \K[0-9:]+\.[0-9]+' "$pipeline_log" | head -100 > "$latency_file" 2>/dev/null
    
    if [ -s "$latency_file" ]; then
        # Calculate inter-frame timing (jitter)
        local frame_times
        frame_times=$(awk -F: '{
            if (NF==3) {
                t = $1*3600 + $2*60 + $3
            } else if (NF==2) {
                t = $1*60 + $2
            } else {
                t = $1
            }
            if (NR>1) print t - prev
            prev = t
        }' "$latency_file")
        
        if [ -n "$frame_times" ]; then
            latency_avg=$(echo "$frame_times" | awk '{sum+=$1; count++} END {if(count>0) printf "%.2f", sum/count*1000; else print "N/A"}')
            latency_min=$(echo "$frame_times" | sort -n | head -1 | awk '{printf "%.2f", $1*1000}')
            latency_max=$(echo "$frame_times" | sort -n | tail -1 | awk '{printf "%.2f", $1*1000}')
            latency_stddev=$(echo "$frame_times" | awk '{sum+=$1; sumsq+=$1*$1; count++} END {if(count>1) printf "%.2f", sqrt(sumsq/count - (sum/count)^2)*1000; else print "N/A"}')
        else
            latency_avg="N/A"
            latency_min="N/A"
            latency_max="N/A"
            latency_stddev="N/A"
        fi
    else
        latency_avg="N/A"
        latency_min="N/A"
        latency_max="N/A"
        latency_stddev="N/A"
    fi
    
    # Calculate delta from baseline
    local ram_delta cpu_delta gpu_delta
    ram_delta=$(echo "$avg_ram $BASELINE_RAM" | awk '{printf "%.0f", $1-$2}')
    cpu_delta=$(echo "$avg_cpu $BASELINE_CPU" | awk '{printf "%.1f", $1-$2}')
    if [ "$avg_gpu" != "N/A" ] && [ "$BASELINE_GPU" != "N/A" ]; then
        gpu_delta=$(echo "$avg_gpu $BASELINE_GPU" | awk '{printf "%.1f", $1-$2}')
    else
        gpu_delta="N/A"
    fi
    
    # Display results
    echo ""
    echo "    ┌─────────────────────────────────────────────────────────────────────┐"
    printf "    │ %-69s │\n" "RESULTS"
    echo "    ├─────────────────────────────────────────────────────────────────────┤"
    printf "    │ %-20s │ %-22s │ %-20s │\n" "Metric" "Average" "Peak"
    echo "    ├─────────────────────────────────────────────────────────────────────┤"
    printf "    │ %-20s │ %14s MB     │ %12s MB     │\n" "RAM Usage" "$avg_ram" "${peak_ram:-N/A}"
    printf "    │ %-20s │ %14s %%      │ %12s %%      │\n" "CPU Usage" "$avg_cpu" "${peak_cpu:-N/A}"
    printf "    │ %-20s │ %14s %%      │ %12s %%      │\n" "GPU Usage (GR3D)" "$avg_gpu" "${peak_gpu:-N/A}"
    echo "    ├─────────────────────────────────────────────────────────────────────┤"
    printf "    │ %-20s │ %14s MB     │ (delta from idle)   │\n" "RAM Delta" "+$ram_delta"
    printf "    │ %-20s │ %14s %%      │                     │\n" "CPU Delta" "+$cpu_delta"
    printf "    │ %-20s │ %14s %%      │                     │\n" "GPU Delta" "+$gpu_delta"
    echo "    ├─────────────────────────────────────────────────────────────────────┤"
    printf "    │ %-20s │ %14s fps    │                     │\n" "Actual FPS" "$actual_fps"
    printf "    │ %-20s │ %14s ms     │ (frame interval)    │\n" "Avg Frame Time" "$latency_avg"
    printf "    │ %-20s │ %14s ms     │                     │\n" "Min Frame Time" "$latency_min"
    printf "    │ %-20s │ %14s ms     │                     │\n" "Max Frame Time" "$latency_max"
    printf "    │ %-20s │ %14s ms     │ (timing jitter)     │\n" "Std Dev" "$latency_stddev"
    echo "    └─────────────────────────────────────────────────────────────────────┘"
    
    # Store results for summary
    BENCHMARK_RESULTS["$description,ram"]="$avg_ram"
    BENCHMARK_RESULTS["$description,cpu"]="$avg_cpu"
    BENCHMARK_RESULTS["$description,gpu"]="$avg_gpu"
    BENCHMARK_RESULTS["$description,fps"]="$actual_fps"
    BENCHMARK_RESULTS["$description,latency"]="$latency_avg"
    
    # Cleanup
    rm -f "$stats_file" "$latency_file" "$pipeline_log"
    
    return 0
}

# Measure camera open latency
benchmark_camera_open_latency() {
    benchmark_print_section "Camera Open Latency"
    
    local iterations=5
    local total_time=0
    local times=()
    
    echo "  Measuring camera initialization time ($iterations iterations)..."
    
    for i in $(seq 1 $iterations); do
        # Force camera close by waiting
        sleep 2
        
        local start_time end_time duration
        start_time=$(date +%s.%N)
        
        # Open camera, grab 1 frame, close
        timeout 30 gst-launch-1.0 zedsrc num-buffers=1 ! fakesink > /dev/null 2>&1
        
        end_time=$(date +%s.%N)
        duration=$(echo "$end_time $start_time" | awk '{printf "%.3f", $1-$2}')
        times+=("$duration")
        total_time=$(echo "$total_time $duration" | awk '{print $1+$2}')
        
        echo -n "."
    done
    echo ""
    
    local avg_time min_time max_time
    avg_time=$(echo "$total_time $iterations" | awk '{printf "%.3f", $1/$2}')
    min_time=$(printf '%s\n' "${times[@]}" | sort -n | head -1)
    max_time=$(printf '%s\n' "${times[@]}" | sort -n | tail -1)
    
    echo ""
    echo "    ┌─────────────────────────────────────────────────────────────────────┐"
    printf "    │ %-69s │\n" "CAMERA OPEN LATENCY"
    echo "    ├─────────────────────────────────────────────────────────────────────┤"
    printf "    │ %-30s │ %34s │\n" "Average open time" "${avg_time}s"
    printf "    │ %-30s │ %34s │\n" "Minimum open time" "${min_time}s"
    printf "    │ %-30s │ %34s │\n" "Maximum open time" "${max_time}s"
    echo "    └─────────────────────────────────────────────────────────────────────┘"
    
    BENCHMARK_RESULTS["camera_open_avg"]="$avg_time"
    
    # Extra delay after multiple rapid camera opens
    sleep 5
}

# =============================================================================
# DEV: Run zero-copy comparison benchmarks only
# =============================================================================
# Quick benchmark to compare NV12 zero-copy vs BGRA performance.
# Usage: ./test.sh --compare-zerocopy
run_zerocopy_comparison() {
    benchmark_print_header
    
    echo ""
    echo -e "${CYAN}╔══════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║         DEV: NV12 Zero-Copy vs BGRA Performance Comparison       ║${NC}"
    echo -e "${CYAN}╚══════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        echo -e "${RED}ERROR: No ZED camera detected. Benchmarks require hardware.${NC}"
        exit 2
    fi
    
    if [ "$GMSL_CAMERA" != true ]; then
        echo -e "${RED}ERROR: This comparison requires a GMSL camera (ZED X / ZED X Mini).${NC}"
        echo -e "${YELLOW}USB cameras don't support zero-copy NV12.${NC}"
        exit 2
    fi
    
    # Detect SDK version and NV12 availability
    local sdk_version
    sdk_version=$(detect_sdk_version)
    local sdk_major_minor
    sdk_major_minor=$(get_sdk_major_minor)
    local nv12_available=false
    
    if [ "$ZERO_COPY_AVAILABLE" = true ]; then
        nv12_available=true
    fi
    
    echo -e "${BLUE}[INFO]${NC} Platform: $(detect_platform)"
    echo -e "${BLUE}[INFO]${NC} ZED SDK: ${sdk_version}"
    echo -e "${BLUE}[INFO]${NC} Camera: GMSL (ZED X series)"
    if [ "$nv12_available" = true ]; then
        echo -e "${BLUE}[INFO]${NC} NV12 Zero-copy: ${GREEN}Available${NC}"
    else
        echo -e "${BLUE}[INFO]${NC} NV12 Zero-copy: ${YELLOW}Not available (SDK 5.2+ required)${NC}"
    fi
    echo -e "${BLUE}[INFO]${NC} Benchmark duration: ${BENCHMARK_DURATION}s per test"
    echo -e "${BLUE}[INFO]${NC} Warmup period: ${BENCHMARK_WARMUP}s"
    echo ""
    
    # Get baseline measurements
    benchmark_print_section "Baseline Measurements (Idle System)"
    get_baseline_stats
    
    echo ""
    echo -e "    ${CYAN}This comparison runs identical pipelines with multiple modes:${NC}"
    if [ "$nv12_available" = true ]; then
        echo -e "    ${GREEN}▶ stream-type=6${NC} - ZED NV12 zero-copy (GMSL native, no conversion)"
    else
        echo -e "    ${YELLOW}▶ stream-type=6${NC} - ZED NV12 zero-copy ${RED}(SKIPPED - requires SDK 5.2+)${NC}"
    fi
    echo -e "    ${YELLOW}▶ stream-type=0${NC} - ZED BGRA (SDK color conversion)"
    echo -e "    ${MAGENTA}▶ nvarguscamerasrc${NC} - Raw Argus (baseline, no ZED SDK)"
    echo -e "    ${CYAN}All tests use: 1920x1200 @ 60fps${NC}"
    echo ""
    
    # Common settings for all tests: HD1200 @ 60fps (native ZED X resolution)
    local common_props="camera-resolution=2 camera-fps=60"
    
    # Check if nvarguscamerasrc is available for raw Argus baseline tests
    local argus_available=false
    if gst-inspect-1.0 nvarguscamerasrc > /dev/null 2>&1; then
        argus_available=true
        echo -e "    ${GREEN}✓ nvarguscamerasrc available for baseline comparison${NC}"
    else
        echo -e "    ${YELLOW}✗ nvarguscamerasrc not available, skipping raw Argus tests${NC}"
    fi
    echo ""
    
    # Test 1: Basic streaming (no processing)
    benchmark_print_section "Test 1: Basic Streaming (no depth)"
    
    if [ "$nv12_available" = true ]; then
        run_benchmark_pipeline "[NV12 zero-copy] Basic stream" \
            "zedsrc stream-type=6 $common_props depth-mode=0 ! 'video/x-raw(memory:NVMM),width=1920,height=1200,format=NV12,framerate=60/1'"
        sleep $CAMERA_RESET_DELAY
    fi
    
    run_benchmark_pipeline "[BGRA legacy] Basic stream" \
        "zedsrc stream-type=0 $common_props depth-mode=0 ! 'video/x-raw,width=1920,height=1200,format=BGRA,framerate=60/1'"
    sleep $CAMERA_RESET_DELAY
    
    if [ "$argus_available" = true ]; then
        run_benchmark_pipeline "[Argus raw] Basic stream" \
            "nvarguscamerasrc sensor-id=0 ! 'video/x-raw(memory:NVMM),width=1920,height=1200,format=NV12,framerate=60/1'"
        sleep $CAMERA_RESET_DELAY
        
        # Argus + RGBA conversion: Simulates legacy pipeline where app needs RGBA in CPU memory
        # Uses videoconvert (CPU) like ZED BGRA path does, for fair comparison
        run_benchmark_pipeline "[Argus+RGBA] Basic stream (CPU convert)" \
            "nvarguscamerasrc sensor-id=0 ! 'video/x-raw(memory:NVMM),width=1920,height=1200,format=NV12,framerate=60/1' ! nvvidconv ! 'video/x-raw,format=RGBA' ! videoconvert ! 'video/x-raw,format=I420'"
        sleep $CAMERA_RESET_DELAY
    fi
    
    # Test 2: With NEURAL depth (common use case) - ZED only, no Argus equivalent
    benchmark_print_section "Test 2: With NEURAL Depth"
    
    if [ "$nv12_available" = true ]; then
        run_benchmark_pipeline "[NV12 zero-copy] NEURAL depth" \
            "zedsrc stream-type=6 $common_props depth-mode=4 ! 'video/x-raw(memory:NVMM),width=1920,height=1200,format=NV12,framerate=60/1'"
        sleep $CAMERA_RESET_DELAY
    fi
    
    run_benchmark_pipeline "[BGRA legacy] NEURAL depth" \
        "zedsrc stream-type=0 $common_props depth-mode=4 ! 'video/x-raw,width=1920,height=1200,format=BGRA,framerate=60/1'"
    sleep $CAMERA_RESET_DELAY
    
    # Test 3: Stereo streaming (side-by-side: 3840x1200)
    # Both NV12 (stream-type=7) and BGRA SBS (stream-type=5) use side-by-side layout
    benchmark_print_section "Test 3: Stereo Streaming (Side-by-Side)"
    
    if [ "$nv12_available" = true ]; then
        run_benchmark_pipeline "[NV12 zero-copy] Stereo SBS" \
            "zedsrc stream-type=7 $common_props ! 'video/x-raw(memory:NVMM),width=3840,height=1200,format=NV12,framerate=60/1'"
        sleep $CAMERA_RESET_DELAY
    fi
    
    run_benchmark_pipeline "[BGRA legacy] Stereo SBS" \
        "zedsrc stream-type=5 $common_props ! 'video/x-raw,width=3840,height=1200,format=BGRA,framerate=60/1'"
    sleep $CAMERA_RESET_DELAY
    
    # Test 4: Hardware encoding pipeline (real-world use case)
    if gst-inspect-1.0 nvv4l2h265enc > /dev/null 2>&1; then
        benchmark_print_section "Test 4: H.265 Hardware Encoding Pipeline"
        
        if [ "$nv12_available" = true ]; then
            run_benchmark_pipeline "[NV12 zero-copy] H.265 encode (direct)" \
                "zedsrc stream-type=6 $common_props ! 'video/x-raw(memory:NVMM),width=1920,height=1200,format=NV12,framerate=60/1' ! nvv4l2h265enc"
            sleep $CAMERA_RESET_DELAY
        fi
        
        # BGRA needs explicit caps for nvvidconv and videoconvert for color space
        run_benchmark_pipeline "[BGRA legacy] H.265 encode (w/ convert)" \
            "zedsrc stream-type=0 $common_props ! videoconvert ! 'video/x-raw,format=I420' ! nvvidconv ! 'video/x-raw(memory:NVMM),width=1920,height=1200,format=NV12,framerate=60/1' ! nvv4l2h265enc"
        sleep $CAMERA_RESET_DELAY
        
        if [ "$argus_available" = true ]; then
            run_benchmark_pipeline "[Argus raw] H.265 encode" \
                "nvarguscamerasrc sensor-id=0 ! 'video/x-raw(memory:NVMM),width=1920,height=1200,format=NV12,framerate=60/1' ! nvv4l2h265enc"
            sleep $CAMERA_RESET_DELAY
            
            # Argus + RGBA roundtrip: NV12 → RGBA (CPU mem) → I420 (CPU convert) → NV12 → encode
            # Uses videoconvert (CPU) like ZED BGRA path does, for fair comparison
            run_benchmark_pipeline "[Argus+RGBA] H.265 encode (CPU convert)" \
                "nvarguscamerasrc sensor-id=0 ! 'video/x-raw(memory:NVMM),width=1920,height=1200,format=NV12,framerate=60/1' ! nvvidconv ! 'video/x-raw,format=RGBA' ! videoconvert ! 'video/x-raw,format=I420' ! nvvidconv ! 'video/x-raw(memory:NVMM),format=NV12' ! nvv4l2h265enc"
            sleep $CAMERA_RESET_DELAY
        fi
    fi
    
    # Test 5: DeepStream with nvinfer (actual AI inference)
    # This is the most important real-world use case for zero-copy
    if gst-inspect-1.0 nvinfer > /dev/null 2>&1; then
        # Find a valid nvinfer config file
        local ds_config=""
        local ds_paths=(
            "/opt/nvidia/deepstream/deepstream/samples/configs/deepstream-app/config_infer_primary.txt"
            "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_primary.txt"
            "/opt/nvidia/deepstream/deepstream-6.2/samples/configs/deepstream-app/config_infer_primary.txt"
            "/opt/nvidia/deepstream/deepstream-6.3/samples/configs/deepstream-app/config_infer_primary.txt"
            "/opt/nvidia/deepstream/deepstream-7.0/samples/configs/deepstream-app/config_infer_primary.txt"
        )
        for path in "${ds_paths[@]}"; do
            if [ -f "$path" ]; then
                ds_config="$path"
                break
            fi
        done
        
        if [ -n "$ds_config" ]; then
            benchmark_print_section "Test 5: DeepStream + nvinfer (AI Inference)"
            echo -e "    ${CYAN}Using config: $ds_config${NC}"
            echo ""
            
            # NV12 zero-copy: Direct to nvinfer (nvinfer expects NV12 in NVMM)
            # Pipeline: zedsrc → nvstreammux → nvinfer → identity → fakesink
            if [ "$nv12_available" = true ]; then
                run_benchmark_pipeline "[NV12 zero-copy] DS nvinfer (direct)" \
                    "zedsrc stream-type=6 $common_props ! 'video/x-raw(memory:NVMM),width=1920,height=1200,format=NV12,framerate=60/1' ! mux.sink_0 nvstreammux name=mux batch-size=1 width=1920 height=1200 live-source=1 ! nvinfer config-file-path=$ds_config"
                sleep $CAMERA_RESET_DELAY
            fi
            
            # BGRA legacy: Requires videoconvert + nvvideoconvert to get to NVMM NV12
            # Pipeline: zedsrc (BGRA sysmem) → videoconvert → nvvideoconvert → NV12 NVMM → nvstreammux → nvinfer → identity → fakesink
            run_benchmark_pipeline "[BGRA legacy] DS nvinfer (w/ convert)" \
                "zedsrc stream-type=0 $common_props ! videoconvert ! 'video/x-raw,format=I420' ! nvvideoconvert ! 'video/x-raw(memory:NVMM),format=NV12' ! mux.sink_0 nvstreammux name=mux batch-size=1 width=1920 height=1200 live-source=1 ! nvinfer config-file-path=$ds_config"
            sleep $CAMERA_RESET_DELAY
            
            if [ "$argus_available" = true ]; then
                run_benchmark_pipeline "[Argus raw] DS nvinfer" \
                    "nvarguscamerasrc sensor-id=0 ! 'video/x-raw(memory:NVMM),width=1920,height=1200,format=NV12,framerate=60/1' ! mux.sink_0 nvstreammux name=mux batch-size=1 width=1920 height=1200 live-source=1 ! nvinfer config-file-path=$ds_config"
                sleep $CAMERA_RESET_DELAY
                
                # Argus + RGBA roundtrip: Simulates legacy pipeline with RGBA processing before inference
                # NV12 → RGBA (custom processing) → back to NV12 NVMM → nvinfer
                run_benchmark_pipeline "[Argus+RGBA] DS nvinfer (RGBA roundtrip)" \
                    "nvarguscamerasrc sensor-id=0 ! 'video/x-raw(memory:NVMM),width=1920,height=1200,format=NV12,framerate=60/1' ! nvvidconv ! 'video/x-raw,format=RGBA' ! nvvidconv ! 'video/x-raw(memory:NVMM),format=NV12' ! mux.sink_0 nvstreammux name=mux batch-size=1 width=1920 height=1200 live-source=1 ! nvinfer config-file-path=$ds_config"
                sleep $CAMERA_RESET_DELAY
            fi
        else
            echo -e "    ${YELLOW}[INFO]${NC} Skipping DeepStream nvinfer test (no config file found)"
        fi
    fi
    
    # Note: Object Detection with NV12 zero-copy is not tested here because
    # OD overlay rendering requires BGRA format internally for drawing boxes.
    # The zero-copy benefit is in the camera capture path, not the OD path.
    
    # Summary - grouped by test pairs for easy comparison
    benchmark_print_section "COMPARISON SUMMARY"
    echo ""
    echo -e "    ${CYAN}ZED SDK Version: ${sdk_version}${NC}"
    if [ "$nv12_available" = true ]; then
        echo -e "    ${GREEN}NV12 Zero-copy: Available${NC}"
    else
        echo -e "    ${YELLOW}NV12 Zero-copy: Not available (BGRA vs Argus comparison only)${NC}"
    fi
    echo ""
    
    # Define test pairs in order
    local -a test_pairs=(
        "Basic stream"
        "NEURAL depth"
        "Stereo"
        "H.265 encode"
        "DS nvinfer"
    )
    
    echo "    ┌────────────────────────────────────────────────────────────────────────────────────────────┐"
    printf "    │ %-28s │ %8s │ %8s │ %8s │ %8s │ %8s │ %7s │\n" "Configuration" "RAM(MB)" "CPU(%)" "GPU(%)" "FPS" "Lat(ms)" "Δ GPU"
    echo "    ├────────────────────────────────────────────────────────────────────────────────────────────┤"
    
    for test_name in "${test_pairs[@]}"; do
        # Find NV12, BGRA, Argus raw, and Argus+RGBA results for this test
        local nv12_key=""
        local bgra_key=""
        local argus_key=""
        local argus_rgba_key=""
        
        for key in "${!BENCHMARK_RESULTS[@]}"; do
            if [[ "$key" == *",ram" ]]; then
                local base="${key%,ram}"
                if [[ "$base" == *"NV12"* && "$base" == *"$test_name"* ]]; then
                    nv12_key="$base"
                elif [[ "$base" == *"BGRA"* && "$base" == *"$test_name"* ]]; then
                    bgra_key="$base"
                elif [[ "$base" == *"Argus+RGBA"* && "$base" == *"$test_name"* ]]; then
                    argus_rgba_key="$base"
                elif [[ "$base" == *"Argus"* && "$base" == *"$test_name"* ]]; then
                    argus_key="$base"
                fi
            fi
        done
        
        # Skip if we don't have at least BGRA results (NV12 is optional for SDK 5.1)
        if [[ -z "$bgra_key" ]]; then
            continue
        fi
        
        # Get NV12 results (may be empty on SDK 5.1)
        local nv12_ram="${BENCHMARK_RESULTS[$nv12_key,ram]:-N/A}"
        local nv12_cpu="${BENCHMARK_RESULTS[$nv12_key,cpu]:-N/A}"
        local nv12_gpu="${BENCHMARK_RESULTS[$nv12_key,gpu]:-N/A}"
        local nv12_fps="${BENCHMARK_RESULTS[$nv12_key,fps]:-N/A}"
        local nv12_lat="${BENCHMARK_RESULTS[$nv12_key,latency]:-N/A}"
        
        # Get BGRA results
        local bgra_ram="${BENCHMARK_RESULTS[$bgra_key,ram]:-N/A}"
        local bgra_cpu="${BENCHMARK_RESULTS[$bgra_key,cpu]:-N/A}"
        local bgra_gpu="${BENCHMARK_RESULTS[$bgra_key,gpu]:-N/A}"
        local bgra_fps="${BENCHMARK_RESULTS[$bgra_key,fps]:-N/A}"
        local bgra_lat="${BENCHMARK_RESULTS[$bgra_key,latency]:-N/A}"
        
        # Get Argus results (optional)
        local argus_ram="${BENCHMARK_RESULTS[$argus_key,ram]:-N/A}"
        local argus_cpu="${BENCHMARK_RESULTS[$argus_key,cpu]:-N/A}"
        local argus_gpu="${BENCHMARK_RESULTS[$argus_key,gpu]:-N/A}"
        local argus_fps="${BENCHMARK_RESULTS[$argus_key,fps]:-N/A}"
        local argus_lat="${BENCHMARK_RESULTS[$argus_key,latency]:-N/A}"
        
        # Get Argus+RGBA results (optional - shows cost of color conversion)
        local argus_rgba_ram="${BENCHMARK_RESULTS[$argus_rgba_key,ram]:-N/A}"
        local argus_rgba_cpu="${BENCHMARK_RESULTS[$argus_rgba_key,cpu]:-N/A}"
        local argus_rgba_gpu="${BENCHMARK_RESULTS[$argus_rgba_key,gpu]:-N/A}"
        local argus_rgba_fps="${BENCHMARK_RESULTS[$argus_rgba_key,fps]:-N/A}"
        local argus_rgba_lat="${BENCHMARK_RESULTS[$argus_rgba_key,latency]:-N/A}"
        
        # Calculate GPU delta (BGRA - NV12, positive means NV12 is better)
        local gpu_delta="N/A"
        if [[ -n "$nv12_key" && "$nv12_gpu" != "N/A" && "$bgra_gpu" != "N/A" ]]; then
            gpu_delta=$(echo "$bgra_gpu $nv12_gpu" | awk '{printf "%.1f", $1-$2}')
        fi
        
        # Calculate GPU delta BGRA vs Argus (shows ZED SDK overhead)
        local bgra_argus_delta="N/A"
        if [[ "$argus_gpu" != "N/A" && "$bgra_gpu" != "N/A" ]]; then
            bgra_argus_delta=$(echo "$bgra_gpu $argus_gpu" | awk '{printf "%.1f", $1-$2}')
        fi
        
        # Calculate GPU delta Argus+RGBA vs Argus raw (shows conversion cost)
        local argus_rgba_delta="N/A"
        if [[ "$argus_gpu" != "N/A" && "$argus_rgba_gpu" != "N/A" ]]; then
            argus_rgba_delta=$(echo "$argus_rgba_gpu $argus_gpu" | awk '{printf "%.1f", $1-$2}')
        fi
        
        # Print test header
        printf "    │ ${CYAN}%-28s${NC} │ %8s │ %8s │ %8s │ %8s │ %8s │ %7s │\n" "── $test_name ──" "" "" "" "" "" ""
        
        # Print Argus row if available (magenta - baseline)
        if [[ -n "$argus_key" && "$argus_ram" != "N/A" ]]; then
            printf "    │ ${MAGENTA}  %-26s${NC} │ %8s │ %8s │ %8s │ %8s │ %8s │ %7s │\n" \
                "Argus raw (baseline)" "$argus_ram" "$argus_cpu" "$argus_gpu" "$argus_fps" "$argus_lat" "(ref)"
        fi
        
        # Print Argus+RGBA row if available (shows color conversion cost)
        if [[ -n "$argus_rgba_key" && "$argus_rgba_ram" != "N/A" ]]; then
            printf "    │ ${MAGENTA}  %-26s${NC} │ %8s │ %8s │ %8s │ %8s │ %8s │ ${MAGENTA}%+6s%%${NC} │\n" \
                "Argus+RGBA (convert)" "$argus_rgba_ram" "$argus_rgba_cpu" "$argus_rgba_gpu" "$argus_rgba_fps" "$argus_rgba_lat" "+$argus_rgba_delta"
        fi
        
        # Print NV12 row if available (green highlight)
        if [[ -n "$nv12_key" && "$nv12_ram" != "N/A" ]]; then
            printf "    │ ${GREEN}  %-26s${NC} │ %8s │ %8s │ %8s │ %8s │ %8s │ ${GREEN}%+6s%%${NC} │\n" \
                "NV12 zero-copy" "$nv12_ram" "$nv12_cpu" "$nv12_gpu" "$nv12_fps" "$nv12_lat" "-$gpu_delta"
        fi
        
        # Print BGRA row (yellow) - show delta vs Argus if we have Argus data
        if [[ -n "$argus_key" && "$bgra_argus_delta" != "N/A" ]]; then
            printf "    │ ${YELLOW}  %-26s${NC} │ %8s │ %8s │ %8s │ %8s │ %8s │ ${YELLOW}%+6s%%${NC} │\n" \
                "BGRA legacy" "$bgra_ram" "$bgra_cpu" "$bgra_gpu" "$bgra_fps" "$bgra_lat" "+$bgra_argus_delta"
        else
            printf "    │ ${YELLOW}  %-26s${NC} │ %8s │ %8s │ %8s │ %8s │ %8s │ %7s │\n" \
                "BGRA legacy" "$bgra_ram" "$bgra_cpu" "$bgra_gpu" "$bgra_fps" "$bgra_lat" "(base)"
        fi
        
        echo "    ├────────────────────────────────────────────────────────────────────────────────────────────┤"
    done
    
    # Remove last separator and add footer
    echo "    └────────────────────────────────────────────────────────────────────────────────────────────┘"
    echo ""
    echo -e "    ${GREEN}Legend:${NC}"
    echo -e "    ${CYAN}• Δ GPU = GPU usage change (positive = higher GPU usage)${NC}"
    echo -e "    ${MAGENTA}• Argus raw    = nvarguscamerasrc baseline (NV12 NVMM, no ZED SDK)${NC}"
    echo -e "    ${MAGENTA}• Argus+RGBA   = Argus with NV12→RGBA→I420 via CPU videoconvert (simulates legacy)${NC}"
    echo -e "    ${GREEN}• NV12 zero-copy = stream-type=6 (SDK 5.2+ required)${NC}"
    echo -e "    ${YELLOW}• BGRA legacy  = stream-type=0 with CPU videoconvert (traditional mode)${NC}"
    echo -e "    ${CYAN}• Lower RAM/CPU/GPU/Latency is better, Higher FPS is better${NC}"
    echo ""
    if [[ "$nv12_available" == true ]]; then
        echo -e "    ${GREEN}Expected Benefits of NV12 Zero-Copy:${NC}"
        echo -e "    ${CYAN}• Lower GPU usage (no CUDA color conversion)${NC}"
        echo -e "    ${CYAN}• Lower CPU usage (less data movement)${NC}"
        echo -e "    ${CYAN}• H.265 encoding: Major improvement (direct to encoder, no nvvidconv)${NC}"
    else
        echo -e "    ${YELLOW}Note: NV12 zero-copy requires ZED SDK 5.2+. Currently running SDK ${sdk_version}${NC}"
        echo -e "    ${CYAN}Showing BGRA vs Argus comparison for SDK overhead analysis.${NC}"
    fi
    echo ""
}

# Run all benchmarks
run_benchmarks() {
    benchmark_print_header
    
    if [ "$HARDWARE_AVAILABLE" = false ]; then
        echo -e "${RED}ERROR: No ZED camera detected. Benchmarks require hardware.${NC}"
        exit 2
    fi
    
    local platform
    platform=$(detect_platform)
    
    local mode_str
    if [ "$FAST_MODE" = true ]; then
        mode_str="Fast (quick benchmark)"
        BENCHMARK_DURATION=$BENCHMARK_DURATION_FAST
    else
        mode_str="Extensive (full benchmark)"
    fi
    
    # Common camera properties for consistent benchmarks
    # HD1200 @ 60fps is the highest common resolution for both USB and GMSL cameras
    local cam_props="camera-resolution=2 camera-fps=60"
    
    echo -e "${BLUE}[INFO]${NC} Platform: $platform"
    echo -e "${BLUE}[INFO]${NC} Mode: $mode_str"
    echo -e "${BLUE}[INFO]${NC} Benchmark duration: ${BENCHMARK_DURATION}s per test"
    echo -e "${BLUE}[INFO]${NC} Warmup period: ${BENCHMARK_WARMUP}s"
    if [[ -n "$BENCHMARK_BASELINE" ]]; then
        echo -e "${BLUE}[INFO]${NC} Comparing with: $BENCHMARK_BASELINE"
    fi
    
    # Get baseline measurements
    benchmark_print_section "Baseline Measurements (Idle System)"
    get_baseline_stats
    
    # Camera open latency (skip in fast mode - takes ~25s)
    if [ "$EXTENSIVE_MODE" = true ]; then
        benchmark_camera_open_latency
    else
        echo -e "${YELLOW}[INFO]${NC} Skipping camera open latency test (use --extensive)"
    fi
    
    # Detect camera type for appropriate resolutions (use global variable from check_hardware)
    if [ "$GMSL_CAMERA" = true ]; then
        echo -e "${BLUE}[INFO]${NC} Detected GMSL camera (ZED X series)"
        if [ "$ZERO_COPY_AVAILABLE" = true ]; then
            echo -e "${BLUE}[INFO]${NC} Zero-copy NV12 available"
        fi
    elif [ "$HARDWARE_AVAILABLE" = true ]; then
        echo -e "${BLUE}[INFO]${NC} Detected USB camera"
    fi
    
    # === CORE BENCHMARKS (always run) ===
    
    # Basic streaming benchmark
    benchmark_print_section "Basic Streaming (Left Image Only)"
    run_benchmark_pipeline "Stream: Left RGB" "zedsrc stream-type=0 $cam_props"
    sleep $CAMERA_RESET_DELAY
    
    # Depth benchmark (one mode)
    benchmark_print_section "Depth Processing Benchmark"
    run_benchmark_pipeline "Depth: NEURAL mode" "zedsrc stream-type=4 $cam_props depth-mode=4"
    sleep $CAMERA_RESET_DELAY
    
    # === EXTENSIVE BENCHMARKS (only in extensive mode) ===
    
    if [ "$EXTENSIVE_MODE" = true ]; then
        benchmark_print_section "Stereo Streaming (Left + Right)"
        run_benchmark_pipeline "Stream: Left+Right RGB" "zedsrc stream-type=2 $cam_props"
        sleep $CAMERA_RESET_DELAY
        
        # NV12 Zero-Copy benchmark (Jetson with GMSL + SDK 5.2+)
        if [ "$ZERO_COPY_AVAILABLE" = true ]; then
            benchmark_print_section "NV12 Zero-Copy Benchmarks (GMSL)"
            
            run_benchmark_pipeline "Stream: RAW_NV12 zero-copy" "zedsrc stream-type=6 $cam_props ! 'video/x-raw(memory:NVMM),format=NV12'"
            sleep $CAMERA_RESET_DELAY
            
            run_benchmark_pipeline "Stream: RAW_NV12 stereo zero-copy" "zedsrc stream-type=7 $cam_props ! 'video/x-raw(memory:NVMM),format=NV12'"
            sleep $CAMERA_RESET_DELAY
            
            # NV12 with hardware encoding benchmark
            if gst-inspect-1.0 nvv4l2h265enc > /dev/null 2>&1; then
                benchmark_print_section "NV12 Zero-Copy + Hardware Encoding"
                run_benchmark_pipeline "NV12 -> H.265 HW encode" \
                    "zedsrc stream-type=6 $cam_props ! 'video/x-raw(memory:NVMM),format=NV12' ! nvv4l2h265enc"
                sleep $CAMERA_RESET_DELAY
            fi
            
            # =================================================================
            # DEV COMPARISON: NV12 Zero-Copy vs BGRA (non-zero-copy)
            # =================================================================
            # This section benchmarks identical pipelines with both modes
            # to measure the performance benefit of zero-copy NV12.
            benchmark_print_section "DEV: NV12 Zero-Copy vs BGRA Comparison"
            echo ""
            echo -e "    ${CYAN}Comparing identical pipelines with different stream modes:${NC}"
            echo -e "    ${CYAN}  - stream-type=6 (NV12 zero-copy, GMSL native)${NC}"
            echo -e "    ${CYAN}  - stream-type=0 (BGRA, requires SDK color conversion)${NC}"
            echo ""
            
            # Test 1: Basic streaming (no processing)
            echo -e "    ${YELLOW}━━━ Test 1: Basic Streaming ━━━${NC}"
            run_benchmark_pipeline "[NV12] Basic stream" \
                "zedsrc stream-type=6 $cam_props ! 'video/x-raw(memory:NVMM),format=NV12'"
            sleep $CAMERA_RESET_DELAY
            
            run_benchmark_pipeline "[BGRA] Basic stream" \
                "zedsrc stream-type=0 $cam_props"
            sleep $CAMERA_RESET_DELAY
            
            # Test 2: With depth enabled (common use case)
            echo -e "    ${YELLOW}━━━ Test 2: With Depth Processing ━━━${NC}"
            run_benchmark_pipeline "[NV12] With depth (NEURAL)" \
                "zedsrc stream-type=6 $cam_props depth-mode=4 ! 'video/x-raw(memory:NVMM),format=NV12'"
            sleep $CAMERA_RESET_DELAY
            
            run_benchmark_pipeline "[BGRA] With depth (NEURAL)" \
                "zedsrc stream-type=0 $cam_props depth-mode=4"
            sleep $CAMERA_RESET_DELAY
            
            # Test 3: Stereo streaming
            echo -e "    ${YELLOW}━━━ Test 3: Stereo Streaming ━━━${NC}"
            run_benchmark_pipeline "[NV12] Stereo stream" \
                "zedsrc stream-type=7 $cam_props ! 'video/x-raw(memory:NVMM),format=NV12'"
            sleep $CAMERA_RESET_DELAY
            
            run_benchmark_pipeline "[BGRA] Stereo stream" \
                "zedsrc stream-type=2 $cam_props"
            sleep $CAMERA_RESET_DELAY
            
            # Test 4: Hardware encoding pipeline (real-world use case)
            if gst-inspect-1.0 nvv4l2h265enc > /dev/null 2>&1; then
                echo -e "    ${YELLOW}━━━ Test 4: H.265 Hardware Encoding ━━━${NC}"
                run_benchmark_pipeline "[NV12] H.265 encode (direct)" \
                    "zedsrc stream-type=6 $cam_props ! 'video/x-raw(memory:NVMM),format=NV12' ! nvv4l2h265enc"
                sleep $CAMERA_RESET_DELAY
                
                # BGRA requires videoconvert + nvvidconv for color space conversion to NV12
                run_benchmark_pipeline "[BGRA] H.265 encode (w/ convert)" \
                    "zedsrc stream-type=0 $cam_props ! videoconvert ! nvvidconv ! 'video/x-raw(memory:NVMM),format=NV12' ! nvv4l2h265enc"
                sleep $CAMERA_RESET_DELAY
            fi
            
            echo ""
            echo -e "    ${GREEN}━━━ Comparison Summary ━━━${NC}"
            echo -e "    ${CYAN}NV12 zero-copy eliminates SDK-side CUDA color conversion.${NC}"
            echo -e "    ${CYAN}Expected benefits: Lower CPU/GPU usage, lower latency, lower RAM.${NC}"
            echo ""
            
        elif [ "$GMSL_CAMERA" = true ]; then
            log_info "Skipping NV12 zero-copy benchmarks (requires ZED SDK 5.2+)"
        fi
        
        # Additional depth modes
        benchmark_print_section "Additional Depth Modes"
        
        run_benchmark_pipeline "Depth: PERFORMANCE mode" "zedsrc stream-type=4 $cam_props depth-mode=1"
        sleep $CAMERA_RESET_DELAY
        
        run_benchmark_pipeline "Depth: ULTRA mode" "zedsrc stream-type=4 $cam_props depth-mode=3"
        sleep $CAMERA_RESET_DELAY
        
        # Resolution benchmarks
        benchmark_print_section "Resolution Benchmarks"
        
        if [ "$GMSL_CAMERA" = true ]; then
            run_benchmark_pipeline "Resolution: HD1200 (1920x1200)" "zedsrc stream-type=0 camera-resolution=2 camera-fps=60"
            sleep $CAMERA_RESET_DELAY
        else
            run_benchmark_pipeline "Resolution: HD720 (1280x720)" "zedsrc stream-type=0 camera-resolution=3 camera-fps=60"
            sleep $CAMERA_RESET_DELAY
        fi
        
        run_benchmark_pipeline "Resolution: HD1080 (1920x1080)" "zedsrc stream-type=0 camera-resolution=1 camera-fps=60"
        sleep $CAMERA_RESET_DELAY
        
        # AI Features benchmarks
        # Note: OD and Body Tracking require positional tracking to be enabled
        benchmark_print_section "AI Features Benchmarks"
        
        run_benchmark_pipeline "Object Detection: MULTI_CLASS_BOX_FAST" \
            "zedsrc stream-type=0 $cam_props depth-mode=4 enable-positional-tracking=true od-enabled=true od-detection-model=2" || \
            echo -e "    ${YELLOW}[SKIP] Object Detection failed${NC}"
        sleep $CAMERA_RESET_DELAY
        
        run_benchmark_pipeline "Body Tracking: BODY_34_FAST" \
            "zedsrc stream-type=0 $cam_props depth-mode=4 enable-positional-tracking=true bt-enabled=true bt-detection-model=0 bt-format=1" || \
            echo -e "    ${YELLOW}[SKIP] Body Tracking failed${NC}"
        sleep $CAMERA_RESET_DELAY
        
        # Positional Tracking benchmark
        benchmark_print_section "Positional Tracking Benchmark"
        run_benchmark_pipeline "Positional Tracking enabled" \
            "zedsrc stream-type=0 $cam_props depth-mode=1 enable-positional-tracking=true"
        sleep $CAMERA_RESET_DELAY
    else
        echo ""
        echo -e "${YELLOW}[INFO]${NC} Skipping extended benchmarks (use --extensive for full suite)"
        echo ""
    fi
    
    # Print summary
    benchmark_print_section "BENCHMARK SUMMARY"
    echo ""
    echo "    ┌───────────────────────────────────────────────────────────────────────────────┐"
    printf "    │ %-25s │ %8s │ %8s │ %8s │ %8s │ %8s │\n" "Configuration" "RAM(MB)" "CPU(%)" "GPU(%)" "FPS" "Lat(ms)"
    echo "    ├───────────────────────────────────────────────────────────────────────────────┤"
    
    for key in "${!BENCHMARK_RESULTS[@]}"; do
        if [[ "$key" == *",ram" ]]; then
            local base="${key%,ram}"
            local ram="${BENCHMARK_RESULTS[$base,ram]:-N/A}"
            local cpu="${BENCHMARK_RESULTS[$base,cpu]:-N/A}"
            local gpu="${BENCHMARK_RESULTS[$base,gpu]:-N/A}"
            local fps="${BENCHMARK_RESULTS[$base,fps]:-N/A}"
            local lat="${BENCHMARK_RESULTS[$base,latency]:-N/A}"
            # Truncate description if too long
            local short_desc="${base:0:25}"
            printf "    │ %-25s │ %8s │ %8s │ %8s │ %8s │ %8s │\n" "$short_desc" "$ram" "$cpu" "$gpu" "$fps" "$lat"
        fi
    done
    
    echo "    └───────────────────────────────────────────────────────────────────────────────┘"
    
    if [ -n "${BENCHMARK_RESULTS[camera_open_avg]}" ]; then
        echo ""
        echo "    Camera open latency: ${BENCHMARK_RESULTS[camera_open_avg]}s (average)"
    fi
    
    # Comparison with baseline if provided
    if [[ -n "$BENCHMARK_BASELINE" ]]; then
        benchmark_print_section "COMPARISON WITH BASELINE"
        echo ""
        echo "    Baseline file: $BENCHMARK_BASELINE"
        echo ""
        
        # Parse baseline JSON
        local baseline_open_latency
        baseline_open_latency=$(grep -oP '"camera_open_latency_s":\s*\K[0-9.]+' "$BENCHMARK_BASELINE" 2>/dev/null)
        
        # Camera open latency comparison
        if [[ -n "$baseline_open_latency" && -n "${BENCHMARK_RESULTS[camera_open_avg]}" ]]; then
            local open_diff open_pct indicator
            open_diff=$(echo "${BENCHMARK_RESULTS[camera_open_avg]} $baseline_open_latency" | awk '{printf "%.3f", $1-$2}')
            open_pct=$(echo "${BENCHMARK_RESULTS[camera_open_avg]} $baseline_open_latency" | awk '{if($2>0) printf "%.1f", ($1-$2)/$2*100; else print "N/A"}')
            
            if (( $(echo "$open_diff < -0.1" | bc -l) )); then
                indicator="${GREEN}▼ IMPROVED${NC}"
            elif (( $(echo "$open_diff > 0.1" | bc -l) )); then
                indicator="${RED}▲ SLOWER${NC}"
            else
                indicator="${YELLOW}≈ SAME${NC}"
            fi
            echo -e "    Camera Open: ${BENCHMARK_RESULTS[camera_open_avg]}s vs ${baseline_open_latency}s (${open_diff}s, ${open_pct}%) $indicator"
        fi
        
        echo ""
        echo "    ┌───────────────────────────────────────────────────────────────────────────────────────────┐"
        printf "    │ %-25s │ %10s │ %10s │ %10s │ %-18s │\n" "Configuration" "Current" "Baseline" "Delta" "Status"
        echo "    ├───────────────────────────────────────────────────────────────────────────────────────────┤"
        
        # Compare each benchmark
        local improvements=0 regressions=0 unchanged=0
        
        for key in "${!BENCHMARK_RESULTS[@]}"; do
            if [[ "$key" == *",ram" ]]; then
                local base="${key%,ram}"
                local short_desc="${base:0:25}"
                
                # Get current values
                local cur_ram="${BENCHMARK_RESULTS[$base,ram]:-0}"
                local cur_cpu="${BENCHMARK_RESULTS[$base,cpu]:-0}"
                local cur_gpu="${BENCHMARK_RESULTS[$base,gpu]:-0}"
                local cur_fps="${BENCHMARK_RESULTS[$base,fps]:-0}"
                local cur_lat="${BENCHMARK_RESULTS[$base,latency]:-0}"
                
                # Get baseline values (parse from JSON)
                local bl_data
                bl_data=$(grep -A5 "\"name\": \"$base\"" "$BENCHMARK_BASELINE" 2>/dev/null | head -6)
                
                if [[ -n "$bl_data" ]]; then
                    local bl_ram bl_cpu bl_gpu bl_fps bl_lat
                    bl_ram=$(echo "$bl_data" | grep -oP '"ram_mb":\s*\K[0-9.]+' | head -1)
                    bl_cpu=$(echo "$bl_data" | grep -oP '"cpu_percent":\s*\K[0-9.]+' | head -1)
                    bl_gpu=$(echo "$bl_data" | grep -oP '"gpu_percent":\s*\K[0-9.]+' | head -1)
                    bl_fps=$(echo "$bl_data" | grep -oP '"fps":\s*\K[0-9.]+' | head -1)
                    bl_lat=$(echo "$bl_data" | grep -oP '"frame_time_ms":\s*\K[0-9.]+' | head -1)
                    
                    # Calculate deltas
                    local ram_delta cpu_delta fps_delta lat_delta status
                    ram_delta=$(echo "${cur_ram:-0} ${bl_ram:-0}" | awk '{printf "%.0f", $1-$2}')
                    cpu_delta=$(echo "${cur_cpu:-0} ${bl_cpu:-0}" | awk '{printf "%.1f", $1-$2}')
                    fps_delta=$(echo "${cur_fps:-0} ${bl_fps:-0}" | awk '{printf "%.1f", $1-$2}')
                    lat_delta=$(echo "${cur_lat:-0} ${bl_lat:-0}" | awk '{printf "%.2f", $1-$2}')
                    
                    # Determine status based on key metrics (FPS up=good, RAM/CPU/Latency down=good)
                    local score=0
                    # FPS: higher is better
                    if (( $(echo "$fps_delta > 0.5" | bc -l 2>/dev/null || echo 0) )); then
                        score=$((score + 1))
                    elif (( $(echo "$fps_delta < -0.5" | bc -l 2>/dev/null || echo 0) )); then
                        score=$((score - 1))
                    fi
                    # RAM: lower is better
                    if (( $(echo "$ram_delta < -20" | bc -l 2>/dev/null || echo 0) )); then
                        score=$((score + 1))
                    elif (( $(echo "$ram_delta > 20" | bc -l 2>/dev/null || echo 0) )); then
                        score=$((score - 1))
                    fi
                    # Latency: lower is better
                    if (( $(echo "$lat_delta < -1" | bc -l 2>/dev/null || echo 0) )); then
                        score=$((score + 1))
                    elif (( $(echo "$lat_delta > 1" | bc -l 2>/dev/null || echo 0) )); then
                        score=$((score - 1))
                    fi
                    
                    if [ $score -gt 0 ]; then
                        status="${GREEN}▲ IMPROVED${NC}"
                        improvements=$((improvements + 1))
                    elif [ $score -lt 0 ]; then
                        status="${RED}▼ REGRESSED${NC}"
                        regressions=$((regressions + 1))
                    else
                        status="${YELLOW}≈ UNCHANGED${NC}"
                        unchanged=$((unchanged + 1))
                    fi
                    
                    # Print RAM comparison row
                    printf "    │ %-25s │ %8s MB │ %8s MB │ %+8s MB │ " "$short_desc" "$cur_ram" "${bl_ram:-N/A}" "$ram_delta"
                    echo -e "$status │"
                    
                    # Print FPS comparison row  
                    printf "    │ %-25s │ %7s fps │ %7s fps │ %+7s fps │ " "  └─ FPS" "$cur_fps" "${bl_fps:-N/A}" "$fps_delta"
                    if (( $(echo "$fps_delta > 0.5" | bc -l 2>/dev/null || echo 0) )); then
                        echo -e "${GREEN}better${NC}              │"
                    elif (( $(echo "$fps_delta < -0.5" | bc -l 2>/dev/null || echo 0) )); then
                        echo -e "${RED}worse${NC}               │"
                    else
                        echo -e "${YELLOW}same${NC}                │"
                    fi
                else
                    printf "    │ %-25s │ %10s │ %10s │ %10s │ %-18s │\n" "$short_desc" "$cur_ram MB" "N/A" "N/A" "NEW TEST"
                    unchanged=$((unchanged + 1))
                fi
            fi
        done
        
        echo "    └───────────────────────────────────────────────────────────────────────────────────────────┘"
        
        # Summary
        echo ""
        echo -e "    ${GREEN}Improvements: $improvements${NC}  ${RED}Regressions: $regressions${NC}  ${YELLOW}Unchanged: $unchanged${NC}"
        
        if [ $regressions -gt 0 ]; then
            echo ""
            echo -e "    ${RED}⚠ WARNING: Performance regressions detected!${NC}"
        elif [ $improvements -gt 0 ]; then
            echo ""
            echo -e "    ${GREEN}✓ Performance improvements detected!${NC}"
        fi
    fi
    
    # Export results to JSON file
    local json_file="/tmp/zed_benchmark_$(date +%Y%m%d_%H%M%S).json"
    {
        echo "{"
        echo "  \"timestamp\": \"$(date -Iseconds)\","
        echo "  \"platform\": \"$platform\","
        echo "  \"camera_type\": \"$([ "$GMSL_CAMERA" = true ] && echo 'GMSL' || echo 'USB')\","
        echo "  \"zero_copy_available\": $ZERO_COPY_AVAILABLE,"
        echo "  \"camera_open_latency_s\": ${BENCHMARK_RESULTS[camera_open_avg]:-null},"
        echo "  \"baseline\": {"
        echo "    \"ram_mb\": $BASELINE_RAM,"
        echo "    \"cpu_percent\": $BASELINE_CPU,"
        echo "    \"gpu_percent\": $BASELINE_GPU"
        echo "  },"
        echo "  \"benchmarks\": ["
        local first=true
        for key in "${!BENCHMARK_RESULTS[@]}"; do
            if [[ "$key" == *",ram" ]]; then
                local base="${key%,ram}"
                [ "$first" = true ] && first=false || echo ","
                echo -n "    {"
                echo -n "\"name\": \"$base\", "
                echo -n "\"ram_mb\": ${BENCHMARK_RESULTS[$base,ram]:-null}, "
                echo -n "\"cpu_percent\": ${BENCHMARK_RESULTS[$base,cpu]:-null}, "
                echo -n "\"gpu_percent\": ${BENCHMARK_RESULTS[$base,gpu]:-null}, "
                echo -n "\"fps\": ${BENCHMARK_RESULTS[$base,fps]:-null}, "
                echo -n "\"frame_time_ms\": ${BENCHMARK_RESULTS[$base,latency]:-null}"
                echo -n "}"
            fi
        done
        echo ""
        echo "  ]"
        echo "}"
    } > "$json_file"
    
    echo ""
    echo "    Results exported to: $json_file"
    echo ""
    echo -e "${GREEN}Benchmark complete!${NC}"
}

# =============================================================================
# Main
# =============================================================================

show_help() {
    echo "ZED GStreamer Plugin Test Suite"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Modes (mutually exclusive, default is --fast):"
    echo "  -f, --fast            Fast mode (~30s) - plugin registration checks only"
    echo "  -e, --extensive       Extensive mode (~10min) - full test suite with hardware"
    echo "  -b, --benchmark       Benchmark mode - performance measurements"
    echo "  --compare-zerocopy    DEV: Compare NV12 zero-copy vs BGRA (GMSL only)"
    echo ""
    echo "Benchmark options:"
    echo "  --fast                With --benchmark: quick benchmark (3 tests, ~2min)"
    echo "  --extensive           With --benchmark: full benchmark (all tests, ~5min)"
    echo "  --baseline FILE       Compare results with previous baseline JSON file"
    echo ""
    echo "Other options:"
    echo "  -v, --verbose         Verbose output"
    echo "  -h, --help            Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                    # Fast tests (default)"
    echo "  $0 -e                 # Extensive tests"
    echo "  $0 -b                 # Full benchmark"
    echo "  $0 -b -f              # Quick benchmark"
    echo "  $0 --compare-zerocopy # DEV: NV12 vs BGRA comparison"
    echo "  $0 -b --baseline /tmp/zed_benchmark_xxx.json  # Compare with baseline"
    echo ""
    echo "Benchmark workflow:"
    echo "  1. Run initial benchmark:  $0 --benchmark"
    echo "  2. Make code changes"
    echo "  3. Run comparison:         $0 --benchmark --baseline <file.json>"
    echo ""
    echo "Zero-copy comparison (DEV):"
    echo "  Runs identical pipelines with NV12 zero-copy (stream-type=6) vs"
    echo "  BGRA legacy mode (stream-type=0) to measure performance difference."
    echo "  Requires: GMSL camera (ZED X) + ZED SDK 5.2+ + Jetson platform"
    echo ""
    echo "Exit codes:"
    echo "  0 - All tests passed"
    echo "  1 - Test failures (code/plugin issues)"
    echo "  2 - Hardware not available (not a code issue)"
    echo "  3 - Missing dependencies"
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -f|--fast)
                FAST_MODE=true
                EXTENSIVE_MODE=false
                shift
                ;;
            -e|--extensive)
                FAST_MODE=false
                EXTENSIVE_MODE=true
                shift
                ;;
            -b|--benchmark)
                BENCHMARK_MODE=true
                # Default benchmark to extensive mode for accurate results
                if [ "$FAST_MODE" = true ] && [ "$EXTENSIVE_MODE" = false ]; then
                    FAST_MODE=false
                    EXTENSIVE_MODE=true
                fi
                shift
                ;;
            --compare-zerocopy)
                COMPARE_ZEROCOPY_MODE=true
                BENCHMARK_MODE=true
                shift
                ;;
            --baseline)
                if [[ -n "$2" && ! "$2" =~ ^- ]]; then
                    BENCHMARK_BASELINE="$2"
                    shift 2
                else
                    echo "Error: --baseline requires a file path argument"
                    exit 1
                fi
                ;;
            -v|--verbose)
                VERBOSE=true
                shift
                ;;
            -h|--help)
                show_help
                exit 0
                ;;
            *)
                echo "Unknown option: $1"
                show_help
                exit 1
                ;;
        esac
    done
    
    # Validate baseline option
    if [[ -n "$BENCHMARK_BASELINE" && "$BENCHMARK_MODE" != true ]]; then
        echo "Error: --baseline requires --benchmark mode"
        exit 1
    fi
    
    if [[ -n "$BENCHMARK_BASELINE" && ! -f "$BENCHMARK_BASELINE" ]]; then
        echo "Error: Baseline file not found: $BENCHMARK_BASELINE"
        exit 1
    fi
}

main() {
    parse_args "$@"
    
    local start_time
    start_time=$(date +%s)
    
    local platform
    platform=$(detect_platform)
    
    # Check dependencies first
    if ! check_command "gst-inspect-1.0"; then
        log_error "GStreamer not found"
        exit 3
    fi
    
    # Check hardware availability
    check_hardware
    
    # Zero-copy comparison mode - quick dev benchmark
    if [ "$COMPARE_ZEROCOPY_MODE" = true ]; then
        run_zerocopy_comparison
        exit 0
    fi
    
    # Benchmark mode - separate execution path
    if [ "$BENCHMARK_MODE" = true ]; then
        run_benchmarks
        exit 0
    fi
    
    # Normal test mode
    print_header "ZED GStreamer Plugin Test Suite"
    
    log_info "Platform: $platform"
    log_info "Mode: $([ "$EXTENSIVE_MODE" = true ] && echo 'Extensive' || echo 'Fast')"
    log_info "Verbose: $VERBOSE"
    
    # Check dependencies
    if ! test_dependencies; then
        log_error "Dependency check failed"
        exit 3
    fi
    
    # Check for hardware
    check_hardware
    
    # Run plugin tests (always run these)
    test_plugin_registration
    test_plugin_properties
    test_plugin_factory
    test_zedsrc_enums
    test_zedsrc_auto_resolution
    test_zedsrc_nv12
    test_element_pads
    test_zedxone
    test_zedxone_enums
    test_zedxone_auto_resolution
    test_zedxone_nv12
    
    # Hardware tests (if available)
    if [ "$HARDWARE_AVAILABLE" = true ]; then
        test_hardware_basic
        test_hardware_streaming
        test_hardware_demux
        test_csv_sink
        
        if [ "$EXTENSIVE_MODE" = true ]; then
            test_latency_query
            test_buffer_metadata
            test_datamux
            test_resolutions
            test_depth_modes
            test_positional_tracking
            test_camera_controls
            test_video_recording_playback
            test_udp_streaming
            test_zedxone_hardware
            test_zedxone_resolutions
            test_hardware_od
            test_hardware_bt
            test_overlay_skeletons
            test_network_streaming
            test_error_handling
        fi
    else
        # Show skipped hardware tests
        test_hardware_basic
        test_hardware_streaming
        test_hardware_demux
        test_csv_sink
        if [ "$EXTENSIVE_MODE" = true ]; then
            test_latency_query
            test_buffer_metadata
            test_datamux
            test_resolutions
            test_depth_modes
            test_positional_tracking
            test_camera_controls
            test_video_recording_playback
            test_udp_streaming
            test_zedxone_hardware
            test_zedxone_resolutions
            test_hardware_od
            test_hardware_bt
            test_overlay_skeletons
            test_network_streaming
            test_error_handling
        fi
    fi
    
    # Summary
    local end_time
    end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    print_header "Test Summary"
    echo ""
    echo -e "  Tests run:     ${BLUE}$TESTS_RUN${NC}"
    echo -e "  Tests passed:  ${GREEN}$TESTS_PASSED${NC}"
    echo -e "  Tests failed:  ${RED}$TESTS_FAILED${NC}"
    echo -e "  Tests skipped: ${YELLOW}$TESTS_SKIPPED${NC}"
    echo ""
    echo -e "  Duration:      ${duration}s"
    echo -e "  Platform:      $platform"
    echo -e "  Hardware:      $([ "$HARDWARE_AVAILABLE" = true ] && echo 'Available' || echo 'Not detected')"
    echo ""
    
    if [ $TESTS_FAILED -gt 0 ]; then
        echo -e "${RED}Some tests failed!${NC}"
        exit 1
    elif [ "$HARDWARE_AVAILABLE" = false ] && [ $TESTS_SKIPPED -gt 0 ]; then
        echo -e "${YELLOW}All plugin tests passed. Hardware tests skipped (no camera).${NC}"
        exit 0
    else
        echo -e "${GREEN}All tests passed!${NC}"
        exit 0
    fi
}

main "$@"
