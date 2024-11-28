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

#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <math.h>
#include <unistd.h>

#include "gst-zed-meta/gstzedmeta.h"
#include "gstzedxonesrc.h"

#include <chrono>

GST_DEBUG_CATEGORY_STATIC(gst_zedxonesrc_debug);
#define GST_CAT_DEFAULT gst_zedxonesrc_debug

/* prototypes */
static void gst_zedxonesrc_set_property(GObject *object, guint property_id, const GValue *value,
                                        GParamSpec *pspec);
static void gst_zedxonesrc_get_property(GObject *object, guint property_id, GValue *value,
                                        GParamSpec *pspec);
static void gst_zedxonesrc_dispose(GObject *object);
static void gst_zedxonesrc_finalize(GObject *object);

static gboolean gst_zedxonesrc_start(GstBaseSrc *src);
static gboolean gst_zedxonesrc_stop(GstBaseSrc *src);
static GstCaps *gst_zedxonesrc_get_caps(GstBaseSrc *src, GstCaps *filter);
static gboolean gst_zedxonesrc_set_caps(GstBaseSrc *src, GstCaps *caps);
static gboolean gst_zedxonesrc_unlock(GstBaseSrc *src);
static gboolean gst_zedxonesrc_unlock_stop(GstBaseSrc *src);

static GstFlowReturn gst_zedxonesrc_fill(GstPushSrc *src, GstBuffer *buf);

enum {
    PROP_0,
    PROP_CAM_RES,
    PROP_CAM_FPS,
    PROP_VERBOSE_LVL,
    PROP_TIMEOUT_SEC,
    PROP_CAM_ID,
    PROP_CAM_SN,
    PROP_OPENCV_CALIB_FILE,
    PROP_IMAGE_FLIP,
    PROP_ENABLE_HDR,
    PROP_SATURATION,
    PROP_SHARPNESS,
    PROP_GAMMA,
    PROP_AUTO_WB,
    PROP_WB_TEMP,
    PROP_AUTO_EXPOSURE,
    PROP_EXPOSURE,
    PROP_EXPOSURE_RANGE_MIN,
    PROP_EXPOSURE_RANGE_MAX,
    PROP_EXP_COMPENSATION,
    PROP_AUTO_ANALOG_GAIN,
    PROP_ANALOG_GAIN,
    PROP_ANALOG_GAIN_RANGE_MIN,
    PROP_ANALOG_GAIN_RANGE_MAX,
    PROP_AUTO_DIGITAL_GAIN,
    PROP_DIGITAL_GAIN,
    PROP_DIGITAL_GAIN_RANGE_MIN,
    PROP_DIGITAL_GAIN_RANGE_MAX,
    PROP_DENOISING,
    N_PROPERTIES
};

typedef enum {
    GST_ZEDXONESRC_SVGA,    // 960 x 600
    GST_ZEDXONESRC_1080P,   // 1920 x 1080
    GST_ZEDXONESRC_1200P,   // 1920 x 1200
    GST_ZEDXONESRC_QHDPLUS, // 3800x1800
    GST_ZEDXONESRC_4K       // 3840 x 2160
} GstZedXOneSrcRes;

typedef enum {
    GST_ZEDXONESRC_120FPS = 120,
    GST_ZEDXONESRC_60FPS = 60,
    GST_ZEDXONESRC_30FPS = 30,
    GST_ZEDXONESRC_15FPS = 15
} GstZedXOneSrcFPS;

//////////////// DEFAULT PARAMETERS
/////////////////////////////////////////////////////////////////////////////

#define DEFAULT_PROP_CAM_RES GST_ZEDXONESRC_1200P
#define DEFAULT_PROP_CAM_FPS GST_ZEDXONESRC_30FPS
#define DEFAULT_PROP_VERBOSE_LVL 1
#define DEFAULT_PROP_TIMEOUT_SEC 5.0f
#define DEFAULT_PROP_CAM_ID -1
#define DEFAULT_PROP_CAM_SN 0
#define DEFAULT_PROP_CAM_FLIP FALSE
#define DEFAULT_PROP_ENABLE_HDR FALSE
#define DEFAULT_PROP_OPENCV_CALIB_FILE ""
#define DEFAULT_PROP_SATURATION 4
#define DEFAULT_PROP_SHARPNESS 4
#define DEFAULT_PROP_GAMMA 8
#define DEFAULT_PROP_AUTO_WB TRUE
#define DEFAULT_PROP_WB_TEMP 4200
#define DEFAULT_PROP_AUTO_EXPOSURE TRUE
#define DEFAULT_PROP_EXPOSURE 10000
#define DEFAULT_PROP_EXPOSURE_RANGE_MIN 28
#define DEFAULT_PROP_EXPOSURE_RANGE_MAX 30000
#define DEFAULT_PROP_EXP_COMPENSATION 50
#define DEFAULT_PROP_AUTO_ANALOG_GAIN TRUE
#define DEFAULT_PROP_ANALOG_GAIN 1000
#define DEFAULT_PROP_ANALOG_GAIN_RANGE_MIN 1000
#define DEFAULT_PROP_ANALOG_GAIN_RANGE_MAX 16000
#define DEFAULT_PROP_AUTO_DIGITAL_GAIN FALSE
#define DEFAULT_PROP_DIGITAL_GAIN 1
#define DEFAULT_PROP_DIGITAL_GAIN_RANGE_MIN 1
#define DEFAULT_PROP_DIGITAL_GAIN_RANGE_MAX 256
#define DEFAULT_PROP_DENOISING 50
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define GST_TYPE_ZEDXONE_RESOL (gst_zedxonesrc_resol_get_type())
static GType gst_zedxonesrc_resol_get_type(void) {
    static GType zedxonesrc_resol_type = 0;

    if (!zedxonesrc_resol_type) {
        static GEnumValue pattern_types[] = {
            {static_cast<gint>(GST_ZEDXONESRC_4K), "3840x2160", "4K"},
            {static_cast<gint>(GST_ZEDXONESRC_QHDPLUS), "3800x1800", "QHDPLUS"},
            {static_cast<gint>(GST_ZEDXONESRC_1200P), "1920x1200", "HD1200"},
            {static_cast<gint>(GST_ZEDXONESRC_1080P), "1920x1080", "HD1080"},
            {static_cast<gint>(GST_ZEDXONESRC_SVGA), "960x600", "SVGA"},
            {0, NULL, NULL},
        };

        zedxonesrc_resol_type = g_enum_register_static("GstZedXOneSrcResol", pattern_types);
    }

    return zedxonesrc_resol_type;
}

#define GST_TYPE_ZED_FPS (gst_zedxonesrc_fps_get_type())
static GType gst_zedxonesrc_fps_get_type(void) {
    static GType zedxonesrc_fps_type = 0;

    if (!zedxonesrc_fps_type) {
        static GEnumValue pattern_types[] = {
            {GST_ZEDXONESRC_120FPS, "Only with SVGA", "120 FPS"},
            {GST_ZEDXONESRC_60FPS, "Not available with 4K mode", "60  FPS"},
            {GST_ZEDXONESRC_30FPS, "Not available with 4K mode", "30  FPS"},
            {GST_ZEDXONESRC_15FPS, "all resolutions (NO GMSL2)", "15  FPS"},
            {0, NULL, NULL},
        };

        zedxonesrc_fps_type = g_enum_register_static("GstZedXOneSrcFPS", pattern_types);
    }

    return zedxonesrc_fps_type;
}

/* pad templates */
static GstStaticPadTemplate gst_zedxonesrc_src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS(("video/x-raw, "   // Color 4K
                                             "format = (string)BGRA, "
                                             "width = (int)3840, "
                                             "height = (int)2160, "
                                             "framerate = (fraction)15"
                                             ";"
                                             "video/x-raw, "   // Color QHDPLUS
                                             "format = (string)BGRA, "
                                             "width = (int)3800, "
                                             "height = (int)1800, "
                                             "framerate = (fraction)15"
                                             ";"
                                             "video/x-raw, "   // Color HD1200
                                             "format = (string)BGRA, "
                                             "width = (int)1920, "
                                             "height = (int)1200, "
                                             "framerate = (fraction) { 15, 30, 60 }"
                                             ";"
                                             "video/x-raw, "   // Color HD1080
                                             "format = (string)BGRA, "
                                             "width = (int)1920, "
                                             "height = (int)1080, "
                                             "framerate = (fraction) { 15, 30, 60 }"
                                             ";"
                                             "video/x-raw, "   // Color SVGA
                                             "format = (string)BGRA, "
                                             "width = (int)960, "
                                             "height = (int)600, "
                                             "framerate = (fraction) { 15, 30, 60, 120 }"
                                             ";"
                                             "video/x-raw, "   // Color 4K
                                             "format = (string)RGBA, "
                                             "width = (int)3840, "
                                             "height = (int)2160, "
                                             "framerate = (fraction)15"
                                             ";"
                                             "video/x-raw, "   // Color HD1200
                                             "format = (string)RGBA, "
                                             "width = (int)1920, "
                                             "height = (int)1200, "
                                             "framerate = (fraction) { 15, 30, 60 }"
                                             ";"
                                             "video/x-raw, "   // Color HD1080
                                             "format = (string)RGBA, "
                                             "width = (int)1920, "
                                             "height = (int)1080, "
                                             "framerate = (fraction) { 15, 30, 60 }"
                                             ";"
                                             "video/x-raw, "   // Color SVGA
                                             "format = (string)RGBA, "
                                             "width = (int)960, "
                                             "height = (int)600, "
                                             "framerate = (fraction) { 15, 30, 60, 120 }")));

/* Tools */
bool resol_to_w_h(const GstZedXOneSrcRes &resol, guint32 &out_w, guint32 &out_h) {
    switch (resol) {
    case GST_ZEDXONESRC_SVGA:
        out_w = 960;
        out_h = 600;
        break;

    case GST_ZEDXONESRC_1080P:
        out_w = 1920;
        out_h = 1080;
        break;

    case GST_ZEDXONESRC_1200P:
        out_w = 1920;
        out_h = 1200;
        break;

    case GST_ZEDXONESRC_QHDPLUS:
        out_w = 3800;
        out_h = 1800;
        break;

    case GST_ZEDXONESRC_4K:
        out_w = 3840;
        out_h = 2160;
        break;

    default:
        out_w = -1;
        out_h = -1;
        return false;
    }

    return true;
}

/* class initialization */
G_DEFINE_TYPE(GstZedXOneSrc, gst_zedxonesrc, GST_TYPE_PUSH_SRC);

static void gst_zedxonesrc_class_init(GstZedXOneSrcClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
    GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS(klass);
    GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS(klass);

    gobject_class->set_property = gst_zedxonesrc_set_property;
    gobject_class->get_property = gst_zedxonesrc_get_property;
    gobject_class->dispose = gst_zedxonesrc_dispose;
    gobject_class->finalize = gst_zedxonesrc_finalize;

    gst_element_class_add_pad_template(gstelement_class,
                                       gst_static_pad_template_get(&gst_zedxonesrc_src_template));

    gst_element_class_set_static_metadata(gstelement_class, "ZED X One Camera Source",
                                          "Source/Video", "Stereolabs ZED X One Camera source",
                                          "Stereolabs <support@stereolabs.com>");

    gstbasesrc_class->start = GST_DEBUG_FUNCPTR(gst_zedxonesrc_start);
    gstbasesrc_class->stop = GST_DEBUG_FUNCPTR(gst_zedxonesrc_stop);
    gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR(gst_zedxonesrc_get_caps);
    gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR(gst_zedxonesrc_set_caps);
    gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR(gst_zedxonesrc_unlock);
    gstbasesrc_class->unlock_stop = GST_DEBUG_FUNCPTR(gst_zedxonesrc_unlock_stop);

    gstpushsrc_class->fill = GST_DEBUG_FUNCPTR(gst_zedxonesrc_fill);

    /* Install GObject properties */
    g_object_class_install_property(
        gobject_class, PROP_CAM_RES,
        g_param_spec_enum("camera-resolution", "Camera Resolution", "Camera Resolution",
                          GST_TYPE_ZEDXONE_RESOL, DEFAULT_PROP_CAM_RES,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_FPS,
        g_param_spec_enum("camera-fps", "Camera frame rate", "Camera frame rate", GST_TYPE_ZED_FPS,
                          DEFAULT_PROP_CAM_FPS,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_VERBOSE_LVL,
        g_param_spec_int("verbose-level", "Capture Library Verbose",
                         "Capture Library Verbose level", 0, 4, DEFAULT_PROP_VERBOSE_LVL,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_TIMEOUT_SEC,
        g_param_spec_int("camera-timeout", "Open Timeout [sec]", "Connection opening timeout in seconds",
                         100, 100000000, DEFAULT_PROP_TIMEOUT_SEC,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_ID,
        g_param_spec_int("camera-id", "Camera ID", "Select camera from cameraID", 0, 255,
                         DEFAULT_PROP_CAM_ID,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_SN,
        g_param_spec_int("camera-sn", "Camera Serial Number", "Select camera from the serial number", 0, 999999999,
                         DEFAULT_PROP_CAM_ID,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OPENCV_CALIB_FILE,
        g_param_spec_string("opencv-calibration-file", "Optional OpenCV Calibration File", "Optional OpenCV Calibration File", 
                            DEFAULT_PROP_OPENCV_CALIB_FILE,
                            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_IMAGE_FLIP,
        g_param_spec_boolean("camera-flip", "Camera flip status",
                             "Invert image if camera is flipped", DEFAULT_PROP_CAM_FLIP,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    
    g_object_class_install_property(
        gobject_class, PROP_SATURATION,
        g_param_spec_int("ctrl-saturation", "Camera control: saturation", "Image saturation", 0, 8,
                         DEFAULT_PROP_SATURATION,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    
    g_object_class_install_property(
        gobject_class, PROP_SHARPNESS,
        g_param_spec_int("ctrl-sharpness", "Camera control: sharpness", "Image sharpness", 0, 8,
                         DEFAULT_PROP_SHARPNESS,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    
    g_object_class_install_property(
        gobject_class, PROP_GAMMA,
        g_param_spec_int("ctrl-gamma", "Camera control: gamma", "Image gamma", 1, 9, DEFAULT_PROP_GAMMA,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    
    g_object_class_install_property(
        gobject_class, PROP_AUTO_WB,
        g_param_spec_boolean("ctrl-whitebalance-auto", "Camera control: automatic whitebalance",
                             "Image automatic white balance", DEFAULT_PROP_AUTO_WB,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    
    g_object_class_install_property(
        gobject_class, PROP_WB_TEMP,
        g_param_spec_int("ctrl-whitebalance-temperature", "Camera control: white balance temperature",
                         "Image white balance temperature", 2800, 6500, DEFAULT_PROP_WB_TEMP,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_AUTO_EXPOSURE,
        g_param_spec_boolean("auto-exposure", "Camera control: Automatic Exposure", "Enable Automatic Exposure",
                             DEFAULT_PROP_AUTO_EXPOSURE,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_EXPOSURE,
        g_param_spec_int("exposure-time", "Camera control: Exposure time [µsec]", "Exposure time in microseconds",
                         28, 66000, DEFAULT_PROP_EXPOSURE,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_EXPOSURE_RANGE_MIN,
        g_param_spec_int("auto-exposure-range-min", "Camera control: Minimum Exposure time [µsec]",
                         "Minimum exposure time in microseconds for the automatic exposure setting",
                         28, 66000, DEFAULT_PROP_EXPOSURE_RANGE_MIN,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_EXPOSURE_RANGE_MAX,
        g_param_spec_int("auto-exposure-range-max", "Camera control: Maximum Exposure time [µsec]",
                         "Maximum exposure time in microseconds for the automatic exposure setting",
                         28, 66000, DEFAULT_PROP_EXPOSURE_RANGE_MAX,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_EXP_COMPENSATION,
        g_param_spec_int("exposure-compensation", "Camera control: Exposure Compensation", "Exposure Compensation",
                         0, 100, DEFAULT_PROP_EXP_COMPENSATION,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_AUTO_ANALOG_GAIN,
        g_param_spec_boolean("auto-analog-gain", "Camera control: Automatic Analog Gain",
                             "Enable Automatic Analog Gain", DEFAULT_PROP_AUTO_ANALOG_GAIN,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ANALOG_GAIN,
        g_param_spec_int("analog-gain", "Analog Gain", "Camera control: Analog Gain value", 1000, 16000,
                         DEFAULT_PROP_ANALOG_GAIN,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ANALOG_GAIN_RANGE_MIN,
        g_param_spec_int("auto-analog-gain-range-min", "Camera control: Minimum Automatic Analog Gain",
                         "Minimum Analog Gain for the automatic analog gain setting", 1000, 16000,
                         DEFAULT_PROP_ANALOG_GAIN_RANGE_MIN,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ANALOG_GAIN_RANGE_MAX,
        g_param_spec_int("auto-analog-gain-range-max", "Camera control: Maximum Automatic Analog Gain",
                         "Maximum Analog Gain for the automatic analog gain setting", 1000, 16000,
                         DEFAULT_PROP_ANALOG_GAIN_RANGE_MAX,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_AUTO_DIGITAL_GAIN,
        g_param_spec_boolean("auto-digital-gain", "Camera control: Automatic Digital Gain",
                             "Enable Automatic Digital Gain", DEFAULT_PROP_AUTO_DIGITAL_GAIN,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_DIGITAL_GAIN,
        g_param_spec_int("digital-gain", "Camera control: Digital Gain", "Digital Gain value", 1, 256,
                         DEFAULT_PROP_DIGITAL_GAIN,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_DIGITAL_GAIN_RANGE_MIN,
        g_param_spec_int("auto-digital-gain-range-min", "Camera control: Minimum Automatic Digital Gain",
                         "Minimum Digital Gain for the automatic digital gain setting", 1, 256,
                         DEFAULT_PROP_DIGITAL_GAIN_RANGE_MIN,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_DIGITAL_GAIN_RANGE_MAX,
        g_param_spec_int("auto-digital-gain-range-max", "Camera control: Maximum Automatic Digital Gain",
                         "Maximum Digital Gain for the automatic digital gain setting", 1, 256,
                         DEFAULT_PROP_DIGITAL_GAIN_RANGE_MAX,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
   
    g_object_class_install_property(
        gobject_class, PROP_DENOISING,
        g_param_spec_int("denoising", "Camera control: Denoising", "Denoising factor", 0, 100,
                         DEFAULT_PROP_DENOISING,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_zedxonesrc_reset(GstZedXOneSrc *src) {
    if (src->_zed->isOpened()) {
        src->_zed->close();
    }

    src->_outFramesize = 0;
    src->_isStarted = FALSE;

    if (src->_caps) {
        gst_caps_unref(src->_caps);
        src->_caps = NULL;
    }
}

static void gst_zedxonesrc_init(GstZedXOneSrc *src) {
    /* set source as live (no preroll) */
    gst_base_src_set_live(GST_BASE_SRC(src), TRUE);

    /* override default of BYTES to operate in time mode */
    gst_base_src_set_format(GST_BASE_SRC(src), GST_FORMAT_TIME);

    // ----> Parameters initialization
    src->_cameraResolution = DEFAULT_PROP_CAM_RES;
    src->_cameraFps = DEFAULT_PROP_CAM_FPS;
    src->_sdkVerboseLevel = DEFAULT_PROP_VERBOSE_LVL;
    src->_camTimeout_sec = DEFAULT_PROP_TIMEOUT_SEC;
    src->_cameraId = DEFAULT_PROP_CAM_ID;
    src->_cameraSN = DEFAULT_PROP_CAM_SN;
    src->_opencvCalibrationFile = *g_string_new(DEFAULT_PROP_OPENCV_CALIB_FILE);
    src->_cameraImageFlip = DEFAULT_PROP_CAM_FLIP;
    src->_enableHDR = DEFAULT_PROP_ENABLE_HDR;
    
    src->_saturation = DEFAULT_PROP_SATURATION;
    src->_sharpness = DEFAULT_PROP_SHARPNESS;
    src->_gamma = DEFAULT_PROP_GAMMA;
    src->_autoWb = DEFAULT_PROP_AUTO_WB;
    src->_manualWb = DEFAULT_PROP_WB_TEMP;

    src->_autoExposure = DEFAULT_PROP_AUTO_EXPOSURE;
    src->_exposureRange_min = DEFAULT_PROP_EXPOSURE_RANGE_MIN;
    src->_exposureRange_max = DEFAULT_PROP_EXPOSURE_RANGE_MAX;
    src->_exposure_usec = DEFAULT_PROP_EXPOSURE;
    src->_exposureCompensation = DEFAULT_PROP_EXP_COMPENSATION;

    src->_autoAnalogGain = DEFAULT_PROP_AUTO_ANALOG_GAIN;
    src->_analogGainRange_min = DEFAULT_PROP_ANALOG_GAIN_RANGE_MIN;
    src->_analogGainRange_max = DEFAULT_PROP_ANALOG_GAIN_RANGE_MAX;
    src->_analogGain = DEFAULT_PROP_ANALOG_GAIN;

    src->_autoDigitalGain = DEFAULT_PROP_AUTO_DIGITAL_GAIN;
    src->_digitalGainRange_min = DEFAULT_PROP_DIGITAL_GAIN_RANGE_MIN;
    src->_digitalGainRange_max = DEFAULT_PROP_DIGITAL_GAIN_RANGE_MAX;
    src->_digitalGain = DEFAULT_PROP_DIGITAL_GAIN;

    src->_denoising = DEFAULT_PROP_DENOISING;
    // <---- Parameters initialization

    src->_stopRequested = FALSE;
    src->_caps = NULL;

    if (!src->_zed) {
        src->_zed = std::make_unique<sl::CameraOne>();
    }

    gst_zedxonesrc_reset(src);
}

void gst_zedxonesrc_set_property(GObject *object, guint property_id, const GValue *value,
                                 GParamSpec *pspec) {
    GstZedXOneSrc *src;
    const gchar *str;

    src = GST_ZED_X_ONE_SRC(object);

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_set_property");

    switch (property_id) {
    case PROP_CAM_RES:
        src->_cameraResolution = g_value_get_enum(value);
        break;
    case PROP_CAM_FPS:
        src->_cameraFps = g_value_get_enum(value);
        break;
    case PROP_VERBOSE_LVL:
        src->_sdkVerboseLevel = g_value_get_int(value);
        break;    
    case PROP_TIMEOUT_SEC:
        src->_camTimeout_sec = g_value_get_float(value);
        break;
    case PROP_CAM_ID:
        src->_cameraId = g_value_get_int(value);
        break;
    case PROP_CAM_SN:
        src->_cameraSN = g_value_get_int(value);
        break;
    case PROP_OPENCV_CALIB_FILE:
        str = g_value_get_string(value);
        src->_opencvCalibrationFile = *g_string_new(str);
        break;
    case PROP_IMAGE_FLIP:
        src->_cameraImageFlip = g_value_get_boolean(value);
        break;
    case PROP_ENABLE_HDR:
        src->_enableHDR = g_value_get_boolean(value);
        break;
    case PROP_SATURATION:
        src->_saturation = g_value_get_int(value);
        break;
    case PROP_SHARPNESS:
        src->_sharpness = g_value_get_int(value);
        break;
    case PROP_GAMMA:
        src->_gamma = g_value_get_int(value);
        break;
    case PROP_AUTO_WB:
        src->_autoWb = g_value_get_boolean(value);
        break;
    case PROP_WB_TEMP:
        src->_manualWb = g_value_get_int(value);
        break;    
    case PROP_AUTO_EXPOSURE:
        src->_autoExposure = g_value_get_boolean(value);
        break;
    case PROP_EXPOSURE:
        src->_exposure_usec = g_value_get_int(value);
        break;
    case PROP_EXPOSURE_RANGE_MIN:
        src->_exposureRange_min = g_value_get_int(value);
        break;
    case PROP_EXPOSURE_RANGE_MAX:
        src->_exposureRange_max = g_value_get_int(value);
        break;
    case PROP_EXP_COMPENSATION:
        src->_exposureCompensation = g_value_get_int(value);
        break;
    case PROP_AUTO_ANALOG_GAIN:
        src->_autoAnalogGain = g_value_get_boolean(value);
        break;
    case PROP_ANALOG_GAIN:
        src->_analogGain = g_value_get_int(value);
        break;
    case PROP_ANALOG_GAIN_RANGE_MIN:
        src->_analogGainRange_min = g_value_get_int(value);
        break;
    case PROP_ANALOG_GAIN_RANGE_MAX:
        src->_analogGainRange_max = g_value_get_int(value);
        break;
    case PROP_AUTO_DIGITAL_GAIN:
        src->_autoDigitalGain = g_value_get_boolean(value);
        break;
    case PROP_DIGITAL_GAIN:
        src->_digitalGain = g_value_get_int(value);
        break;
    case PROP_DIGITAL_GAIN_RANGE_MIN:
        src->_digitalGainRange_min = g_value_get_int(value);
        break;
    case PROP_DIGITAL_GAIN_RANGE_MAX:
        src->_digitalGainRange_max = g_value_get_int(value);
        break;    
    case PROP_DENOISING:
        src->_denoising = g_value_get_int(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_zedxonesrc_get_property(GObject *object, guint property_id, GValue *value,
                                 GParamSpec *pspec) {
    GstZedXOneSrc *src;

    g_return_if_fail(GST_IS_ZED_X_ONE_SRC(object));
    src = GST_ZED_X_ONE_SRC(object);

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_get_property");

    switch (property_id) {
    case PROP_CAM_RES:
        g_value_set_enum(value, src->_cameraResolution);
        break;
    case PROP_CAM_FPS:
        g_value_set_enum(value, src->_cameraFps);
        break;
    case PROP_VERBOSE_LVL:
        g_value_set_int(value, src->_sdkVerboseLevel);
        break;
    case PROP_TIMEOUT_SEC:
        g_value_set_float(value, src->_camTimeout_sec);
        break;
    case PROP_CAM_ID:
        g_value_set_int(value, src->_cameraId);
        break;
    case PROP_CAM_SN:
        g_value_set_int(value, src->_cameraSN);
        break;
    case PROP_OPENCV_CALIB_FILE:
        g_value_set_string(value, src->_opencvCalibrationFile.str);
        break;
    case PROP_IMAGE_FLIP:
        g_value_set_boolean(value, src->_cameraImageFlip);
        break;
    case PROP_ENABLE_HDR:
        g_value_set_boolean(value, src->_enableHDR);
        break;
    case PROP_SATURATION:
        g_value_set_int(value, src->_saturation);
        break;
    case PROP_SHARPNESS:
        g_value_set_int(value, src->_sharpness);
        break;
    case PROP_GAMMA:
        g_value_set_int(value, src->_gamma);
        break;
    case PROP_AUTO_WB:
        g_value_set_boolean(value, src->_autoWb);
        break;
    case PROP_WB_TEMP:
        g_value_set_int(value, src->_manualWb);
        break; 
    case PROP_AUTO_EXPOSURE:
        g_value_set_boolean(value, src->_autoExposure);
        break;
    case PROP_EXPOSURE:
        g_value_set_int(value, src->_exposure_usec);
        break;
    case PROP_EXPOSURE_RANGE_MIN:
        g_value_set_int(value, src->_exposureRange_min);
        break;
    case PROP_EXPOSURE_RANGE_MAX:
        g_value_set_int(value, src->_exposureRange_max);
        break;
    case PROP_EXP_COMPENSATION:
        g_value_set_int(value, src->_exposureCompensation);
        break;
    case PROP_AUTO_ANALOG_GAIN:
        g_value_set_boolean(value, src->_autoAnalogGain);
        break;
    case PROP_ANALOG_GAIN:
        g_value_set_int(value, src->_analogGain);
        break;
    case PROP_ANALOG_GAIN_RANGE_MIN:
        g_value_set_int(value, src->_analogGainRange_min);
        break;
    case PROP_ANALOG_GAIN_RANGE_MAX:
        g_value_set_int(value, src->_analogGainRange_max);
        break;
    case PROP_AUTO_DIGITAL_GAIN:
        g_value_set_boolean(value, src->_autoDigitalGain);
        break;
    case PROP_DIGITAL_GAIN:
        g_value_set_int(value, src->_digitalGain);
        break;
    case PROP_DIGITAL_GAIN_RANGE_MIN:
        g_value_set_int(value, src->_digitalGainRange_min);
        break;
    case PROP_DIGITAL_GAIN_RANGE_MAX:
        g_value_set_int(value, src->_digitalGainRange_max);
        break;    
    case PROP_DENOISING:
        g_value_set_int(value, src->_denoising);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_zedxonesrc_dispose(GObject *object) {
    GstZedXOneSrc *src;

    g_return_if_fail(GST_IS_ZED_X_ONE_SRC(object));
    src = GST_ZED_X_ONE_SRC(object);

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_dispose");

    /* clean up as possible.  may be called multiple times */

    G_OBJECT_CLASS(gst_zedxonesrc_parent_class)->dispose(object);
}

void gst_zedxonesrc_finalize(GObject *object) {
    GstZedXOneSrc *src;

    g_return_if_fail(GST_IS_ZED_X_ONE_SRC(object));
    src = GST_ZED_X_ONE_SRC(object);

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_finalize");

    /* clean up object here */
    if (src->_caps) {
        gst_caps_unref(src->_caps);
        src->_caps = NULL;
    }

    G_OBJECT_CLASS(gst_zedxonesrc_parent_class)->finalize(object);
}

static gboolean gst_zedxonesrc_calculate_caps(GstZedXOneSrc *src) {
    GST_TRACE_OBJECT(src, "gst_zedxonesrc_calculate_caps");

    guint32 width, height;
    gint fps;
    GstVideoInfo vinfo;
    GstVideoFormat format = GST_VIDEO_FORMAT_BGRA;

    if (!resol_to_w_h(static_cast<GstZedXOneSrcRes>(src->_cameraResolution), width, height)) {
        return FALSE;
    }

    fps = src->_cameraFps;

    if (format != GST_VIDEO_FORMAT_UNKNOWN) {
        gst_video_info_init(&vinfo);
        gst_video_info_set_format(&vinfo, format, width, height);
        if (src->_caps) {
            gst_caps_unref(src->_caps);
        }
        src->_outFramesize = (guint) GST_VIDEO_INFO_SIZE(&vinfo);
        vinfo.fps_n = fps;
        vinfo.fps_d = 1;
        src->_caps = gst_video_info_to_caps(&vinfo);
    }

    gst_base_src_set_blocksize(GST_BASE_SRC(src), src->_outFramesize);
    gst_base_src_set_caps(GST_BASE_SRC(src), src->_caps);
    GST_DEBUG_OBJECT(src, "Created caps %" GST_PTR_FORMAT, src->_caps);

    return TRUE;
}

static gboolean gst_zedxonesrc_start(GstBaseSrc *bsrc) {
#if (ZED_SDK_MAJOR_VERSION != 4 && ZED_SDK_MINOR_VERSION != 2 && ZED_SDK_SUB_VERSION != 2)
    GST_ELEMENT_ERROR(src, LIBRARY, FAILED, 
    ("Wrong ZED SDK version. SDK v4.2.2 required "),
                      (NULL));
#endif

    GstZedXOneSrc *src = GST_ZED_X_ONE_SRC(bsrc);
    sl::ERROR_CODE ret;

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_calculate_caps");

    // ----> Set init parameters
    sl::InitParametersOne init_params;

    if (src->_cameraId != DEFAULT_PROP_CAM_ID) {
        init_params.input.setFromCameraID(src->_cameraId);

        GST_INFO(" * Input Camera ID: %d", src->_cameraId);
    } else if (src->_cameraSN != DEFAULT_PROP_CAM_SN) {
        init_params.input.setFromSerialNumber(src->_cameraSN);

        GST_INFO(" * Input Camera SN: %ld", src->_cameraSN);
    }

    GST_INFO("CAMERA INITIALIZATION PARAMETERS");    
    init_params.async_grab_camera_recovery = false;
    init_params.camera_resolution = static_cast<sl::RESOLUTION>(src->_cameraResolution);
    GST_INFO(" * Camera resolution: %s", sl::toString(init_params.camera_resolution).c_str());
    init_params.camera_fps = src->_cameraFps;
    GST_INFO(" * Camera FPS: %d", init_params.camera_fps);
    init_params.sdk_verbose = src->_sdkVerboseLevel;
    GST_INFO(" * SDK verbose level: %d", init_params.sdk_verbose);
    init_params.open_timeout_sec = src->_camTimeout_sec;
    GST_INFO(" * Open timeout [sec]: %g", init_params.open_timeout_sec);
    init_params.camera_image_flip = (src->_cameraImageFlip?sl::FLIP_MODE::ON:sl::FLIP_MODE::OFF);
    GST_INFO(" * Camera flipped: %s", (init_params.camera_image_flip?"TRUE":"FALSE"));
    init_params.enable_hdr = src->_cameraImageFlip;
    GST_INFO(" * Enable HDR: %s", (init_params.enable_hdr?"TRUE":"FALSE"));
    sl::String opencv_calibration_file(src->_opencvCalibrationFile.str);
    init_params.optional_opencv_calibration_file = opencv_calibration_file;
    GST_INFO(" * OpenCV calib file: %s", init_params.optional_opencv_calibration_file.c_str());
    // <---- Set init parameters

    // ----> Open camera
    ret = src->_zed->open(init_params);

    if (ret != sl::ERROR_CODE::SUCCESS) {
        GST_ELEMENT_ERROR(src, RESOURCE, NOT_FOUND,
                          ("Failed to open camera, '%s'", sl::toString(ret).c_str()), (NULL));
        return FALSE;
    }
    // <---- Open camera

    // ----> Camera Controls
    // <---- Camera Controls

    if (!gst_zedxonesrc_calculate_caps(src)) {
        return FALSE;
    }

    return TRUE;
}

static gboolean gst_zedxonesrc_stop(GstBaseSrc *bsrc) {
    GstZedXOneSrc *src = GST_ZED_X_ONE_SRC(bsrc);

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_stop");

    gst_zedxonesrc_reset(src);

    return TRUE;
}

static GstCaps *gst_zedxonesrc_get_caps(GstBaseSrc *bsrc, GstCaps *filter) {
    GstZedXOneSrc *src = GST_ZED_X_ONE_SRC(bsrc);
    GstCaps *caps;

    if (src->_caps) {
        caps = gst_caps_copy(src->_caps);
    } else {
        caps = gst_pad_get_pad_template_caps(GST_BASE_SRC_PAD(src));
    }

    GST_DEBUG_OBJECT(src, "The caps before filtering are %" GST_PTR_FORMAT, caps);

    if (filter && caps) {
        GstCaps *tmp = gst_caps_intersect(caps, filter);
        gst_caps_unref(caps);
        caps = tmp;
    }

    GST_DEBUG_OBJECT(src, "The caps after filtering are %" GST_PTR_FORMAT, caps);

    return caps;
}

static gboolean gst_zedxonesrc_set_caps(GstBaseSrc *bsrc, GstCaps *caps) {
    GstZedXOneSrc *src = GST_ZED_X_ONE_SRC(bsrc);
    GST_TRACE_OBJECT(src, "gst_zedxonesrc_set_caps");

    GstVideoInfo vinfo;

    gst_caps_get_structure(caps, 0);

    GST_DEBUG_OBJECT(src, "The caps being set are %" GST_PTR_FORMAT, caps);

    gst_video_info_from_caps(&vinfo, caps);

    if (GST_VIDEO_INFO_FORMAT(&vinfo) == GST_VIDEO_FORMAT_UNKNOWN) {
        goto unsupported_caps;
    }

    return TRUE;

unsupported_caps:
    GST_ERROR_OBJECT(src, "Unsupported caps: %" GST_PTR_FORMAT, caps);
    return FALSE;
}

static gboolean gst_zedxonesrc_unlock(GstBaseSrc *bsrc) {
    GstZedXOneSrc *src = GST_ZED_X_ONE_SRC(bsrc);

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_unlock");

    src->_stopRequested = TRUE;

    return TRUE;
}

static gboolean gst_zedxonesrc_unlock_stop(GstBaseSrc *bsrc) {
    GstZedXOneSrc *src = GST_ZED_X_ONE_SRC(bsrc);

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_unlock_stop");

    src->_stopRequested = FALSE;

    return TRUE;
}

static GstFlowReturn gst_zedxonesrc_fill(GstPushSrc *psrc, GstBuffer *buf) {
    GstZedXOneSrc *src = GST_ZED_X_ONE_SRC(psrc);

    GST_TRACE_OBJECT(src, "gst_zedsrc_fill");

    sl::ERROR_CODE ret;
    GstMapInfo minfo;
    GstClock *clock;
    GstClockTime clock_time;

    static int temp_ugly_buf_index = 0;

    if (!src->_isStarted) {
        src->_acqStartTime = gst_clock_get_time(gst_element_get_clock(GST_ELEMENT(src)));

        src->_isStarted = TRUE;
    }

    // ----> ZED grab
    ret = src->_zed->grab();

    if (ret != sl::ERROR_CODE::SUCCESS) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED,
                          ("Grabbing failed with error: '%s' - %s", sl::toString(ret).c_str(),
                           sl::toVerbose(ret).c_str()),
                          (NULL));
        return GST_FLOW_ERROR;
    }
    // <---- ZED grab

    // ----> Clock update
    clock = gst_element_get_clock(GST_ELEMENT(src));
    clock_time = gst_clock_get_time(clock);
    gst_object_unref(clock);
    // <---- Clock update

    // Memory mapping
    if (FALSE == gst_buffer_map(buf, &minfo, GST_MAP_WRITE)) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("Failed to map buffer for writing"), (NULL));
        return GST_FLOW_ERROR;
    }

    // ZED Mats
    sl::Mat img;

    // ----> Retrieve images
    auto check_ret = [src](sl::ERROR_CODE ret) {
        if (ret != sl::ERROR_CODE::SUCCESS) {
            GST_ELEMENT_ERROR(src, RESOURCE, FAILED,
                            ("Grabbing failed with error: '%s' - %s", sl::toString(ret).c_str(),
                            sl::toVerbose(ret).c_str()),
                            (NULL));
            return false;
        }
        return true;
    };

    ret = src->_zed->retrieveImage(img, sl::VIEW::LEFT, sl::MEM::CPU);
    if(!check_ret(ret)) return GST_FLOW_ERROR;
    // <---- Retrieve images

    // Memory copy
    memcpy(minfo.data, img.getPtr<sl::uchar4>(), minfo.size);

    // ----> Info metadata
    sl::CameraOneInformation cam_info = src->_zed->getCameraInformation();
    ZedInfo info;
    info.cam_model = (gint) cam_info.camera_model;
    info.stream_type = 0; // "Only left image"
    info.grab_single_frame_width = cam_info.camera_configuration.resolution.width;
    info.grab_single_frame_height = cam_info.camera_configuration.resolution.height;
    // <---- Info metadata

    // ----> Sensors metadata
    ZedSensors sens;
    
    sens.sens_avail = TRUE;
    sl::SensorsData sens_data;
    src->_zed->getSensorsData(sens_data, sl::TIME_REFERENCE::IMAGE);

    // IMU
    sens.imu.imu_avail = TRUE;
    sens.imu.acc[0] = sens_data.imu.linear_acceleration.x;
    sens.imu.acc[1] = sens_data.imu.linear_acceleration.y;
    sens.imu.acc[2] = sens_data.imu.linear_acceleration.z;
    sens.imu.gyro[0] = sens_data.imu.angular_velocity.x;
    sens.imu.gyro[1] = sens_data.imu.angular_velocity.y;
    sens.imu.gyro[2] = sens_data.imu.angular_velocity.z;

    // TEMPERATURE
    sens.temp.temp_avail = TRUE;
    float temp;
    sens_data.temperature.get(
    sl::SensorsData::TemperatureData::SENSOR_LOCATION::IMU, temp);
    sens.temp.temp_cam_left = temp;
    sens.temp.temp_cam_right = temp;
    
    sens.mag.mag_avail = FALSE;
    sens.env.env_avail = FALSE;
    // <---- Sensors metadata metadata

    // ----> Positional Tracking metadata
    ZedPose pose;
    pose.pose_avail = FALSE;
    pose.pos_tracking_state = static_cast<int>(sl::POSITIONAL_TRACKING_STATE::OFF);
    pose.pos[0] = 0.0;
    pose.pos[1] = 0.0;
    pose.pos[2] = 0.0;
    pose.orient[0] = 0.0;
    pose.orient[1] = 0.0;
    pose.orient[2] = 0.0;
    // <---- Positional Tracking metadata

    // ----> Timestamp meta-data
    GST_BUFFER_TIMESTAMP(buf) =
        GST_CLOCK_DIFF(gst_element_get_base_time(GST_ELEMENT(src)), clock_time);
    GST_BUFFER_DTS(buf) = GST_BUFFER_TIMESTAMP(buf);
    GST_BUFFER_OFFSET(buf) = temp_ugly_buf_index++;
    // <---- Timestamp meta-data

    guint64 offset = GST_BUFFER_OFFSET(buf);
    GstZedSrcMeta *meta = gst_buffer_add_zed_src_meta(buf, info, pose, sens,
                                                      false,
                                                      0, NULL, offset);

    // Buffer release
    gst_buffer_unmap(buf, &minfo);
    // gst_buffer_unref(buf); // NOTE(Walter) do not uncomment to not crash

    if (src->_stopRequested) {
        return GST_FLOW_FLUSHING;
    }

    return GST_FLOW_OK;
}

static gboolean plugin_init(GstPlugin *plugin) {
    GST_DEBUG_CATEGORY_INIT(gst_zedxonesrc_debug, "zedxonesrc", 0,
                            "debug category for zedxonesrc element");
    gst_element_register(plugin, "zedxonesrc", GST_RANK_NONE, gst_zedxonesrc_get_type());

    return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, zedxonesrc, "Zed X One camera source",
                  plugin_init, GST_PACKAGE_VERSION, GST_PACKAGE_LICENSE, GST_PACKAGE_NAME,
                  GST_PACKAGE_ORIGIN)
