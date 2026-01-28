#!/usr/bin/env python3
"""
Buffer Hold Test for Zero-Copy NV12 Analysis

This script tests the behavior of zero-copy NV12 buffers when they are held
(not released immediately). This simulates scenarios where downstream processing
takes longer than expected, helping analyze:
  - Buffer pool exhaustion
  - Argus capture queue behavior  
  - ZED SDK internal buffer management
  - Frame drops and latency under buffer pressure

Usage:
  ./buffer_hold_test.py --source zedsrc --hold-time 50
  ./buffer_hold_test.py --source argus --hold-time 100 --num-buffers 60
  ./buffer_hold_test.py --source zedsrc --hold-buffers 3  # Hold last N buffers

Requirements:
  - GStreamer 1.0 with Python bindings (python3-gst-1.0)
  - For zedsrc: ZED SDK 5.2+ with NV12 zero-copy support
  - For argus: nvarguscamerasrc (Jetson only)
"""

import argparse
import sys
import time
import threading
from collections import deque
from datetime import datetime

import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstApp', '1.0')
from gi.repository import Gst, GstApp, GLib

# Initialize GStreamer
Gst.init(None)


class BufferHoldTest:
    """Test zero-copy buffer behavior by holding buffers before release."""
    
    def __init__(self, args):
        self.source_type = args.source
        self.hold_time_ms = args.hold_time
        self.hold_buffers = args.hold_buffers
        self.num_buffers = args.num_buffers
        self.resolution = args.resolution
        self.fps = args.fps
        self.verbose = args.verbose
        self.stereo = args.stereo
        
        # Statistics
        self.buffer_count = 0
        self.dropped_count = 0
        self.start_time = None
        self.last_pts = None
        self.pts_gaps = []
        self.buffer_timestamps = []
        
        # Buffer holding
        self.held_buffers = deque(maxlen=self.hold_buffers if self.hold_buffers else None)
        self.buffer_lock = threading.Lock()
        
        # Pipeline
        self.pipeline = None
        self.loop = None
        self.running = False
        
    def get_resolution_params(self):
        """Get width/height based on resolution enum."""
        resolutions = {
            0: (2208, 1242),  # HD2K
            1: (1920, 1080),  # HD1080
            2: (1920, 1200),  # HD1200 (ZED X native)
            3: (1280, 720),   # HD720
            4: (672, 376),    # VGA
            5: (640, 480),    # WVGA (Argus)
        }
        return resolutions.get(self.resolution, (1920, 1200))
    
    def build_pipeline(self):
        """Build the GStreamer pipeline based on source type."""
        width, height = self.get_resolution_params()
        
        # For stereo, width is doubled (side-by-side)
        if self.stereo:
            width *= 2
        
        if self.source_type == 'zedsrc':
            # ZED SDK NV12 zero-copy mode
            stream_type = 7 if self.stereo else 6  # RAW_NV12_STEREO or RAW_NV12
            source = (
                f'zedsrc stream-type={stream_type} '
                f'camera-resolution={self.resolution} camera-fps={self.fps} '
                f'enable-positional-tracking=false depth-mode=0'
            )
            caps = f"video/x-raw(memory:NVMM),format=NV12,width={width},height={height}"
            
        elif self.source_type == 'argus':
            # nvarguscamerasrc (raw Argus, no ZED SDK)
            source = f'nvarguscamerasrc sensor-id=0'
            caps = f"video/x-raw(memory:NVMM),format=NV12,width={width},height={height},framerate={self.fps}/1"
            
        else:
            raise ValueError(f"Unknown source type: {self.source_type}")
        
        # Build pipeline with appsink
        # drop=false ensures we see all buffers (or drops if pool exhausted)
        # max-buffers=1 with drop=true would drop old buffers
        pipeline_str = (
            f'{source} ! {caps} ! '
            f'appsink name=sink emit-signals=true drop=false max-buffers=1 sync=false'
        )
        
        if self.verbose:
            print(f"\n[Pipeline] {pipeline_str}\n")
        
        self.pipeline = Gst.parse_launch(pipeline_str)
        
        # Connect appsink signal
        sink = self.pipeline.get_by_name('sink')
        sink.connect('new-sample', self.on_new_sample)
        
        # Bus for messages
        bus = self.pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect('message', self.on_bus_message)
        
    def on_new_sample(self, sink):
        """Called when a new buffer is available."""
        sample = sink.emit('pull-sample')
        if sample is None:
            return Gst.FlowReturn.ERROR
        
        buffer = sample.get_buffer()
        receive_time = time.time()
        
        if self.start_time is None:
            self.start_time = receive_time
        
        self.buffer_count += 1
        
        # Get buffer info
        pts = buffer.pts
        pts_sec = pts / Gst.SECOND if pts != Gst.CLOCK_TIME_NONE else -1
        
        # Check for PTS gaps (potential drops)
        if self.last_pts is not None and pts != Gst.CLOCK_TIME_NONE:
            expected_interval = 1.0 / self.fps
            actual_interval = (pts - self.last_pts) / Gst.SECOND
            if actual_interval > expected_interval * 1.5:  # 50% tolerance
                gap_frames = int(actual_interval / expected_interval) - 1
                self.pts_gaps.append((self.buffer_count, gap_frames, actual_interval))
                self.dropped_count += gap_frames
                if self.verbose:
                    print(f"  ⚠ PTS gap detected: {gap_frames} frames missing "
                          f"(interval: {actual_interval*1000:.1f}ms vs expected {expected_interval*1000:.1f}ms)")
        
        self.last_pts = pts
        
        # Record timing
        elapsed = receive_time - self.start_time
        self.buffer_timestamps.append((self.buffer_count, elapsed, pts_sec))
        
        if self.verbose:
            print(f"  Buffer {self.buffer_count:4d}: PTS={pts_sec:8.3f}s, elapsed={elapsed:8.3f}s, "
                  f"held={len(self.held_buffers)}")
        
        # Hold buffer strategy
        if self.hold_time_ms > 0:
            # Strategy 1: Hold each buffer for a fixed time
            # This simulates slow downstream processing
            time.sleep(self.hold_time_ms / 1000.0)
            
        if self.hold_buffers > 0:
            # Strategy 2: Keep last N buffers in memory
            # This simulates buffering/caching behavior
            with self.buffer_lock:
                # Keep a reference to the buffer to prevent release
                self.held_buffers.append(buffer)
        
        # Check if we've received enough buffers
        if self.num_buffers > 0 and self.buffer_count >= self.num_buffers:
            if self.verbose:
                print(f"\n  Reached {self.num_buffers} buffers, stopping...")
            GLib.idle_add(self.stop)
        
        return Gst.FlowReturn.OK
    
    def on_bus_message(self, bus, message):
        """Handle GStreamer bus messages."""
        t = message.type
        
        if t == Gst.MessageType.EOS:
            print("\n[EOS] End of stream")
            self.stop()
            
        elif t == Gst.MessageType.ERROR:
            err, debug = message.parse_error()
            print(f"\n[ERROR] {err.message}")
            if self.verbose and debug:
                print(f"  Debug: {debug}")
            self.stop()
            
        elif t == Gst.MessageType.WARNING:
            warn, debug = message.parse_warning()
            print(f"\n[WARNING] {warn.message}")
            
        elif t == Gst.MessageType.QOS:
            # Quality of Service - indicates dropped buffers
            qos_vals = message.parse_qos()
            if self.verbose:
                print(f"  [QOS] Dropped buffer reported")
                
        elif t == Gst.MessageType.STATE_CHANGED:
            if message.src == self.pipeline:
                old, new, pending = message.parse_state_changed()
                if self.verbose:
                    print(f"  [State] {old.value_nick} -> {new.value_nick}")
    
    def run(self):
        """Run the test."""
        print("=" * 70)
        print("Buffer Hold Test - Zero-Copy NV12 Analysis")
        print("=" * 70)
        print(f"  Source:       {self.source_type}")
        print(f"  Resolution:   {self.get_resolution_params()} ({'stereo SBS' if self.stereo else 'mono'})")
        print(f"  FPS:          {self.fps}")
        print(f"  Hold time:    {self.hold_time_ms}ms per buffer")
        print(f"  Hold buffers: {self.hold_buffers} (keep last N in memory)")
        print(f"  Num buffers:  {self.num_buffers if self.num_buffers > 0 else 'unlimited'}")
        print("=" * 70)
        
        try:
            self.build_pipeline()
        except Exception as e:
            print(f"\n[ERROR] Failed to build pipeline: {e}")
            return 1
        
        print("\nStarting pipeline... (Ctrl+C to stop)\n")
        
        self.running = True
        self.pipeline.set_state(Gst.State.PLAYING)
        
        self.loop = GLib.MainLoop()
        
        try:
            self.loop.run()
        except KeyboardInterrupt:
            print("\n\n[Interrupted by user]")
        
        self.stop()
        self.print_summary()
        
        return 0
    
    def stop(self):
        """Stop the pipeline and main loop."""
        if not self.running:
            return
            
        self.running = False
        
        if self.pipeline:
            self.pipeline.set_state(Gst.State.NULL)
        
        if self.loop and self.loop.is_running():
            self.loop.quit()
    
    def print_summary(self):
        """Print test summary and analysis."""
        print("\n" + "=" * 70)
        print("Test Summary")
        print("=" * 70)
        
        if self.buffer_count == 0:
            print("  No buffers received!")
            return
        
        duration = self.buffer_timestamps[-1][1] if self.buffer_timestamps else 0
        actual_fps = self.buffer_count / duration if duration > 0 else 0
        expected_frames = int(duration * self.fps)
        
        print(f"  Duration:         {duration:.2f}s")
        print(f"  Buffers received: {self.buffer_count}")
        print(f"  Expected frames:  {expected_frames} (at {self.fps} fps)")
        print(f"  Actual FPS:       {actual_fps:.2f}")
        print(f"  PTS gaps:         {len(self.pts_gaps)} (est. {self.dropped_count} dropped frames)")
        
        if self.hold_time_ms > 0:
            theoretical_max_fps = 1000 / self.hold_time_ms
            print(f"\n  Hold time impact:")
            print(f"    Hold time:       {self.hold_time_ms}ms")
            print(f"    Theoretical max: {theoretical_max_fps:.1f} fps")
            print(f"    Achieved:        {actual_fps:.1f} fps")
            
            if actual_fps < self.fps * 0.9:
                print(f"\n  ⚠ Frame rate degraded! Buffer hold time is causing backpressure.")
                print(f"    The source cannot sustain {self.fps} fps with {self.hold_time_ms}ms hold time.")
        
        if self.hold_buffers > 0:
            print(f"\n  Buffer holding impact:")
            print(f"    Max held:        {self.hold_buffers} buffers")
            print(f"    Memory impact:   Buffers kept in NVMM until dequeued")
        
        if self.pts_gaps:
            print(f"\n  PTS Gap Details (frame drops):")
            for buf_num, gap_frames, interval in self.pts_gaps[:10]:  # Show first 10
                print(f"    At buffer {buf_num}: {gap_frames} frames dropped "
                      f"(interval: {interval*1000:.1f}ms)")
            if len(self.pts_gaps) > 10:
                print(f"    ... and {len(self.pts_gaps) - 10} more gaps")
        
        # Analysis
        print("\n" + "-" * 70)
        print("Analysis")
        print("-" * 70)
        
        if self.dropped_count > 0:
            drop_rate = self.dropped_count / (self.buffer_count + self.dropped_count) * 100
            print(f"  Drop rate: {drop_rate:.1f}%")
            
            if self.source_type == 'zedsrc':
                print("\n  ZED SDK zero-copy behavior:")
                print("    When buffers aren't returned quickly, the SDK's internal")
                print("    buffer pool may become exhausted. The SDK will either:")
                print("    - Wait for buffers (causing latency)")
                print("    - Drop frames (if configured to do so)")
                print("    - Return errors if pool is fully exhausted")
            else:
                print("\n  nvarguscamerasrc behavior:")
                print("    Argus uses a fixed buffer pool. When exhausted:")
                print("    - New captures are blocked until a buffer is returned")
                print("    - This can cause frame drops at the ISP level")
                print("    - May see 'nvbuf_utils' warnings in the console")
        else:
            print("  No frame drops detected - buffer pool is sufficient for this hold time.")
        
        print("\n" + "=" * 70)


def main():
    parser = argparse.ArgumentParser(
        description='Test zero-copy NV12 buffer behavior by holding buffers',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Test ZED SDK with 50ms hold time (simulates slow processing)
  %(prog)s --source zedsrc --hold-time 50

  # Test Argus with 100ms hold time for 120 buffers
  %(prog)s --source argus --hold-time 100 --num-buffers 120

  # Hold last 5 buffers in memory (test buffer pool exhaustion)
  %(prog)s --source zedsrc --hold-buffers 5 --num-buffers 60

  # Compare both sources with same parameters
  %(prog)s --source zedsrc --hold-time 30 --num-buffers 300
  %(prog)s --source argus --hold-time 30 --num-buffers 300

  # Test stereo mode (ZED SDK only)
  %(prog)s --source zedsrc --stereo --hold-time 50
        """
    )
    
    parser.add_argument('--source', '-s', choices=['zedsrc', 'argus'], default='zedsrc',
                        help='Video source: zedsrc (ZED SDK NV12) or argus (nvarguscamerasrc)')
    
    parser.add_argument('--hold-time', '-t', type=int, default=50,
                        help='Time in ms to hold each buffer before releasing (default: 50)')
    
    parser.add_argument('--hold-buffers', '-b', type=int, default=0,
                        help='Keep last N buffers in memory simultaneously (default: 0)')
    
    parser.add_argument('--num-buffers', '-n', type=int, default=0,
                        help='Number of buffers to capture (0 = unlimited, default: 0)')
    
    parser.add_argument('--resolution', '-r', type=int, default=2,
                        help='Resolution enum: 0=HD2K, 1=HD1080, 2=HD1200, 3=HD720 (default: 2)')
    
    parser.add_argument('--fps', '-f', type=int, default=60,
                        help='Target frame rate (default: 60)')
    
    parser.add_argument('--stereo', action='store_true',
                        help='Use stereo mode (ZED SDK only, side-by-side)')
    
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Verbose output (show each buffer)')
    
    args = parser.parse_args()
    
    # Validation
    if args.stereo and args.source == 'argus':
        print("Error: Stereo mode is only supported with zedsrc")
        return 1
    
    if args.hold_time < 0:
        print("Error: hold-time must be >= 0")
        return 1
    
    test = BufferHoldTest(args)
    return test.run()


if __name__ == '__main__':
    sys.exit(main())
