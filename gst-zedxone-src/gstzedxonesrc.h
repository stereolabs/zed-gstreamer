// /////////////////////////////////////////////////////////////////////////

//
// Copyright (c) 2024, STEREOLABS.
//
// All rights reserved.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// /////////////////////////////////////////////////////////////////////////

#ifndef _GST_ZED_X_ONE_SRC_H_
#define _GST_ZED_X_ONE_SRC_H_

#include <gst/base/gstpushsrc.h>

#include "sl/CameraOne.hpp"

G_BEGIN_DECLS

#define GST_TYPE_ZED_X_ONE_SRC (gst_zedxonesrc_get_type())
#define GST_ZED_X_ONE_SRC(obj)                                                                     \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ZED_X_ONE_SRC, GstZedXOneSrc))
#define GST_ZED_X_ONE_SRC_CLASS(klass)                                                             \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ZED_X_ONE_SRC, GstZedXOneSrcClass))
#define GST_IS_ZED_X_ONE_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ZED_X_ONE_SRC))
#define GST_IS_ZED_X_ONE_SRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ZED_X_ONE_SRC))

typedef struct _GstZedXOneSrc GstZedXOneSrc;
typedef struct _GstZedXOneSrcClass GstZedXOneSrcClass;

struct _GstZedXOneSrc {
    GstPushSrc _base_zedxonesrc;

    // ZED X One camera object
    std::unique_ptr<sl::CameraOne> _zed;   // ZED X One object

    gboolean _isStarted;       // grab started flag
    gboolean _stopRequested;   // stop request flagout_framesize

    // ----> Properties
    gint _cameraResolution;            // Camera resolution [enum]
    gint _cameraFps;                   // Camera FPS
    gint _sdkVerboseLevel;             // Capture library verbose level
    gfloat _camTimeout_sec;            // Camera open timeout
    gint _cameraId;                    // Camera ID
    gint64 _cameraSN;                  // Camera Serial Number
    GString *_svoFile;                 // SVO file path
    GString *_streamIp;                // Input Stream IP
    gint _streamPort;                  // Input Stream Port
    GString *_opencvCalibrationFile;   // OpenCV calibration file path
    gboolean _cameraImageFlip;         // Camera flipped
    gboolean _enableHDR;               // HDR mode
    gboolean _svoRealTime;             // SVO Real Time Mode
    gint _coordUnit;                   // Coordinate unit
    gint _coordSys;                    // Coordinate system
    GString *_sdkLogFile;              // SDK Log file path
    GString *_settingsPath;            // Settings file path
    gboolean _asyncRecovery;           // Async grab camera recovery

    gint _saturation;   // Image Saturation
    gint _sharpness;    // Image Sharpness
    gint _gamma;        // Image Gamma
    gboolean _autoWb;   // Enable Automatic White Balance
    gint _manualWb;     // Manual White Balance [2800,6500]

    gboolean _autoExposure;       // Enable Automatic Exposure
    gint _exposure_usec;          // Manual Exposure time [28,30000]
    gint _exposureRange_min;      // Minimum value for Automatic Exposure [28,30000]
    gint _exposureRange_max;      // Maximum value for Automatic Exposure [28,30000]
    gint _exposureCompensation;   // Exposure Compensation [0,100]

    gboolean _autoAnalogGain;    // Enable Automatic Analog Gain
    gint _analogGain;            // Manual Analog Gain [1000,16000]
    gint _analogGainRange_min;   // Minimum value for Automatic Analog Gain [1000,16000]
    gint _analogGainRange_max;   // Maximum value for Automatic Analog Gain [1000,16000]

    gboolean _autoDigitalGain;    // Enable Automatic Digital Gain
    gint _digitalGain;            // Manual Digital Gain [1,256]
    gint _digitalGainRange_min;   // Minimum value for Automatic Digital Gain [1,256]
    gint _digitalGainRange_max;   // Maximum value for Automatic Digital Gain [1,256]

    gint _denoising;   // Image Denoising [0,100]

    gboolean _outputRectifiedImage;   // Output rectified image (FALSE for custom optics)

    gint _streamType;           // Stream type [enum]
    gint _resolvedStreamType;   // Actual stream type after auto-negotiation (-1 = not resolved)
    // <---- Properties

    int _realFps;   // Real FPS

    GstClockTime _acqStartTime;   // Acquisition start time

    guint64 _bufferIndex;   // Buffer index counter

    GstCaps *_caps;        // Stream caps
    guint _outFramesize;   // Output frame size in byte

    // Resolution tracking for flexible output caps
    guint _cameraWidth;    // Native camera resolution width
    guint _cameraHeight;   // Native camera resolution height
    guint _outputWidth;    // Negotiated output resolution width
    guint _outputHeight;   // Negotiated output resolution height
};

struct _GstZedXOneSrcClass {
    GstPushSrcClass base_zedxonesrc_class;
};

G_GNUC_INTERNAL GType gst_zedxonesrc_get_type(void);

G_END_DECLS

#endif   // _GST_ZED_X_ONE_SRC_H_
