#!/bin/bash -e

# Example pipeline to acquire a stream at default resolution with RGB, Depth and Sensor Data and save them to a CSV file for logging

# 1) Start `zedxonesrc` to acquire RGB and depth at default resolution
# 2) Define `zeddemux` object named `demux` to demux the `zedxonesrc` composite stream creating two streams: RGB, and data.
# 3) Display RGB stream with FPS info
# 4) Save the sensor data in the home folder to CSV file named `test_csv.csv` replacing eventual existing data

gst-launch-1.0 \
zedxonesrc ! \
zeddemux is-mono=TRUE stream-data=TRUE name=demux \
demux.src_mono ! queue ! nvvideoconvert ! nv3dsink sync=false \
demux.src_data ! queue ! zeddatacsvsink location="${HOME}/test_csv.csv" append=FALSE
