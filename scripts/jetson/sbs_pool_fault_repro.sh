#!/bin/bash
# =============================================================================
# Minimal NV12 SBS / nvjpegenc surface registration fault reproducer
# =============================================================================
#
# Invariant:
#   stream-type=7 must keep the number of distinct SBS NVMM destination surfaces
#   bounded while downstream holds buffers. Under backpressure it may slow down
#   or block, but it must not make NVJPG print "Surface not registered".
#
# This script intentionally applies encoder-side backpressure and scans stderr
# for the NVIDIA NVJPG failure signature.
#
# Usage:
#   ./sbs_pool_fault_repro.sh [timeout_seconds]
#
# Exit codes:
#   0 - NVJPG surface registration fault reproduced
#   1 - Signature not observed before timeout
#   2 - Prerequisites not met
# =============================================================================

set -u
set -o pipefail

TIMEOUT_SECONDS="${1:-300}"
SCRIPT_NAME="$(basename "$0")"
LOG_FILE="/tmp/zed_sbs_pool_fault_repro_$$.log"

CAMERA_RESOLUTION=2
CAMERA_FPS=60
WIDTH=3840
HEIGHT=1200
QUEUE_BUFFERS=60
HOLD_US=40000

cleanup() {
    rm -f "$LOG_FILE" 2>/dev/null || true
}
trap cleanup EXIT

usage() {
    echo "Usage: $SCRIPT_NAME [timeout_seconds]"
}

if [ "$#" -gt 1 ]; then
    usage
    exit 2
fi

case "$TIMEOUT_SECONDS" in
    ''|*[!0-9]*)
        echo "ERROR: timeout_seconds must be a positive integer"
        exit 2
        ;;
esac

if [ "$TIMEOUT_SECONDS" -le 0 ]; then
    echo "ERROR: timeout_seconds must be > 0"
    exit 2
fi

if ! command -v gst-launch-1.0 >/dev/null 2>&1; then
    echo "ERROR: gst-launch-1.0 not found"
    exit 2
fi

if ! gst-inspect-1.0 zedsrc >/dev/null 2>&1; then
    echo "ERROR: zedsrc plugin not found"
    exit 2
fi

if ! gst-inspect-1.0 nvjpegenc >/dev/null 2>&1; then
    echo "ERROR: nvjpegenc not found"
    exit 2
fi

ZEDSRC_INSPECT="$(gst-inspect-1.0 zedsrc 2>/dev/null)"
if ! printf '%s\n' "$ZEDSRC_INSPECT" | grep -q "Raw NV12 stereo"; then
    echo "ERROR: zedsrc does not advertise stream-type=7 NV12 stereo support"
    exit 2
fi

echo "=============================================="
echo " ZED SBS NVMM / nvjpegenc fault reproducer"
echo "=============================================="
echo " Timeout:       ${TIMEOUT_SECONDS}s"
echo " Camera:        resolution=${CAMERA_RESOLUTION}, fps=${CAMERA_FPS}"
echo " Output caps:   NV12 ${WIDTH}x${HEIGHT} memory:NVMM"
echo " Backpressure:  queue=${QUEUE_BUFFERS}, identity sleep=${HOLD_US}us"
echo " Looking for:   Surface not registered"
echo "=============================================="
echo ""

timeout "${TIMEOUT_SECONDS}s" \
    gst-launch-1.0 -e \
        zedsrc stream-type=7 camera-resolution="${CAMERA_RESOLUTION}" camera-fps="${CAMERA_FPS}" \
            enable-positional-tracking=false depth-mode=0 ! \
        "video/x-raw(memory:NVMM),format=NV12,width=${WIDTH},height=${HEIGHT}" ! \
        queue max-size-buffers="${QUEUE_BUFFERS}" max-size-bytes=0 max-size-time=0 ! \
        identity sleep-time="${HOLD_US}" ! \
        nvjpegenc ! \
        fakesink sync=false \
    2>&1 | tee "$LOG_FILE"

GST_STATUS=${PIPESTATUS[0]}

echo ""
echo "=============================================="
echo " Verdict"
echo "=============================================="

if grep -q \
    -e "Surface not registered" \
    -e "NVJPGGetSurfPinHandle" \
    -e "NVJPGPushSurfFalconMethodRelocShift" \
    -e "JPEGEncFeedFrame" \
    "$LOG_FILE"; then
    echo "REPRODUCED: NVJPG surface registration fault was printed."
    exit 0
fi

if [ "$GST_STATUS" -eq 124 ]; then
    echo "NOT reproduced: no NVJPG signature before timeout."
else
    echo "NOT reproduced: pipeline exited without the target NVJPG signature."
    echo "gst-launch exit status: $GST_STATUS"
fi

exit 1
