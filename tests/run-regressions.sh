#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
for test in zeddatamux zeddatamux-matching zeddatamux-threads zeddemux zeddemux-outputs; do
    ${CXX:-c++} -std=c++11 -pthread ${CXXFLAGS:-} -I. \
        $(pkg-config --cflags gstreamer-video-1.0) \
        "tests/test-$test.cpp" gst-zed-meta/gstzedmeta.cpp \
        $(pkg-config --libs gstreamer-video-1.0) ${LDFLAGS:-} \
        -o "$build_dir/test-$test"
    "$build_dir/test-$test"
done
