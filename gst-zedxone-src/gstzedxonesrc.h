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
    gint _cameraResolution;   // * Camera resolution [enum]
    gint _cameraFps;          // * Camera FPS
    gint _sdkVerboseLevel;    // * Capture library verbose level
    gint _camTimeout_msec;    // * Camera communication timeout
    gint _cameraId;           // * Camera ID
    gint _cameraSN;           // * Camera Serial Number
    GString _svoFile;       // * SVO file path
    gboolean _svoRealTimeMode;   // * SVO real-time mode
    GString _opencvCalibrationFile;  // * OpenCV calibration file path
    GString _streamIp;      // * Stream IP address
    gint _streamPort;      // * Stream port

    gboolean _autoExposure;      // * Enable Automatic Exposure
    gint _exposureRange_min;     // * Minimum value for Automatic Exposure
    gint _exposureRange_max;     // * Maximum value for Automatic Exposure
    gint _manualExposure_usec;   // * Manual Exposure time

    gboolean _autoAnalogGain;    // * Enable Automatic Analog Gain
    gint _analogGainRange_min;   // * Minimum value for Automatic Analog Gain
    gint _analogGainRange_max;   // * Maximum value for Automatic Analog Gain
    gint _manualAnalogGain;      // * Manual Analog Gain

    gboolean _autoDigitalGain;    // * Enable Automatic Digital Gain
    gint _digitalGainRange_min;   // * Minimum value for Automatic Digital Gain
    gint _digitalGainRange_max;   // * Maximum value for Automatic Digital Gain
    gint _manualDigitalGain;      // * Manual Digital Gain [1,256]

    gboolean _autoWb;   // * Enable Automatic White Balance
    gint _manualWb;     // * Manual White Balance [2800,12000]

    gint _colorSaturation;        // * Color Saturation
    gint _denoising;              // * Image Denoising
    gint _exposureCompensation;   // * Exposure Compensation
    gint _sharpness;              // * Image Sharpness
    gint _gamma;                  // * Image Gamma
    // <---- Properties

    GstClockTime _acqStartTime;   // Acquisition start time

    GstCaps *caps;         // Stream caps
    guint _outFramesize;   // Output frame size in byte
};

struct _GstZedXOneSrcClass {
    GstPushSrcClass base_zedxonesrc_class;
};

G_GNUC_INTERNAL GType gst_zedxonesrc_get_type(void);

G_END_DECLS

#endif   // _GST_ZED_X_ONE_SRC_H_
