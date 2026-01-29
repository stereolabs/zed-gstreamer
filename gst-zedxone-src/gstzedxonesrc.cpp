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

#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
#include <gst/allocators/gstdmabuf.h>
#include <nvbufsurface.h>
#endif

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
static gboolean gst_zedxonesrc_query(GstBaseSrc *src, GstQuery *query);

static GstFlowReturn gst_zedxonesrc_fill(GstPushSrc *src, GstBuffer *buf);

#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
static GstFlowReturn gst_zedxonesrc_create(GstPushSrc *src, GstBuffer **buf);
#endif

enum {
    PROP_0,
    PROP_CAM_RES,
    PROP_CAM_FPS,
    PROP_VERBOSE_LVL,
    PROP_TIMEOUT_SEC,
    PROP_CAM_ID,
    PROP_CAM_SN,
    PROP_SVO_FILE,
    PROP_STREAM_IP,
    PROP_STREAM_PORT,
    PROP_OPENCV_CALIB_FILE,
    PROP_IMAGE_FLIP,
    PROP_ENABLE_HDR,
    PROP_SVO_REAL_TIME,
    PROP_COORD_UNIT,
    PROP_COORD_SYS,
    PROP_SDK_LOG_FILE,
    PROP_SETTINGS_PATH,
    PROP_ASYNC_RECOVERY,
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
    PROP_OUTPUT_RECTIFIED_IMAGE,
    PROP_STREAM_TYPE,
    N_PROPERTIES
};

typedef enum {
    GST_ZEDXONESRC_STREAM_AUTO =
        -1,   // Auto-negotiate: prefer NV12 zero-copy if downstream accepts
    GST_ZEDXONESRC_STREAM_IMAGE = 0,   // Single image (BGRA)
#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
    GST_ZEDXONESRC_RAW_NV12 = 1,   // Zero-copy NV12 raw buffer
#endif
} GstZedXOneSrcStreamType;

typedef enum {
    GST_ZEDXONESRC_SVGA,      // 960 x 600
    GST_ZEDXONESRC_1080P,     // 1920 x 1080
    GST_ZEDXONESRC_1200P,     // 1920 x 1200
    GST_ZEDXONESRC_QHDPLUS,   // 3200x1800
    GST_ZEDXONESRC_4K         // 3840 x 2160
} GstZedXOneSrcRes;

typedef enum {
    GST_ZEDXONESRC_120FPS = 120,
    GST_ZEDXONESRC_60FPS = 60,
    GST_ZEDXONESRC_30FPS = 30,
    GST_ZEDXONESRC_15FPS = 15
} GstZedXOneSrcFPS;

typedef enum {
    GST_ZEDXONESRC_UNIT_MILLIMETER = 0,
    GST_ZEDXONESRC_UNIT_CENTIMETER = 1,
    GST_ZEDXONESRC_UNIT_METER = 2,
    GST_ZEDXONESRC_UNIT_INCH = 3,
    GST_ZEDXONESRC_UNIT_FOOT = 4,
} GstZedXOneSrcUnit;

typedef enum {
    GST_ZEDXONESRC_COORD_IMAGE = 0,
    GST_ZEDXONESRC_COORD_LEFT_HANDED_Y_UP = 1,
    GST_ZEDXONESRC_COORD_RIGHT_HANDED_Y_UP = 2,
    GST_ZEDXONESRC_COORD_RIGHT_HANDED_Z_UP = 3,
    GST_ZEDXONESRC_COORD_LEFT_HANDED_Z_UP = 4,
    GST_ZEDXONESRC_COORD_RIGHT_HANDED_Z_UP_X_FWD = 5
} GstZedXOneSrcCoordSys;

//////////////// DEFAULT PARAMETERS
/////////////////////////////////////////////////////////////////////////////

#define DEFAULT_PROP_CAM_RES GST_ZEDXONESRC_1200P
#define DEFAULT_PROP_CAM_FPS GST_ZEDXONESRC_30FPS
#define DEFAULT_PROP_VERBOSE_LVL 1
#define DEFAULT_PROP_TIMEOUT_SEC 5.0f
#define DEFAULT_PROP_CAM_ID -1
#define DEFAULT_PROP_CAM_SN 0
#define DEFAULT_PROP_SVO_FILE ""
#define DEFAULT_PROP_STREAM_IP ""
#define DEFAULT_PROP_STREAM_PORT 30000
#define DEFAULT_PROP_CAM_FLIP FALSE
#define DEFAULT_PROP_ENABLE_HDR FALSE
#define DEFAULT_PROP_SVO_REAL_TIME FALSE
#define DEFAULT_PROP_COORD_UNIT GST_ZEDXONESRC_UNIT_MILLIMETER
#define DEFAULT_PROP_COORD_SYS GST_ZEDXONESRC_COORD_IMAGE
#define DEFAULT_PROP_SDK_LOG_FILE ""
#define DEFAULT_PROP_SETTINGS_PATH ""
#define DEFAULT_PROP_ASYNC_RECOVERY FALSE
#define DEFAULT_PROP_OPENCV_CALIB_FILE ""
#define DEFAULT_PROP_SATURATION 4
#define DEFAULT_PROP_SHARPNESS 1
#define DEFAULT_PROP_GAMMA 2
#define DEFAULT_PROP_AUTO_WB TRUE
#define DEFAULT_PROP_WB_TEMP 4200
#define DEFAULT_PROP_AUTO_EXPOSURE TRUE
#define DEFAULT_PROP_EXPOSURE 10000
#define DEFAULT_PROP_EXPOSURE_RANGE_MIN 1024
#define DEFAULT_PROP_EXPOSURE_RANGE_MAX 66666
#define DEFAULT_PROP_EXP_COMPENSATION 50
#define DEFAULT_PROP_AUTO_ANALOG_GAIN TRUE
#define DEFAULT_PROP_ANALOG_GAIN 30000
#define DEFAULT_PROP_ANALOG_GAIN_RANGE_MIN 1000
#define DEFAULT_PROP_ANALOG_GAIN_RANGE_MAX 30000
#define DEFAULT_PROP_AUTO_DIGITAL_GAIN TRUE
#define DEFAULT_PROP_DIGITAL_GAIN 3
#define DEFAULT_PROP_DIGITAL_GAIN_RANGE_MIN 1
#define DEFAULT_PROP_DIGITAL_GAIN_RANGE_MAX 256
#define DEFAULT_PROP_DENOISING 50
#define DEFAULT_PROP_OUTPUT_RECTIFIED_IMAGE TRUE
#define DEFAULT_PROP_STREAM_TYPE GST_ZEDXONESRC_STREAM_AUTO
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define GST_TYPE_ZEDXONE_STREAM_TYPE (gst_zedxonesrc_stream_type_get_type())
static GType gst_zedxonesrc_stream_type_get_type(void) {
    static GType zedxonesrc_stream_type_type = 0;

    if (!zedxonesrc_stream_type_type) {
        static GEnumValue pattern_types[] = {
            {GST_ZEDXONESRC_STREAM_AUTO,
             "Auto-negotiate format based on downstream (prefer NV12 zero-copy)",
             "Auto [prefer NV12 zero-copy]"},
            {GST_ZEDXONESRC_STREAM_IMAGE, "8 bits- 4 channels image", "Image [BGRA]"},
#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
            {GST_ZEDXONESRC_RAW_NV12, "Zero-copy NV12 raw buffer", "Raw NV12 zero-copy [NV12]"},
#endif
            {0, NULL, NULL},
        };

        zedxonesrc_stream_type_type =
            g_enum_register_static("GstZedXOneSrcStreamType", pattern_types);
    }

    return zedxonesrc_stream_type_type;
}

#define GST_TYPE_ZEDXONE_RESOL (gst_zedxonesrc_resol_get_type())
static GType gst_zedxonesrc_resol_get_type(void) {
    static GType zedxonesrc_resol_type = 0;

    if (!zedxonesrc_resol_type) {
        static GEnumValue pattern_types[] = {
            {static_cast<gint>(GST_ZEDXONESRC_4K), "3840x2160 (only ZED X One 4K)", "4K"},
            {static_cast<gint>(GST_ZEDXONESRC_QHDPLUS), "3200x1800 (only ZED X One 4K)", "QHDPLUS"},
            {static_cast<gint>(GST_ZEDXONESRC_1200P), "1920x1200", "HD1200"},
            {static_cast<gint>(GST_ZEDXONESRC_1080P), "1920x1080", "HD1080"},
            {static_cast<gint>(GST_ZEDXONESRC_SVGA), "960x600 (only ZED X One GS)", "SVGA"},
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
            {GST_ZEDXONESRC_120FPS, "Only with SVGA. Not with ZED X One 4K", "120 FPS"},
            {GST_ZEDXONESRC_60FPS, "Not available with 4K mode", "60  FPS"},
            {GST_ZEDXONESRC_30FPS, "Available with all the resolutions", "30  FPS"},
            {GST_ZEDXONESRC_15FPS, "Available with all the resolutions", "15  FPS"},
            {0, NULL, NULL},
        };

        zedxonesrc_fps_type = g_enum_register_static("GstZedXOneSrcFPS", pattern_types);
    }

    return zedxonesrc_fps_type;
}

#define GST_TYPE_ZEDXONE_UNIT (gst_zedxonesrc_unit_get_type())
static GType gst_zedxonesrc_unit_get_type(void) {
    static GType zedxonesrc_unit_type = 0;

    if (!zedxonesrc_unit_type) {
        static GEnumValue pattern_types[] = {
            {static_cast<gint>(GST_ZEDXONESRC_UNIT_MILLIMETER), "Millimeter", "MILLIMETER"},
            {static_cast<gint>(GST_ZEDXONESRC_UNIT_CENTIMETER), "Centimeter", "CENTIMETER"},
            {static_cast<gint>(GST_ZEDXONESRC_UNIT_METER), "Meter", "METER"},
            {static_cast<gint>(GST_ZEDXONESRC_UNIT_INCH), "Inch", "INCH"},
            {static_cast<gint>(GST_ZEDXONESRC_UNIT_FOOT), "Foot", "FOOT"},
            {0, NULL, NULL},
        };

        zedxonesrc_unit_type = g_enum_register_static("GstZedXOneSrcUnit", pattern_types);
    }

    return zedxonesrc_unit_type;
}

#define GST_TYPE_ZEDXONE_COORD_SYS (gst_zedxonesrc_coord_sys_get_type())
static GType gst_zedxonesrc_coord_sys_get_type(void) {
    static GType zedxonesrc_coord_sys_type = 0;

    if (!zedxonesrc_coord_sys_type) {
        static GEnumValue pattern_types[] = {
            {static_cast<gint>(GST_ZEDXONESRC_COORD_IMAGE),
             "Standard image (0,0) at top left corner", "IMAGE"},
            {static_cast<gint>(GST_ZEDXONESRC_COORD_LEFT_HANDED_Y_UP),
             "Left handed, Y up and Z forward", "LEFT_HANDED_Y_UP"},
            {static_cast<gint>(GST_ZEDXONESRC_COORD_RIGHT_HANDED_Y_UP),
             "Right handed, Y up and Z backward", "RIGHT_HANDED_Y_UP"},
            {static_cast<gint>(GST_ZEDXONESRC_COORD_RIGHT_HANDED_Z_UP),
             "Right handed, Z up and Y forward", "RIGHT_HANDED_Z_UP"},
            {static_cast<gint>(GST_ZEDXONESRC_COORD_LEFT_HANDED_Z_UP),
             "Left handed, Z up and Y forward", "LEFT_HANDED_Z_UP"},
            {static_cast<gint>(GST_ZEDXONESRC_COORD_RIGHT_HANDED_Z_UP_X_FWD),
             "Right handed, Z up and X forward", "RIGHT_HANDED_Z_UP_X_FWD"},
            {0, NULL, NULL},
        };

        zedxonesrc_coord_sys_type = g_enum_register_static("GstZedXOneSrcCoordSys", pattern_types);
    }

    return zedxonesrc_coord_sys_type;
}

/* pad templates */
static GstStaticPadTemplate gst_zedxonesrc_src_template = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS(("video/x-raw, "   // Color 4K
                     "format = (string)BGRA, "
                     "width = (int)3840, "
                     "height = (int)2160, "
                     "framerate = (fraction) { 15, 30 }"
                     ";"
                     "video/x-raw, "   // Color QHDPLUS
                     "format = (string)BGRA, "
                     "width = (int)3200, "
                     "height = (int)1800, "
                     "framerate = (fraction) { 15, 30 }"
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
#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
                     ";"
                     "video/x-raw(memory:NVMM), "   // NV12 4K (zero-copy)
                     "format = (string)NV12, "
                     "width = (int)3840, "
                     "height = (int)2160, "
                     "framerate = (fraction) { 15, 30 }"
                     ";"
                     "video/x-raw(memory:NVMM), "   // NV12 QHDPLUS (zero-copy)
                     "format = (string)NV12, "
                     "width = (int)3200, "
                     "height = (int)1800, "
                     "framerate = (fraction) { 15, 30 }"
                     ";"
                     "video/x-raw(memory:NVMM), "   // NV12 HD1200 (zero-copy)
                     "format = (string)NV12, "
                     "width = (int)1920, "
                     "height = (int)1200, "
                     "framerate = (fraction) { 15, 30, 60 }"
                     ";"
                     "video/x-raw(memory:NVMM), "   // NV12 HD1080 (zero-copy)
                     "format = (string)NV12, "
                     "width = (int)1920, "
                     "height = (int)1080, "
                     "framerate = (fraction) { 15, 30, 60 }"
                     ";"
                     "video/x-raw(memory:NVMM), "   // NV12 SVGA (zero-copy)
                     "format = (string)NV12, "
                     "width = (int)960, "
                     "height = (int)600, "
                     "framerate = (fraction) { 15, 30, 60, 120 }"
#endif
                     )));

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
        out_w = 3200;
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

    gst_element_class_set_static_metadata(
        gstelement_class, "ZED X One Camera Source GS/4K", "Source/Video",
        "Stereolabs ZED X One GS/4K Camera source", "Stereolabs <support@stereolabs.com>");

    gstbasesrc_class->start = GST_DEBUG_FUNCPTR(gst_zedxonesrc_start);
    gstbasesrc_class->stop = GST_DEBUG_FUNCPTR(gst_zedxonesrc_stop);
    gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR(gst_zedxonesrc_get_caps);
    gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR(gst_zedxonesrc_set_caps);
    gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR(gst_zedxonesrc_unlock);
    gstbasesrc_class->unlock_stop = GST_DEBUG_FUNCPTR(gst_zedxonesrc_unlock_stop);
    gstbasesrc_class->query = GST_DEBUG_FUNCPTR(gst_zedxonesrc_query);

    gstpushsrc_class->fill = GST_DEBUG_FUNCPTR(gst_zedxonesrc_fill);
#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
    gstpushsrc_class->create = GST_DEBUG_FUNCPTR(gst_zedxonesrc_create);
#endif

    /* Install GObject properties */
    g_object_class_install_property(
        gobject_class, PROP_CAM_RES,
        g_param_spec_enum("camera-resolution", "Camera Resolution", "Camera Resolution",
                          GST_TYPE_ZEDXONE_RESOL, DEFAULT_PROP_CAM_RES,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_FPS,
        g_param_spec_enum("camera-fps", "Camera frame rate", "Camera frame rate", GST_TYPE_ZED_FPS,
                          DEFAULT_PROP_CAM_FPS,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_STREAM_TYPE,
        g_param_spec_enum("stream-type", "Image stream type", "Image stream type",
                          GST_TYPE_ZEDXONE_STREAM_TYPE, DEFAULT_PROP_STREAM_TYPE,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_VERBOSE_LVL,
        g_param_spec_int("verbose-level", "ZED SDK Verbose level", "ZED SDK Verbose level", 0, 999,
                         DEFAULT_PROP_VERBOSE_LVL,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_TIMEOUT_SEC,
        g_param_spec_float("camera-timeout", "Open Timeout [sec]",
                           "Connection opening timeout in seconds", 0.5f, 86400.f,
                           DEFAULT_PROP_TIMEOUT_SEC,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_ID,
        g_param_spec_int("camera-id", "Camera ID", "Select camera from cameraID", -1, 255,
                         DEFAULT_PROP_CAM_ID,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_SN,
        g_param_spec_int64("camera-sn", "Camera Serial Number",
                           "Select camera from the serial number", 0, 999999999,
                           DEFAULT_PROP_CAM_SN,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_SVO_FILE,
        g_param_spec_string("svo-file-path", "SVO file", "Input from SVO file",
                            DEFAULT_PROP_SVO_FILE,
                            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_STREAM_IP,
        g_param_spec_string("input-stream-ip", "Input Stream IP",
                            "Specify IP address when using streaming input", DEFAULT_PROP_STREAM_IP,
                            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_STREAM_PORT,
        g_param_spec_int("input-stream-port", "Input Stream Port",
                         "Specify port when using streaming input", 1, G_MAXUINT16,
                         DEFAULT_PROP_STREAM_PORT,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OPENCV_CALIB_FILE,
        g_param_spec_string("opencv-calibration-file", "Optional OpenCV Calibration File",
                            "Optional OpenCV Calibration File", DEFAULT_PROP_OPENCV_CALIB_FILE,
                            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_IMAGE_FLIP,
        g_param_spec_boolean("camera-flip", "Camera flip status",
                             "Mirror image vertically if camera is flipped", DEFAULT_PROP_CAM_FLIP,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ENABLE_HDR,
        g_param_spec_boolean(
            "enable-hdr", "HDR status", "Enable HDR if supported by resolution and frame rate.",
            DEFAULT_PROP_ENABLE_HDR, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_SVO_REAL_TIME,
        g_param_spec_boolean("svo-real-time-mode", "SVO Real Time Mode", "SVO Real Time Mode",
                             DEFAULT_PROP_SVO_REAL_TIME,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_COORD_UNIT,
        g_param_spec_enum("coordinate-units", "SDK Coordinate Units", "SDK Coordinate Units",
                          GST_TYPE_ZEDXONE_UNIT, DEFAULT_PROP_COORD_UNIT,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_COORD_SYS,
        g_param_spec_enum("coordinate-system", "SDK Coordinate System", "SDK Coordinate System",
                          GST_TYPE_ZEDXONE_COORD_SYS, DEFAULT_PROP_COORD_SYS,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_SDK_LOG_FILE,
        g_param_spec_string("sdk-verbose-log-file", "SDK Verbose Log File", "SDK Verbose Log File",
                            DEFAULT_PROP_SDK_LOG_FILE,
                            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_SETTINGS_PATH,
        g_param_spec_string("optional-settings-path", "Optional Settings Path",
                            "Optional Settings Path", DEFAULT_PROP_SETTINGS_PATH,
                            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ASYNC_RECOVERY,
        g_param_spec_boolean("async-grab-camera-recovery", "Async Grab Camera Recovery",
                             "Async Grab Camera Recovery", DEFAULT_PROP_ASYNC_RECOVERY,
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
        g_param_spec_int("ctrl-gamma", "Camera control: gamma", "Image gamma", 1, 9,
                         DEFAULT_PROP_GAMMA,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_AUTO_WB,
        g_param_spec_boolean("ctrl-whitebalance-auto", "Camera control: automatic whitebalance",
                             "Image automatic white balance", DEFAULT_PROP_AUTO_WB,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_WB_TEMP,
        g_param_spec_int("ctrl-whitebalance-temperature",
                         "Camera control: white balance temperature",
                         "Image white balance temperature", 2800, 6500, DEFAULT_PROP_WB_TEMP,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_AUTO_EXPOSURE,
        g_param_spec_boolean("ctrl-auto-exposure", "Camera control: Automatic Exposure",
                             "Enable Automatic Exposure", DEFAULT_PROP_AUTO_EXPOSURE,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_EXPOSURE,
        g_param_spec_int("ctrl-exposure-time", "Camera control: Exposure time [µsec]",
                         "Exposure time in microseconds", DEFAULT_PROP_EXPOSURE_RANGE_MIN,
                         DEFAULT_PROP_EXPOSURE_RANGE_MAX, DEFAULT_PROP_EXPOSURE,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_EXPOSURE_RANGE_MIN,
        g_param_spec_int("ctrl-auto-exposure-range-min",
                         "Camera control: Minimum Exposure time [µsec]",
                         "Minimum exposure time in microseconds for the automatic exposure setting",
                         DEFAULT_PROP_EXPOSURE_RANGE_MIN, DEFAULT_PROP_EXPOSURE_RANGE_MAX,
                         DEFAULT_PROP_EXPOSURE_RANGE_MIN,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_EXPOSURE_RANGE_MAX,
        g_param_spec_int("ctrl-auto-exposure-range-max",
                         "Camera control: Maximum Exposure time [µsec]",
                         "Maximum exposure time in microseconds for the automatic exposure setting",
                         DEFAULT_PROP_EXPOSURE_RANGE_MIN, DEFAULT_PROP_EXPOSURE_RANGE_MAX,
                         DEFAULT_PROP_EXPOSURE_RANGE_MAX,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_EXP_COMPENSATION,
        g_param_spec_int("ctrl-exposure-compensation", "Camera control: Exposure Compensation",
                         "Exposure Compensation", 0, 100, DEFAULT_PROP_EXP_COMPENSATION,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_AUTO_ANALOG_GAIN,
        g_param_spec_boolean("ctrl-auto-analog-gain", "Camera control: Automatic Analog Gain",
                             "Enable Automatic Analog Gain", DEFAULT_PROP_AUTO_ANALOG_GAIN,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ANALOG_GAIN,
        g_param_spec_int("ctrl-analog-gain", "Analog Gain", "Camera control: Analog Gain value",
                         DEFAULT_PROP_ANALOG_GAIN_RANGE_MIN, DEFAULT_PROP_ANALOG_GAIN_RANGE_MAX,
                         DEFAULT_PROP_ANALOG_GAIN,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ANALOG_GAIN_RANGE_MIN,
        g_param_spec_int("ctrl-auto-analog-gain-range-min",
                         "Camera control: Minimum Automatic Analog Gain",
                         "Minimum Analog Gain for the automatic analog gain setting",
                         DEFAULT_PROP_ANALOG_GAIN_RANGE_MIN, DEFAULT_PROP_ANALOG_GAIN_RANGE_MAX,
                         DEFAULT_PROP_ANALOG_GAIN_RANGE_MIN,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ANALOG_GAIN_RANGE_MAX,
        g_param_spec_int("ctrl-auto-analog-gain-range-max",
                         "Camera control: Maximum Automatic Analog Gain",
                         "Maximum Analog Gain for the automatic analog gain setting",
                         DEFAULT_PROP_ANALOG_GAIN_RANGE_MIN, DEFAULT_PROP_ANALOG_GAIN_RANGE_MAX,
                         DEFAULT_PROP_ANALOG_GAIN_RANGE_MAX,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_AUTO_DIGITAL_GAIN,
        g_param_spec_boolean("ctrl-auto-digital-gain", "Camera control: Automatic Digital Gain",
                             "Enable Automatic Digital Gain", DEFAULT_PROP_AUTO_DIGITAL_GAIN,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_DIGITAL_GAIN,
        g_param_spec_int("ctrl-digital-gain", "Camera control: Digital Gain", "Digital Gain value",
                         DEFAULT_PROP_DIGITAL_GAIN_RANGE_MIN, DEFAULT_PROP_DIGITAL_GAIN_RANGE_MAX,
                         DEFAULT_PROP_DIGITAL_GAIN,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_DIGITAL_GAIN_RANGE_MIN,
        g_param_spec_int("ctrl-auto-digital-gain-range-min",
                         "Camera control: Minimum Automatic Digital Gain",
                         "Minimum Digital Gain for the automatic digital gain setting",
                         DEFAULT_PROP_DIGITAL_GAIN_RANGE_MIN, DEFAULT_PROP_DIGITAL_GAIN_RANGE_MAX,
                         DEFAULT_PROP_DIGITAL_GAIN_RANGE_MIN,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_DIGITAL_GAIN_RANGE_MAX,
        g_param_spec_int("ctrl-auto-digital-gain-range-max",
                         "Camera control: Maximum Automatic Digital Gain",
                         "Maximum Digital Gain for the automatic digital gain setting",
                         DEFAULT_PROP_DIGITAL_GAIN_RANGE_MIN, DEFAULT_PROP_DIGITAL_GAIN_RANGE_MAX,
                         DEFAULT_PROP_DIGITAL_GAIN_RANGE_MAX,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_DENOISING,
        g_param_spec_int("ctrl-denoising", "Camera control: Denoising", "Denoising factor", 0, 100,
                         DEFAULT_PROP_DENOISING,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OUTPUT_RECTIFIED_IMAGE,
        g_param_spec_boolean(
            "output-rectified-image", "Output rectified image",
            "Enable image rectification (disable for custom optics without calibration)",
            DEFAULT_PROP_OUTPUT_RECTIFIED_IMAGE,
            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_zedxonesrc_reset(GstZedXOneSrc *src) {
    if (src->_zed->isOpened()) {
        src->_zed->close();
    }

    src->_outFramesize = 0;
    src->_isStarted = FALSE;
    src->_bufferIndex = 0;

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
    src->_svoFile = g_string_new(DEFAULT_PROP_SVO_FILE);
    src->_streamIp = g_string_new(DEFAULT_PROP_STREAM_IP);
    src->_streamPort = DEFAULT_PROP_STREAM_PORT;
    src->_opencvCalibrationFile = g_string_new(DEFAULT_PROP_OPENCV_CALIB_FILE);
    src->_cameraImageFlip = DEFAULT_PROP_CAM_FLIP;
    src->_enableHDR = DEFAULT_PROP_ENABLE_HDR;
    src->_svoRealTime = DEFAULT_PROP_SVO_REAL_TIME;
    src->_coordUnit = DEFAULT_PROP_COORD_UNIT;
    src->_coordSys = DEFAULT_PROP_COORD_SYS;
    src->_sdkLogFile = g_string_new(DEFAULT_PROP_SDK_LOG_FILE);
    src->_settingsPath = g_string_new(DEFAULT_PROP_SETTINGS_PATH);
    src->_asyncRecovery = DEFAULT_PROP_ASYNC_RECOVERY;

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
    src->_outputRectifiedImage = DEFAULT_PROP_OUTPUT_RECTIFIED_IMAGE;
    src->_streamType = DEFAULT_PROP_STREAM_TYPE;
    src->_resolvedStreamType = -1;   // Not resolved yet
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
        src->_cameraSN = g_value_get_int64(value);
        break;
    case PROP_SVO_FILE:
        str = g_value_get_string(value);
        g_string_assign(src->_svoFile, str);
        break;
    case PROP_STREAM_IP:
        str = g_value_get_string(value);
        g_string_assign(src->_streamIp, str);
        break;
    case PROP_STREAM_PORT:
        src->_streamPort = g_value_get_int(value);
        break;
    case PROP_OPENCV_CALIB_FILE:
        str = g_value_get_string(value);
        g_string_assign(src->_opencvCalibrationFile, str);
        break;
    case PROP_IMAGE_FLIP:
        src->_cameraImageFlip = g_value_get_boolean(value);
        break;
    case PROP_ENABLE_HDR:
        src->_enableHDR = g_value_get_boolean(value);
        break;
    case PROP_SVO_REAL_TIME:
        src->_svoRealTime = g_value_get_boolean(value);
        break;
    case PROP_COORD_UNIT:
        src->_coordUnit = g_value_get_enum(value);
        break;
    case PROP_COORD_SYS:
        src->_coordSys = g_value_get_enum(value);
        break;
    case PROP_SDK_LOG_FILE:
        str = g_value_get_string(value);
        g_string_assign(src->_sdkLogFile, str);
        break;
    case PROP_SETTINGS_PATH:
        str = g_value_get_string(value);
        g_string_assign(src->_settingsPath, str);
        break;
    case PROP_ASYNC_RECOVERY:
        src->_asyncRecovery = g_value_get_boolean(value);
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
    case PROP_OUTPUT_RECTIFIED_IMAGE:
        src->_outputRectifiedImage = g_value_get_boolean(value);
        break;
    case PROP_STREAM_TYPE:
        src->_streamType = g_value_get_enum(value);
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
        g_value_set_int64(value, src->_cameraSN);
        break;
    case PROP_SVO_FILE:
        g_value_set_string(value, src->_svoFile->str);
        break;
    case PROP_STREAM_IP:
        g_value_set_string(value, src->_streamIp->str);
        break;
    case PROP_STREAM_PORT:
        g_value_set_int(value, src->_streamPort);
        break;
    case PROP_OPENCV_CALIB_FILE:
        g_value_set_string(value, src->_opencvCalibrationFile->str);
        break;
    case PROP_IMAGE_FLIP:
        g_value_set_boolean(value, src->_cameraImageFlip);
        break;
    case PROP_ENABLE_HDR:
        g_value_set_boolean(value, src->_enableHDR);
        break;
    case PROP_SVO_REAL_TIME:
        g_value_set_boolean(value, src->_svoRealTime);
        break;
    case PROP_COORD_UNIT:
        g_value_set_enum(value, src->_coordUnit);
        break;
    case PROP_COORD_SYS:
        g_value_set_enum(value, src->_coordSys);
        break;
    case PROP_SDK_LOG_FILE:
        g_value_set_string(value, src->_sdkLogFile->str);
        break;
    case PROP_SETTINGS_PATH:
        g_value_set_string(value, src->_settingsPath->str);
        break;
    case PROP_ASYNC_RECOVERY:
        g_value_set_boolean(value, src->_asyncRecovery);
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
    case PROP_OUTPUT_RECTIFIED_IMAGE:
        g_value_set_boolean(value, src->_outputRectifiedImage);
        break;
    case PROP_STREAM_TYPE:
        g_value_set_enum(value, src->_streamType);
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

    if (src->_svoFile) {
        g_string_free(src->_svoFile, TRUE);
    }
    if (src->_streamIp) {
        g_string_free(src->_streamIp, TRUE);
    }
    if (src->_opencvCalibrationFile) {
        g_string_free(src->_opencvCalibrationFile, TRUE);
    }
    if (src->_sdkLogFile) {
        g_string_free(src->_sdkLogFile, TRUE);
    }
    if (src->_settingsPath) {
        g_string_free(src->_settingsPath, TRUE);
    }

    G_OBJECT_CLASS(gst_zedxonesrc_parent_class)->finalize(object);
}

#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
/* Helper function to check if downstream accepts NV12 NVMM caps */
static gboolean gst_zedxonesrc_downstream_accepts_nv12_nvmm(GstZedXOneSrc *src) {
    GstPad *srcpad = GST_BASE_SRC_PAD(src);
    GstCaps *peer_caps = gst_pad_peer_query_caps(srcpad, NULL);
    gboolean accepts_nv12_nvmm = FALSE;

    if (peer_caps) {
        GST_DEBUG_OBJECT(src, "Peer caps for auto-negotiation: %" GST_PTR_FORMAT, peer_caps);

        // Check if any structure has memory:NVMM feature and NV12 format
        for (guint i = 0; i < gst_caps_get_size(peer_caps); i++) {
            GstCapsFeatures *features = gst_caps_get_features(peer_caps, i);
            GstStructure *structure = gst_caps_get_structure(peer_caps, i);

            if (features && gst_caps_features_contains(features, "memory:NVMM")) {
                const gchar *format = gst_structure_get_string(structure, "format");
                if (format && g_strcmp0(format, "NV12") == 0) {
                    accepts_nv12_nvmm = TRUE;
                    GST_INFO_OBJECT(src, "Downstream accepts NV12 in NVMM memory");
                    break;
                }
                // Also check if format is not specified (accepts any)
                if (!format) {
                    accepts_nv12_nvmm = TRUE;
                    GST_INFO_OBJECT(src, "Downstream accepts NVMM memory (format unspecified)");
                    break;
                }
            }
        }
        gst_caps_unref(peer_caps);
    } else {
        GST_DEBUG_OBJECT(src, "No peer caps available for auto-negotiation");
    }

    return accepts_nv12_nvmm;
}
#endif

/* Resolve stream-type=AUTO to actual stream type based on downstream caps and SDK capability */
static void gst_zedxonesrc_resolve_stream_type(GstZedXOneSrc *src) {
    // If user explicitly set a stream type (not AUTO), use that
    if (src->_streamType != GST_ZEDXONESRC_STREAM_AUTO) {
        src->_resolvedStreamType = src->_streamType;
        GST_DEBUG_OBJECT(src, "Using explicit stream-type=%d", src->_resolvedStreamType);
        return;
    }

    // AUTO mode: prefer NV12 zero-copy if available
    GST_INFO_OBJECT(src, "stream-type=AUTO, checking downstream capabilities...");

#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
    // Check if downstream accepts NV12 NVMM
    if (gst_zedxonesrc_downstream_accepts_nv12_nvmm(src)) {
        src->_resolvedStreamType = GST_ZEDXONESRC_RAW_NV12;
        GST_INFO_OBJECT(src, "Auto-negotiated to NV12 zero-copy (stream-type=%d)",
                        src->_resolvedStreamType);
        return;
    } else {
        GST_INFO_OBJECT(src, "Downstream does not accept NV12 NVMM, falling back to BGRA");
    }
#else
    GST_INFO_OBJECT(src, "NV12 zero-copy not available (SDK < 5.2), using BGRA");
#endif

    // Default fallback: Image in BGRA
    src->_resolvedStreamType = GST_ZEDXONESRC_STREAM_IMAGE;
    GST_INFO_OBJECT(src, "Auto-negotiated to BGRA Image (stream-type=%d)",
                    src->_resolvedStreamType);
}

static gboolean gst_zedxonesrc_calculate_caps(GstZedXOneSrc *src) {
    GST_TRACE_OBJECT(src, "gst_zedxonesrc_calculate_caps");

    // Use resolved stream type which accounts for AUTO negotiation
    gint stream_type = src->_resolvedStreamType;

    guint32 width, height;
    gint fps;
    GstVideoInfo vinfo;
    GstVideoFormat format = GST_VIDEO_FORMAT_BGRA;

#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
    if (stream_type == GST_ZEDXONESRC_RAW_NV12) {
        format = GST_VIDEO_FORMAT_NV12;
    }
#endif

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

#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
        // Add memory:NVMM feature for zero-copy NV12 mode
        if (stream_type == GST_ZEDXONESRC_RAW_NV12) {
            GstCapsFeatures *features = gst_caps_features_new("memory:NVMM", NULL);
            gst_caps_set_features(src->_caps, 0, features);
            GST_INFO_OBJECT(src, "Added memory:NVMM feature for zero-copy");
        }
#endif
    }

    gst_base_src_set_blocksize(GST_BASE_SRC(src), src->_outFramesize);
    gst_base_src_set_caps(GST_BASE_SRC(src), src->_caps);
    GST_DEBUG_OBJECT(src, "Created caps %" GST_PTR_FORMAT, src->_caps);

    return TRUE;
}

static gboolean gst_zedxonesrc_start(GstBaseSrc *bsrc) {
    GstZedXOneSrc *src = GST_ZED_X_ONE_SRC(bsrc);

#if (ZED_SDK_MAJOR_VERSION < 5)
    GST_ELEMENT_ERROR(src, LIBRARY, FAILED, ("Wrong ZED SDK version. SDK v5.0 or newer required"),
                      (NULL));
    return FALSE;
#endif
    sl::ERROR_CODE ret;

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_calculate_caps");

    // ----> Set init parameters
    sl::InitParametersOne init_params;

    GST_INFO("CAMERA INITIALIZATION PARAMETERS");

    if (src->_svoFile->len != 0) {
        init_params.input.setFromSVOFile(sl::String(src->_svoFile->str));
        init_params.svo_real_time_mode = src->_svoRealTime;
        GST_INFO(" * Input SVO file: %s", src->_svoFile->str);
        GST_INFO(" * SVO real time mode: %s", (init_params.svo_real_time_mode ? "TRUE" : "FALSE"));
    } else if (src->_cameraSN != 0) {
        init_params.input.setFromSerialNumber(static_cast<unsigned int>(src->_cameraSN));
        GST_INFO(" * Input Serial Number: %u", static_cast<unsigned int>(src->_cameraSN));
    } else if (src->_cameraId != -1) {
        init_params.input.setFromCameraID(src->_cameraId);
        GST_INFO(" * Input Camera ID: %d", src->_cameraId);
    } else if (src->_streamIp->len != 0) {
        init_params.input.setFromStream(sl::String(src->_streamIp->str), src->_streamPort);
        GST_INFO(" * Input Stream: %s:%d", src->_streamIp->str, src->_streamPort);
    }

    switch (src->_cameraResolution) {
    case GST_ZEDXONESRC_SVGA:
        init_params.camera_resolution = sl::RESOLUTION::SVGA;
        break;
    case GST_ZEDXONESRC_1080P:
        init_params.camera_resolution = sl::RESOLUTION::HD1080;
        break;
    case GST_ZEDXONESRC_1200P:
        init_params.camera_resolution = sl::RESOLUTION::HD1200;
        break;
    case GST_ZEDXONESRC_QHDPLUS:
        init_params.camera_resolution = sl::RESOLUTION::QHDPLUS;
        break;
    case GST_ZEDXONESRC_4K:
        init_params.camera_resolution = sl::RESOLUTION::HD4K;
        break;
    default:
        GST_ELEMENT_ERROR(src, RESOURCE, NOT_FOUND, ("Failed to set camera resolution"), (NULL));
        return FALSE;
    }

    GST_INFO(" * Camera resolution: %s", sl::toString(init_params.camera_resolution).c_str());
    init_params.camera_fps = src->_cameraFps;
    GST_INFO(" * Camera FPS: %d", init_params.camera_fps);

    init_params.sdk_verbose = src->_sdkVerboseLevel;
    GST_INFO(" * SDK verbose level: %d", init_params.sdk_verbose);

    init_params.sdk_verbose_log_file = sl::String(src->_sdkLogFile->str);
    GST_INFO(" * SDK verbose log file: %s", init_params.sdk_verbose_log_file.c_str());

    init_params.camera_image_flip =
        (src->_cameraImageFlip ? sl::FLIP_MODE::ON : sl::FLIP_MODE::OFF);
    GST_INFO(" * Camera flipped: %s", (init_params.camera_image_flip ? "TRUE" : "FALSE"));

    init_params.enable_hdr = src->_enableHDR;
    GST_INFO(" * Enable HDR: %s", (init_params.enable_hdr ? "TRUE" : "FALSE"));

    sl::String opencv_calibration_file(src->_opencvCalibrationFile->str);
    init_params.optional_opencv_calibration_file = opencv_calibration_file;
    GST_INFO(" * OpenCV calib file: %s", init_params.optional_opencv_calibration_file.c_str());

    init_params.coordinate_units = static_cast<sl::UNIT>(src->_coordUnit);
    GST_INFO(" * Coordinate units: %s", sl::toString(init_params.coordinate_units).c_str());

    init_params.coordinate_system = static_cast<sl::COORDINATE_SYSTEM>(src->_coordSys);
    GST_INFO(" * Coordinate system: %s", sl::toString(init_params.coordinate_system).c_str());

    init_params.optional_settings_path = sl::String(src->_settingsPath->str);
    GST_INFO(" * Optional settings path: %s", init_params.optional_settings_path.c_str());

    init_params.async_grab_camera_recovery = src->_asyncRecovery;
    GST_INFO(" * Async grab camera recovery: %s",
             (init_params.async_grab_camera_recovery ? "TRUE" : "FALSE"));
    // <---- Set init parameters

    // ----> Open camera
    ret = src->_zed->open(init_params);

    if (ret > sl::ERROR_CODE::SUCCESS) {
        GST_ELEMENT_ERROR(src, RESOURCE, NOT_FOUND,
                          ("Failed to open camera, '%s'", sl::toString(ret).c_str()), (NULL));
        return FALSE;
    }
    // <---- Open camera

    // Check FPS
    src->_realFps = static_cast<int>(src->_zed->getCameraInformation().camera_configuration.fps);
    if (src->_realFps != src->_cameraFps) {
        GST_WARNING("Camera FPS set to %d, but real FPS is %d", src->_cameraFps, src->_realFps);
    }

    // Lambda to check return values
    auto check_ret = [src](sl::ERROR_CODE err) {
        if (err != sl::ERROR_CODE::SUCCESS) {
            GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("Failed, '%s'", sl::toString(err).c_str()),
                              (NULL));
            return false;
        }
        return true;
    };

    // ----> Get default camera control values for debug
    GST_INFO("CAMERA CONTROLS");
    int value;
    ret = src->_zed->getCameraSettings(sl::VIDEO_SETTINGS::SATURATION, value);
    if (!check_ret(ret))
        return FALSE;
    GST_DEBUG(" * Default Saturation: %d", value);
    ret = src->_zed->getCameraSettings(sl::VIDEO_SETTINGS::SHARPNESS, value);
    if (!check_ret(ret))
        return FALSE;
    GST_DEBUG(" * Default Sharpness: %d", value);
    ret = src->_zed->getCameraSettings(sl::VIDEO_SETTINGS::GAMMA, value);
    if (!check_ret(ret))
        return FALSE;
    GST_DEBUG(" * Default Gamma: %d", value);
    ret = src->_zed->getCameraSettings(sl::VIDEO_SETTINGS::WHITEBALANCE_AUTO, value);
    if (!check_ret(ret))
        return FALSE;
    GST_DEBUG(" * Default White balance auto: %s", (value ? "TRUE" : "FALSE"));
    ret = src->_zed->getCameraSettings(sl::VIDEO_SETTINGS::WHITEBALANCE_TEMPERATURE, value);
    if (!check_ret(ret))
        return FALSE;
    GST_DEBUG(" * Default White balance temperature: %d", value);

    int val_min, val_max;
    ret = src->_zed->getCameraSettings(sl::VIDEO_SETTINGS::EXPOSURE_TIME, value);
    if (!check_ret(ret))
        return FALSE;
    GST_DEBUG(" * Default Exposure time: %d", value);
    ret = src->_zed->getCameraSettings(sl::VIDEO_SETTINGS::AUTO_EXPOSURE_TIME_RANGE, val_min,
                                       val_max);
    if (!check_ret(ret))
        return FALSE;
    GST_DEBUG(" * Default Auto Exposure range: [%d,%d]", val_min, val_max);
    ret = src->_zed->getCameraSettings(sl::VIDEO_SETTINGS::EXPOSURE_COMPENSATION, value);
    if (!check_ret(ret))
        return FALSE;
    GST_DEBUG(" * Default Exposure compensation: %d", value);

    ret = src->_zed->getCameraSettings(sl::VIDEO_SETTINGS::ANALOG_GAIN, value);
    if (!check_ret(ret))
        return FALSE;
    GST_DEBUG(" * Default Analog Gain: %d", value);
    ret =
        src->_zed->getCameraSettings(sl::VIDEO_SETTINGS::AUTO_ANALOG_GAIN_RANGE, val_min, val_max);
    if (!check_ret(ret))
        return FALSE;
    GST_DEBUG(" * Default Auto Analog Gain range: [%d,%d]", val_min, val_max);

    ret = src->_zed->getCameraSettings(sl::VIDEO_SETTINGS::DIGITAL_GAIN, value);
    if (!check_ret(ret))
        return FALSE;
    GST_DEBUG(" * Default Digital Gain: %d", value);
    ret =
        src->_zed->getCameraSettings(sl::VIDEO_SETTINGS::AUTO_DIGITAL_GAIN_RANGE, val_min, val_max);
    if (!check_ret(ret))
        return FALSE;
    GST_DEBUG(" * Default Auto Digital Gain range: [%d,%d]", val_min, val_max);

    ret = src->_zed->getCameraSettings(sl::VIDEO_SETTINGS::DENOISING, value);
    if (!check_ret(ret))
        return FALSE;
    GST_DEBUG(" * Default Denoising: %d", value);
    // <---- Get default camera control values for debug

    // ----> Camera Controls
    GST_INFO("CAMERA CONTROLS");

    ret = src->_zed->setCameraSettings(sl::VIDEO_SETTINGS::SATURATION, src->_saturation);
    if (!check_ret(ret))
        return FALSE;
    GST_INFO(" * Saturation: %d", src->_saturation);
    ret = src->_zed->setCameraSettings(sl::VIDEO_SETTINGS::SHARPNESS, src->_sharpness);
    if (!check_ret(ret))
        return FALSE;
    GST_INFO(" * Sharpness: %d", src->_sharpness);
    ret = src->_zed->setCameraSettings(sl::VIDEO_SETTINGS::GAMMA, src->_gamma);
    if (!check_ret(ret))
        return FALSE;
    GST_INFO(" * Gamma: %d", src->_gamma);
    ret = src->_zed->setCameraSettings(sl::VIDEO_SETTINGS::WHITEBALANCE_AUTO, src->_autoWb);
    if (!check_ret(ret))
        return FALSE;
    GST_INFO(" * White balance auto: %s", (src->_autoWb ? "TRUE" : "FALSE"));
    if (!src->_autoWb) {
        ret = src->_zed->setCameraSettings(sl::VIDEO_SETTINGS::WHITEBALANCE_TEMPERATURE,
                                           src->_manualWb);
        if (!check_ret(ret))
            return FALSE;
        GST_INFO(" * White balance temperature: %d", src->_manualWb);
    }

    if (src->_autoExposure) {
        GST_INFO(" * Auto Exposure: TRUE");
    } else {
        GST_INFO(" * Auto Exposure: FALSE");
        ret = src->_zed->setCameraSettings(sl::VIDEO_SETTINGS::EXPOSURE_TIME, src->_exposure_usec);
        if (!check_ret(ret))
            return FALSE;
        GST_INFO(" * Exposure time: %d", src->_exposure_usec);
        // Force Exposure range values
        src->_exposureRange_min = src->_exposure_usec;
        src->_exposureRange_max = src->_exposure_usec;
    }
    src->_exposureRange_max = std::min(src->_exposureRange_max, 1000000 / src->_realFps);
    ret = src->_zed->setCameraSettings(sl::VIDEO_SETTINGS::AUTO_EXPOSURE_TIME_RANGE,
                                       src->_exposureRange_min, src->_exposureRange_max);
    if (!check_ret(ret))
        return FALSE;
    GST_INFO(" * Auto Exposure range: [%d,%d]", src->_exposureRange_min, src->_exposureRange_max);
    ret = src->_zed->setCameraSettings(sl::VIDEO_SETTINGS::EXPOSURE_COMPENSATION,
                                       src->_exposureCompensation);
    if (!check_ret(ret))
        return FALSE;
    GST_INFO(" * Exposure compensation: %d", src->_exposureCompensation);

    if (src->_autoAnalogGain) {
        GST_INFO(" * Auto Analog Gain: TRUE");
    } else {
        GST_INFO(" * Auto Analog Gain: FALSE");
        ret = src->_zed->setCameraSettings(sl::VIDEO_SETTINGS::ANALOG_GAIN, src->_analogGain);
        if (!check_ret(ret))
            return FALSE;
        GST_INFO(" * Analog Gain: %d", src->_analogGain);
        // Force Exposure range values
        src->_analogGainRange_min = src->_analogGain;
        src->_analogGainRange_max = src->_analogGain;
    }
    ret = src->_zed->setCameraSettings(sl::VIDEO_SETTINGS::AUTO_ANALOG_GAIN_RANGE,
                                       src->_analogGainRange_min, src->_analogGainRange_max);
    if (!check_ret(ret))
        return FALSE;
    GST_INFO(" * Auto Analog Gain range: [%d,%d]", src->_analogGainRange_min,
             src->_analogGainRange_max);

    if (src->_autoDigitalGain) {
        GST_INFO(" * Auto Digital Gain: TRUE");
    } else {
        GST_INFO(" * Auto Digital Gain: FALSE");
        ret = src->_zed->setCameraSettings(sl::VIDEO_SETTINGS::DIGITAL_GAIN, src->_digitalGain);
        if (!check_ret(ret))
            return FALSE;
        GST_INFO(" * Digital Gain: %d", src->_digitalGain);
        // Force Exposure range values
        src->_digitalGainRange_min = src->_digitalGain;
        src->_digitalGainRange_max = src->_digitalGain;
    }
    ret = src->_zed->setCameraSettings(sl::VIDEO_SETTINGS::AUTO_DIGITAL_GAIN_RANGE,
                                       src->_digitalGainRange_min, src->_digitalGainRange_max);
    if (!check_ret(ret))
        return FALSE;
    GST_INFO(" * Auto Digital Gain range: [%d,%d]", src->_digitalGainRange_min,
             src->_digitalGainRange_max);

    ret = src->_zed->setCameraSettings(sl::VIDEO_SETTINGS::DENOISING, src->_denoising);
    if (!check_ret(ret))
        return FALSE;
    GST_INFO(" * Denoising: %d", src->_denoising);
    // <---- Camera Controls

    // Resolve stream type (AUTO -> actual type based on downstream caps)
    gst_zedxonesrc_resolve_stream_type(src);

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

static gboolean gst_zedxonesrc_query(GstBaseSrc *bsrc, GstQuery *query) {
    GstZedXOneSrc *src = GST_ZED_X_ONE_SRC(bsrc);
    gboolean res = FALSE;

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_LATENCY: {
        GstClockTime min_latency, max_latency;

        if (!src->_isStarted || src->_cameraFps == 0) {
            GST_DEBUG_OBJECT(src, "Latency query before started, returning FALSE");
            return FALSE;
        }

        // Latency is one frame duration
        min_latency = gst_util_uint64_scale_int(GST_SECOND, 1, src->_cameraFps);
        max_latency = min_latency;

        GST_DEBUG_OBJECT(src, "Reporting latency: min %" GST_TIME_FORMAT ", max %" GST_TIME_FORMAT,
                         GST_TIME_ARGS(min_latency), GST_TIME_ARGS(max_latency));

        gst_query_set_latency(query, TRUE, min_latency, max_latency);
        res = TRUE;
        break;
    }
    default:
        res = GST_BASE_SRC_CLASS(gst_zedxonesrc_parent_class)->query(bsrc, query);
        break;
    }

    return res;
}

#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
/* Callback to release the RawBuffer when GstBuffer is unreffed */
static void raw_buffer_destroy_notify(gpointer data) {
    sl::RawBuffer *raw_buffer = static_cast<sl::RawBuffer *>(data);
    if (raw_buffer) {
        delete raw_buffer;
    }
}

/* Create function for zero-copy NV12 mode */
static GstFlowReturn gst_zedxonesrc_create(GstPushSrc *psrc, GstBuffer **outbuf) {
    GstZedXOneSrc *src = GST_ZED_X_ONE_SRC(psrc);

    // Use resolved stream type which accounts for AUTO negotiation
    gint stream_type = src->_resolvedStreamType;

    // For non-NVMM modes, fall back to the default fill() path
    if (stream_type != GST_ZEDXONESRC_RAW_NV12) {
        return GST_PUSH_SRC_CLASS(gst_zedxonesrc_parent_class)->create(psrc, outbuf);
    }

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_create (NVMM zero-copy)");

    sl::ERROR_CODE ret;
    GstClock *clock = nullptr;
    GstClockTime clock_time = GST_CLOCK_TIME_NONE;

    // Acquisition start time
    if (!src->_isStarted) {
        GstClock *start_clock = gst_element_get_clock(GST_ELEMENT(src));
        if (start_clock) {
            src->_acqStartTime = gst_clock_get_time(start_clock);
            gst_object_unref(start_clock);
        }
        src->_isStarted = TRUE;
    }

    // Grab frame
    ret = src->_zed->grab();
    if (ret == sl::ERROR_CODE::END_OF_SVOFILE_REACHED) {
        GST_INFO_OBJECT(src, "End of SVO file");
        return GST_FLOW_EOS;
    } else if (ret != sl::ERROR_CODE::SUCCESS) {
        GST_ERROR_OBJECT(src, "grab() failed: %s", sl::toString(ret).c_str());
        return GST_FLOW_ERROR;
    }

    // Get clock for timestamp
    clock = gst_element_get_clock(GST_ELEMENT(src));
    if (clock) {
        clock_time = gst_clock_get_time(clock);
        gst_object_unref(clock);
    }

    // Retrieve RawBuffer - allocate on heap for GstBuffer lifecycle
    sl::RawBuffer *raw_buffer = new sl::RawBuffer();
    ret = src->_zed->retrieveImage(*raw_buffer);
    if (ret != sl::ERROR_CODE::SUCCESS) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED,
                          ("Failed to retrieve RawBuffer: '%s'", sl::toString(ret).c_str()),
                          (NULL));
        delete raw_buffer;
        return GST_FLOW_ERROR;
    }

    // Get NvBufSurface
    NvBufSurface *nvbuf = static_cast<NvBufSurface *>(raw_buffer->getRawBuffer());
    if (!nvbuf) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("RawBuffer returned null NvBufSurface"), (NULL));
        delete raw_buffer;
        return GST_FLOW_ERROR;
    }

    if (nvbuf->numFilled == 0) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("NvBufSurface has no filled surfaces"), (NULL));
        delete raw_buffer;
        return GST_FLOW_ERROR;
    }

    // Log buffer info
    NvBufSurfaceParams *params = &nvbuf->surfaceList[0];
    GST_DEBUG_OBJECT(src, "NvBufSurface: %p, FD: %ld, size: %d, memType: %d, format: %d", nvbuf,
                     params->bufferDesc, params->dataSize, nvbuf->memType, params->colorFormat);

    // Create GstBuffer wrapping the NvBufSurface pointer directly
    GstBuffer *buf =
        gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, nvbuf, sizeof(NvBufSurface), 0,
                                    sizeof(NvBufSurface), raw_buffer, raw_buffer_destroy_notify);

    // ----> Info metadata
    sl::CameraOneInformation cam_info = src->_zed->getCameraInformation();
    ZedInfo info;
    info.cam_model = (gint) cam_info.camera_model;
    info.stream_type = src->_resolvedStreamType;
    info.grab_single_frame_width = cam_info.camera_configuration.resolution.width;
    info.grab_single_frame_height = cam_info.camera_configuration.resolution.height;

    // ----> Sensors metadata
    ZedSensors sens;
    sens.sens_avail = TRUE;
    sl::SensorsData sens_data;
    src->_zed->getSensorsData(sens_data, sl::TIME_REFERENCE::IMAGE);

    sens.imu.imu_avail = TRUE;
    sens.imu.acc[0] = sens_data.imu.linear_acceleration.x;
    sens.imu.acc[1] = sens_data.imu.linear_acceleration.y;
    sens.imu.acc[2] = sens_data.imu.linear_acceleration.z;
    sens.imu.gyro[0] = sens_data.imu.angular_velocity.x;
    sens.imu.gyro[1] = sens_data.imu.angular_velocity.y;
    sens.imu.gyro[2] = sens_data.imu.angular_velocity.z;

    float temp;
    sens_data.temperature.get(sl::SensorsData::TemperatureData::SENSOR_LOCATION::IMU, temp);
    sens.temp.temp_avail = TRUE;
    sens.temp.temp_cam_left = temp;
    sens.temp.temp_cam_right = temp;

    sens.mag.mag_avail = FALSE;
    sens.env.env_avail = FALSE;

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

    // ----> Timestamp meta-data
    GST_BUFFER_TIMESTAMP(buf) =
        GST_CLOCK_DIFF(gst_element_get_base_time(GST_ELEMENT(src)), clock_time);
    GST_BUFFER_DTS(buf) = GST_BUFFER_TIMESTAMP(buf);
    GST_BUFFER_OFFSET(buf) = src->_bufferIndex++;

    guint64 offset = GST_BUFFER_OFFSET(buf);
    gst_buffer_add_zed_src_meta(buf, info, pose, sens, false, 0, NULL, offset);

    if (src->_stopRequested) {
        gst_buffer_unref(buf);
        return GST_FLOW_FLUSHING;
    }

    *outbuf = buf;
    return GST_FLOW_OK;
}
#endif

static GstFlowReturn gst_zedxonesrc_fill(GstPushSrc *psrc, GstBuffer *buf) {
    GstZedXOneSrc *src = GST_ZED_X_ONE_SRC(psrc);

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_fill");

    sl::ERROR_CODE ret;
    GstMapInfo minfo;
    GstClock *clock;
    GstClockTime clock_time;

    if (!src->_isStarted) {
        src->_acqStartTime = gst_clock_get_time(gst_element_get_clock(GST_ELEMENT(src)));

        src->_isStarted = TRUE;
    }

    // ----> ZED grab
    GST_TRACE(" Data Grabbing");
    ret = src->_zed->grab();

    if (ret > sl::ERROR_CODE::SUCCESS) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED,
                          ("Grabbing failed with error: '%s' - %s", sl::toString(ret).c_str(),
                           sl::toVerbose(ret).c_str()),
                          (NULL));
        return GST_FLOW_ERROR;
    }
    // <---- ZED grab

    // ----> Clock update
    GST_TRACE("Clock update");
    clock = gst_element_get_clock(GST_ELEMENT(src));
    clock_time = gst_clock_get_time(clock);
    gst_object_unref(clock);
    // <---- Clock update

    // Memory mapping
    GST_TRACE("Memory mapping");
    if (FALSE == gst_buffer_map(buf, &minfo, GST_MAP_WRITE)) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("Failed to map buffer for writing"), (NULL));
        return GST_FLOW_ERROR;
    }

    // ZED Mats
    sl::Mat img;

    // ----> Retrieve images
    GST_TRACE("Retrieve images");
    auto check_ret = [src](sl::ERROR_CODE err) {
        if (err != sl::ERROR_CODE::SUCCESS) {
            GST_ELEMENT_ERROR(src, RESOURCE, FAILED,
                              ("Grabbing failed with error: '%s' - %s", sl::toString(err).c_str(),
                               sl::toVerbose(err).c_str()),
                              (NULL));
            return false;
        }
        return true;
    };

    const sl::VIEW view_type =
        src->_outputRectifiedImage ? sl::VIEW::LEFT : sl::VIEW::LEFT_UNRECTIFIED;
    ret = src->_zed->retrieveImage(img, view_type, sl::MEM::CPU);
    if (!check_ret(ret))
        return GST_FLOW_ERROR;
    // <---- Retrieve images

    // Memory copy
    GST_TRACE("Memory copy");
    memcpy(minfo.data, img.getPtr<sl::uchar4>(), minfo.size);

    // ----> Info metadata
    GST_TRACE("Info metadata");
    sl::CameraOneInformation cam_info = src->_zed->getCameraInformation();
    ZedInfo info;
    info.cam_model = (gint) cam_info.camera_model;
    info.stream_type = src->_resolvedStreamType;   // Use resolved type for metadata
    info.grab_single_frame_width = cam_info.camera_configuration.resolution.width;
    info.grab_single_frame_height = cam_info.camera_configuration.resolution.height;
    // <---- Info metadata

    // ----> Sensors metadata
    GST_TRACE("Sensors metadata");
    ZedSensors sens;

    sens.sens_avail = TRUE;
    sl::SensorsData sens_data;
    src->_zed->getSensorsData(sens_data, sl::TIME_REFERENCE::IMAGE);

    // IMU
    GST_TRACE("IMU");
    sens.imu.imu_avail = TRUE;
    sens.imu.acc[0] = sens_data.imu.linear_acceleration.x;
    sens.imu.acc[1] = sens_data.imu.linear_acceleration.y;
    sens.imu.acc[2] = sens_data.imu.linear_acceleration.z;
    sens.imu.gyro[0] = sens_data.imu.angular_velocity.x;
    sens.imu.gyro[1] = sens_data.imu.angular_velocity.y;
    sens.imu.gyro[2] = sens_data.imu.angular_velocity.z;

    // TEMPERATURE
    GST_TRACE("TEMPERATURE");
    sens.temp.temp_avail = TRUE;
    float temp;
    sens_data.temperature.get(sl::SensorsData::TemperatureData::SENSOR_LOCATION::IMU, temp);
    sens.temp.temp_cam_left = temp;
    sens.temp.temp_cam_right = temp;

    sens.mag.mag_avail = FALSE;
    sens.env.env_avail = FALSE;
    // <---- Sensors metadata metadata

    // ----> Positional Tracking metadata
    GST_TRACE("Positional Tracking metadata");
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
    GST_TRACE("Timestamp meta-data");
    GST_BUFFER_TIMESTAMP(buf) =
        GST_CLOCK_DIFF(gst_element_get_base_time(GST_ELEMENT(src)), clock_time);
    GST_BUFFER_DTS(buf) = GST_BUFFER_TIMESTAMP(buf);
    GST_BUFFER_OFFSET(buf) = src->_bufferIndex++;
    // <---- Timestamp meta-data

    GST_TRACE("PUSH Buffer meta-data");
    guint64 offset = GST_BUFFER_OFFSET(buf);
    GstZedSrcMeta *meta =
        gst_buffer_add_zed_src_meta(buf, info, pose, sens, false, 0, NULL, offset);

    // Buffer release
    GST_TRACE("Buffer release");
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
