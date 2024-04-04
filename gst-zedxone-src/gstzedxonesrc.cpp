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

#include "gst-zed-meta/gstzedmeta.h"
#include "gstzedxonesrc.h"


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
    PROP_CAM_ID,
    PROP_CAM_SN,
    PROP_BRIGHTNESS,
    PROP_CONTRAST,
    PROP_HUE,
    PROP_SATURATION,
    PROP_SHARPNESS,
    PROP_GAMMA,
    PROP_GAIN,
    PROP_EXPOSURE,
    PROP_AEC_AGC,
    PROP_AEC_AGC_ROI_X,
    PROP_AEC_AGC_ROI_Y,
    PROP_AEC_AGC_ROI_W,
    PROP_AEC_AGC_ROI_H,
    PROP_AEC_AGC_ROI_SIDE,
    PROP_WHITEBALANCE,
    PROP_WHITEBALANCE_AUTO,
    N_PROPERTIES
};

typedef enum {
    GST_ZEDXONESRC_SVGA,    // 960 x 600
    GST_ZEDXONESRC_1080P,   // 1920 x 1080
    GST_ZEDXONESRC_1200P,   // 1920 x 1200
    GST_ZEDXONESRC_4K       // 3856 x 2180
} GstZedXOneSrcRes;

typedef enum {
    GST_ZEDXONESRC_120FPS = 120,
    GST_ZEDXONESRC_60FPS = 60,
    GST_ZEDXONESRC_30FPS = 30,
    GST_ZEDXONESRC_15FPS = 15
} GstZedXOneSrcFPS;

//////////////// DEFAULT PARAMETERS
/////////////////////////////////////////////////////////////////////////////

// INITIALIZATION
#define DEFAULT_PROP_CAM_RES        GST_ZEDXONESRC_1080P
#define DEFAULT_PROP_CAM_FPS        GST_ZEDXONESRC_15FPS
#define DEFAULT_PROP_VERBOSE_LVL    4
#define DEFAULT_PROP_CAM_ID         0
#define DEFAULT_PROP_SWAP_RB        0
//#define DEFAULT_PROP_CAM_SN         0

// CAMERA CONTROLS
#define DEFAULT_PROP_BRIGHTNESS        4
#define DEFAULT_PROP_CONTRAST          4
#define DEFAULT_PROP_HUE               0
#define DEFAULT_PROP_SATURATION        4
#define DEFAULT_PROP_SHARPNESS         4
#define DEFAULT_PROP_GAMMA             8
#define DEFAULT_PROP_GAIN              60
#define DEFAULT_PROP_EXPOSURE          80
#define DEFAULT_PROP_AEG_AGC           1
#define DEFAULT_PROP_AEG_AGC_ROI_X     -1
#define DEFAULT_PROP_AEG_AGC_ROI_Y     -1
#define DEFAULT_PROP_AEG_AGC_ROI_W     -1
#define DEFAULT_PROP_AEG_AGC_ROI_H     -1
#define DEFAULT_PROP_WHITEBALANCE      4600
#define DEFAULT_PROP_WHITEBALANCE_AUTO 1
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define GST_TYPE_ZEDXONE_RESOL (gst_zedxonesrc_resol_get_type())
static GType gst_zedxonesrc_resol_get_type(void) {
    static GType zedxonesrc_resol_type = 0;

    if (!zedxonesrc_resol_type) {
        static GEnumValue pattern_types[] = {
            {static_cast<gint>(GST_ZEDXONESRC_4K), "3856x2180", "4K"},
            {static_cast<gint>(GST_ZEDXONESRC_1200P), "1920x1200", "HD1200"},
            {static_cast<gint>(GST_ZEDXONESRC_1080P), "1920x1080", "HD1080"},
            {static_cast<gint>(GST_ZEDXONESRC_SVGA), "960x600", "SVGA"},
            {0, NULL, NULL},
        };

        zedxonesrc_resol_type = g_enum_register_static("GstZedsrcResolution", pattern_types);
    }

    return zedxonesrc_resol_type;
}

#define GST_TYPE_ZED_FPS (gst_zedxonesrc_fps_get_type())
static GType gst_zedxonesrc_fps_get_type(void) {
    static GType zedxonesrc_fps_type = 0;

    if (!zedxonesrc_fps_type) {
        static GEnumValue pattern_types[] = {
            {GST_ZEDXONESRC_120FPS, "Only with SVGA. Not available with 4K mode", "120 FPS"},
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
    GST_STATIC_PAD_TEMPLATE(
                            "src", GST_PAD_SRC, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS(("video/x-raw, "  // Color HD1080
                                            "format = (string)BGRA, "
                                            "width = (int)1920, "
                                            "height = (int)1080, "
                                            "framerate = (fraction) { 15, 30, 60 }"
                                            ";"
                                            "video/x-raw, "  // Color HD2K
                                            "format = (string)BGRA, "
                                            "width = (int)2208, "
                                            "height = (int)1242, "
                                            "framerate = (fraction) { 15, 30, 60 }"
                                            ";"
                                            "video/x-raw, "  // Color HD1200
                                            "format = (string)BGRA, "
                                            "width = (int)1920, "
                                            "height = (int)1200, "
                                            "framerate = (fraction) { 15, 30, 60 }"
                                            ";"
                                            "video/x-raw, "  // Color SVGA (GMSL2)
                                            "format = (string)BGRA, "
                                            "width = (int)960, "
                                            "height = (int)600, "
                                            "framerate = (fraction) { 15, 30, 60, 120 }"
                                            ";"
                                            "video/x-raw, "  // Color 4K
                                            "format = (string)BGRA, "
                                            "width = (int)3856, "
                                            "height = (int)2180, "
                                            "framerate = (fraction)15"
                                            ";"
                                            "video/x-raw, "  // Color HD1080
                                            "format = (string)RGBA, "
                                            "width = (int)1920, "
                                            "height = (int)1080, "
                                            "framerate = (fraction) { 15, 30, 60 }"
                                            ";"
                                            "video/x-raw, "  // Color HD2K
                                            "format = (string)RGBA, "
                                            "width = (int)2208, "
                                            "height = (int)1242, "
                                            "framerate = (fraction) { 15, 30, 60 }"
                                            ";"
                                            "video/x-raw, "  // Color HD1200
                                            "format = (string)RGBA, "
                                            "width = (int)1920, "
                                            "height = (int)1200, "
                                            "framerate = (fraction) { 15, 30, 60 }"
                                            ";"
                                            "video/x-raw, "  // Color SVGA (GMSL2)
                                            "format = (string)RGBA, "
                                            "width = (int)960, "
                                            "height = (int)600, "
                                            "framerate = (fraction) { 15, 30, 60, 120 }"
                                            ";"
                                            "video/x-raw, "  // Color 4K
                                            "format = (string)RGBA, "
                                            "width = (int)3856, "
                                            "height = (int)2180, "
                                            "framerate = (fraction)15"
                                            ))
                         );

/* Tools */
bool resol_to_w_h(const GstZedXOneSrcRes &resol, guint32 &out_w, guint32 &out_h) {
    switch(resol) {
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

        case GST_ZEDXONESRC_4K:
          out_w = 3856;
          out_h = 2180;
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

    gst_element_class_set_static_metadata(gstelement_class, "ZED X One Camera Source", "Source/Video",
                                          "Stereolabs ZED X One Camera source",
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
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_FPS,
        g_param_spec_enum("camera-fps", "Camera frame rate", "Camera frame rate", GST_TYPE_ZED_FPS,
                          DEFAULT_PROP_CAM_FPS,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_VERBOSE_LVL,
        g_param_spec_int("verbose-level", "Capture Library Verbose", "Capture Library Verbose level", 0, 1000,
                         DEFAULT_PROP_VERBOSE_LVL,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_ID,
        g_param_spec_int("camera-id", "Camera ID", "Select camera from cameraID", 0, 255,
                         DEFAULT_PROP_CAM_ID,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    // g_object_class_install_property(
    //     gobject_class, PROP_CAM_SN,
    //     g_param_spec_int64("camera-sn", "Camera Serial Number",
    //                        "Select camera from camera serial number", 0, G_MAXINT64,
    //                        DEFAULT_PROP_CAM_SN,
    //                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_BRIGHTNESS,
        g_param_spec_int("ctrl-brightness", "Camera control: brightness", "Image brightness", 0, 8,
                         DEFAULT_PROP_BRIGHTNESS,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_CONTRAST,
        g_param_spec_int("ctrl-contrast", "Camera control: contrast", "Image contrast", 0, 8,
                         DEFAULT_PROP_CONTRAST,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_HUE,
        g_param_spec_int("ctrl-hue", "Camera control: hue", "Image hue", 0, 11, DEFAULT_PROP_HUE,
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
        gobject_class, PROP_GAIN,
        g_param_spec_int("ctrl-gain", "Camera control: gain", "Camera gain", 0, 100, DEFAULT_PROP_GAIN,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_EXPOSURE,
        g_param_spec_int("ctrl-exposure", "Camera control: exposure", "Camera exposure", 0, 100,
                         DEFAULT_PROP_EXPOSURE,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_AEC_AGC,
        g_param_spec_boolean("ctrl-aec-agc", "Camera control: automatic gain and exposure",
                             "Camera automatic gain and exposure", DEFAULT_PROP_AEG_AGC,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_AEC_AGC_ROI_X,
        g_param_spec_int("ctrl-aec-agc-roi-x",
                         "Camera control: auto gain/exposure ROI top left 'X' coordinate",
                         "Auto gain/exposure ROI top left 'X' coordinate (-1 to not set ROI)", -1,
                         2208, DEFAULT_PROP_AEG_AGC_ROI_X,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_AEC_AGC_ROI_Y,
        g_param_spec_int("ctrl-aec-agc-roi-y",
                         "Camera control: auto gain/exposure ROI top left 'Y' coordinate",
                         "Auto gain/exposure ROI top left 'Y' coordinate (-1 to not set ROI)", -1,
                         1242, DEFAULT_PROP_AEG_AGC_ROI_Y,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_AEC_AGC_ROI_W,
        g_param_spec_int("ctrl-aec-agc-roi-w", "Camera control: auto gain/exposure ROI width",
                         "Auto gain/exposure ROI width (-1 to not set ROI)", -1, 2208,
                         DEFAULT_PROP_AEG_AGC_ROI_W,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_AEC_AGC_ROI_H,
        g_param_spec_int("ctrl-aec-agc-roi-h", "Camera control: auto gain/exposure ROI height",
                         "Auto gain/exposure ROI height (-1 to not set ROI)", -1, 1242,
                         DEFAULT_PROP_AEG_AGC_ROI_H,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_WHITEBALANCE,
        g_param_spec_int("ctrl-whitebalance-temperature", "Camera control: white balance temperature",
                         "Image white balance temperature", 2800, 6500, DEFAULT_PROP_WHITEBALANCE,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_WHITEBALANCE_AUTO,
        g_param_spec_boolean("ctrl-whitebalance-auto", "Camera control: automatic whitebalance",
                             "Image automatic white balance", DEFAULT_PROP_WHITEBALANCE_AUTO,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_zedxonesrc_reset(GstZedXOneSrc *src) {
    if (src->zed->isOpened()) {
        src->zed->closeCamera();
    }

    src->out_framesize = 0;
    src->is_started = FALSE;

    src->last_frame_count = 0;
    src->total_dropped_frames = 0;

    if (src->caps) {
        gst_caps_unref(src->caps);
        src->caps = NULL;
    }
}

static void gst_zedxonesrc_init(GstZedXOneSrc *src) {
    /* set source as live (no preroll) */
    gst_base_src_set_live(GST_BASE_SRC(src), TRUE);

    /* override default of BYTES to operate in time mode */
    gst_base_src_set_format(GST_BASE_SRC(src), GST_FORMAT_TIME);

    // ----> Parameters initialization
    src->camera_resolution = DEFAULT_PROP_CAM_RES;
    src->camera_fps = DEFAULT_PROP_CAM_FPS;
    src->verbose_level = DEFAULT_PROP_VERBOSE_LVL;
    src->camera_id = DEFAULT_PROP_CAM_ID;
    src->swap_rb = DEFAULT_PROP_SWAP_RB;
    // src->camera_sn = DEFAULT_PROP_CAM_SN;

    src->brightness = DEFAULT_PROP_BRIGHTNESS;
    src->contrast = DEFAULT_PROP_CONTRAST;
    src->hue = DEFAULT_PROP_HUE;
    src->saturation = DEFAULT_PROP_SATURATION;
    src->sharpness = DEFAULT_PROP_SHARPNESS;
    src->gamma = DEFAULT_PROP_GAMMA;
    src->gain = DEFAULT_PROP_GAIN;
    src->exposure = DEFAULT_PROP_EXPOSURE;
    src->aec_agc = DEFAULT_PROP_AEG_AGC;
    src->aec_agc_roi_x = DEFAULT_PROP_AEG_AGC_ROI_X;
    src->aec_agc_roi_y = DEFAULT_PROP_AEG_AGC_ROI_Y;
    src->aec_agc_roi_w = DEFAULT_PROP_AEG_AGC_ROI_W;
    src->aec_agc_roi_h = DEFAULT_PROP_AEG_AGC_ROI_H;
    src->whitebalance_temperature = DEFAULT_PROP_WHITEBALANCE;
    src->whitebalance_temperature_auto = DEFAULT_PROP_WHITEBALANCE_AUTO;
    // <---- Parameters initialization

    src->stop_requested = FALSE;
    src->caps = NULL;

    if(!src->zed) {
        src->zed = std::make_unique<oc::ArgusBayerCapture>();
    }

    gst_zedxonesrc_reset(src);
}

void gst_zedxonesrc_set_property(GObject *object, guint property_id, const GValue *value,
                             GParamSpec *pspec) {
    GstZedXOneSrc *src;

    src = GST_ZED_X_ONE_SRC(object);

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_set_property");

    switch (property_id) {
    case PROP_CAM_RES:
        src->camera_resolution = g_value_get_enum(value);
        break;
    case PROP_CAM_FPS:
        src->camera_fps = g_value_get_enum(value);
        break;
    case PROP_VERBOSE_LVL:
        src->verbose_level = g_value_get_int(value);
        break;
    case PROP_CAM_ID:
        src->camera_id = g_value_get_int(value);
        break;
    // case PROP_CAM_SN:
    //     src->camera_sn = g_value_get_int64(value);
    //     break;
    case PROP_BRIGHTNESS:
        src->brightness = g_value_get_int(value);
        break;
    case PROP_CONTRAST:
        src->contrast = g_value_get_int(value);
        break;
    case PROP_HUE:
        src->hue = g_value_get_int(value);
        break;
    case PROP_SATURATION:
        src->saturation = g_value_get_int(value);
        break;
    case PROP_SHARPNESS:
        src->sharpness = g_value_get_int(value);
        break;
    case PROP_GAMMA:
        src->gamma = g_value_get_int(value);
        break;
    case PROP_GAIN:
        src->gain = g_value_get_int(value);
        break;
    case PROP_EXPOSURE:
        src->exposure = g_value_get_int(value);
        break;
    case PROP_AEC_AGC:
        src->aec_agc = g_value_get_boolean(value);
        break;
    case PROP_AEC_AGC_ROI_X:
        src->aec_agc_roi_x = g_value_get_int(value);
        break;
    case PROP_AEC_AGC_ROI_Y:
        src->aec_agc_roi_y = g_value_get_int(value);
        break;
    case PROP_AEC_AGC_ROI_W:
        src->aec_agc_roi_w = g_value_get_int(value);
        break;
    case PROP_AEC_AGC_ROI_H:
        src->aec_agc_roi_h = g_value_get_int(value);
        break;
    case PROP_AEC_AGC_ROI_SIDE:
        src->aec_agc_roi_side = g_value_get_enum(value);
        break;
    case PROP_WHITEBALANCE:
        src->whitebalance_temperature = g_value_get_int(value);
        break;
    case PROP_WHITEBALANCE_AUTO:
        src->whitebalance_temperature_auto = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_zedxonesrc_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    GstZedXOneSrc *src;

    g_return_if_fail(GST_IS_ZED_X_ONE_SRC(object));
    src = GST_ZED_X_ONE_SRC(object);

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_get_property");

    switch (property_id) {
    case PROP_CAM_RES:
        g_value_set_enum(value, src->camera_resolution);
        break;
    case PROP_CAM_FPS:
        g_value_set_enum(value, src->camera_fps);
        break;
    case PROP_VERBOSE_LVL:
        g_value_set_int(value, src->verbose_level);
        break;
    case PROP_CAM_ID:
        g_value_set_int(value, src->camera_id);
        break;
    // case PROP_CAM_SN:
    //     g_value_set_int64(value, src->camera_sn);
    //     break;
    case PROP_BRIGHTNESS:
        g_value_set_int(value, src->brightness);
        break;
    case PROP_CONTRAST:
        g_value_set_int(value, src->contrast);
        break;
    case PROP_HUE:
        g_value_set_int(value, src->hue);
        break;
    case PROP_SATURATION:
        g_value_set_int(value, src->saturation);
        break;
    case PROP_SHARPNESS:
        g_value_set_int(value, src->sharpness);
        break;
    case PROP_GAMMA:
        g_value_set_int(value, src->gamma);
        break;
    case PROP_GAIN:
        g_value_set_int(value, src->gain);
        break;
    case PROP_EXPOSURE:
        g_value_set_int(value, src->exposure);
        break;
    case PROP_AEC_AGC:
        g_value_set_boolean(value, src->aec_agc);
        break;
    case PROP_AEC_AGC_ROI_X:
        g_value_set_int(value, src->aec_agc_roi_x);
        break;
    case PROP_AEC_AGC_ROI_Y:
        g_value_set_int(value, src->aec_agc_roi_y);
        break;
    case PROP_AEC_AGC_ROI_W:
        g_value_set_int(value, src->aec_agc_roi_w);
        break;
    case PROP_AEC_AGC_ROI_H:
        g_value_set_int(value, src->aec_agc_roi_h);
        break;
    case PROP_AEC_AGC_ROI_SIDE:
        g_value_set_enum(value, src->aec_agc_roi_side);
        break;
    case PROP_WHITEBALANCE:
        g_value_set_int(value, src->whitebalance_temperature);
        break;
    case PROP_WHITEBALANCE_AUTO:
        g_value_set_boolean(value, src->whitebalance_temperature_auto);
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
    if (src->caps) {
        gst_caps_unref(src->caps);
        src->caps = NULL;
    }

    G_OBJECT_CLASS(gst_zedxonesrc_parent_class)->finalize(object);
}

static gboolean gst_zedxonesrc_calculate_caps(GstZedXOneSrc *src) {
    GST_TRACE_OBJECT(src, "gst_zedxonesrc_calculate_caps");

    guint32 width, height;
    gint fps;
    GstVideoInfo vinfo;
    GstVideoFormat format = src->swap_rb?GST_VIDEO_FORMAT_RGBA:GST_VIDEO_FORMAT_BGRA;

    if(!resol_to_w_h(static_cast<GstZedXOneSrcRes>(src->camera_resolution), width, height)) {
      return FALSE;
    }

    fps = src->camera_fps;

    if (format != GST_VIDEO_FORMAT_UNKNOWN) {
        gst_video_info_init(&vinfo);
        gst_video_info_set_format(&vinfo, format, width, height);
        if (src->caps) {
            gst_caps_unref(src->caps);
        }
        src->out_framesize = (guint) GST_VIDEO_INFO_SIZE(&vinfo);
        vinfo.fps_n = fps;
        vinfo.fps_d = 1;
        src->caps = gst_video_info_to_caps(&vinfo);
    }

    gst_base_src_set_blocksize(GST_BASE_SRC(src), src->out_framesize);
    gst_base_src_set_caps(GST_BASE_SRC(src), src->caps);
    GST_DEBUG_OBJECT(src, "Created caps %" GST_PTR_FORMAT, src->caps);

    return TRUE;
}

static gboolean gst_zedxonesrc_start(GstBaseSrc *bsrc) {

    GstZedXOneSrc *src = GST_ZED_X_ONE_SRC(bsrc);

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_calculate_caps");

    // ----> Set config parameters
    guint32 width, height;
    if(!resol_to_w_h(static_cast<GstZedXOneSrcRes>(src->camera_resolution), width, height)) {
      return FALSE;
    }

    int major, minor, patch;
    oc::ArgusVirtualCapture::getVersion(major, minor, patch);
    GST_INFO("ZED Argus Capture Version: %d.%d.%d",major,minor,patch);

    std::vector<oc::ArgusDevice> devs = oc::ArgusBayerCapture::getArgusDevices();
    for (int i = 0; i < devs.size(); i++) {
        GST_INFO("##################");
        GST_INFO(" Device: %d",devs.at(i).id);
        GST_INFO(" Name: %s",devs.at(i).name.c_str());
        GST_INFO(" Badge: %s",devs.at(i).badge.c_str());
        GST_INFO(" Available: %s",(devs.at(i).available?"TRUE":"FALSE"));
    }
    GST_INFO("***********************");


    GST_INFO("CAMERA INITIALIZATION PARAMETERS");

    oc::ArgusCameraConfig config;
    config.mDeviceId = src->camera_id;
    GST_INFO(" * Camera ID: %d", src->camera_id);
    config.mWidth = width;
    config.mHeight = height;
    GST_INFO(" * Camera resolution: %d x %d", (int)width, (int)height);
    config.mFPS = src->camera_fps;
    GST_INFO(" * Camera FPS: %d", src->camera_fps);
    config.mChannel = 4;
    GST_INFO(" * Camera channels: %d", config.mChannel);
    config.mSwapRB = src->swap_rb;
    GST_INFO(" * Swap RB: %s", (src->swap_rb?"TRUE":"FALSE"));
    config.verbose_level = src->verbose_level;
    GST_INFO(" * Verbose level: %d", src->verbose_level);
    // <---- Set config parameters

    // ----> Open camera
    GST_INFO("Camera opening: #%d", (int)config.mDeviceId);

    oc::ARGUS_STATE ret = src->zed->openCamera(config);

    if (ret != oc::ARGUS_STATE::OK) {
        GST_ELEMENT_ERROR(src, RESOURCE, NOT_FOUND,
                          ("Failed to open camera, '%s'", oc::ARGUS_STATE2str(ret).c_str()), (NULL));
        return FALSE;
    }
    GST_INFO("Camera ready");
    GST_INFO(" * %dx%d@%dFPS", src->zed->getWidth(), src->zed->getHeight(),
             src->zed->getFPS());
    // <---- Open camera

    // ----> Camera Controls
    // TODO set camera controls from parameters
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

    if (src->caps) {
        caps = gst_caps_copy(src->caps);
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

    src->stop_requested = TRUE;

    return TRUE;
}

static gboolean gst_zedxonesrc_unlock_stop(GstBaseSrc *bsrc) {
    GstZedXOneSrc *src = GST_ZED_X_ONE_SRC(bsrc);

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_unlock_stop");

    src->stop_requested = FALSE;

    return TRUE;
}

static GstFlowReturn gst_zedxonesrc_fill(GstPushSrc *psrc, GstBuffer *buf) {
    GstZedXOneSrc *src = GST_ZED_X_ONE_SRC(psrc);

    GST_TRACE_OBJECT(src, "gst_zedxonesrc_fill");

    GstMapInfo minfo;
    GstClock *clock;
    GstClockTime clock_time;

    static int temp_ugly_buf_index = 0;

    if (!src->is_started) {
        src->acq_start_time = gst_clock_get_time(gst_element_get_clock(GST_ELEMENT(src)));

        src->is_started = TRUE;
    }

    // ----> Check if a new frame is available
    // TODO(Walter) Add a timeout here!
    int count = 0;
    while (!src->zed->isNewFrame()) {
      ++count;
    }
    // <---- Check if a new frame is available

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

    // ----> Check memory size
    gsize data_size = static_cast<gsize>(src->zed->getWidth() * src->zed->getHeight() *
                    src->zed->getNumberOfChannels());
    if(minfo.size!=data_size) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("ZED X One Data size mismatch!"), (NULL));
        return GST_FLOW_ERROR;
    }
    // <---- Check memory size

    // ----> Memory copy
    memcpy(minfo.data, src->zed->getPixels(), minfo.size);
    // <---- Memory copy

    // // ----> Info metadata
    // sl::CameraInformation cam_info = src->zed->getCameraInformation();
    // ZedInfo info;
    // info.cam_model = (gint) cam_info.camera_model;
    // info.stream_type = src->stream_type;
    // info.grab_single_frame_width = cam_info.camera_configuration.resolution.width;
    // info.grab_single_frame_height = cam_info.camera_configuration.resolution.height;
    // if (info.grab_single_frame_height == 752 || info.grab_single_frame_height == 1440 ||
    //     info.grab_single_frame_height == 2160 || info.grab_single_frame_height == 2484) {
    //     info.grab_single_frame_height /= 2;   // Only half buffer size if the stream is composite
    // }
    // // <---- Info metadata

    // // ----> Sensors metadata
    // ZedSensors sens;
    // if (src->zed->getCameraInformation().camera_model != sl::MODEL::ZED) {
    //     sens.sens_avail = TRUE;
    //     sens.imu.imu_avail = TRUE;

    //     sl::SensorsData sens_data;
    //     src->zed->getSensorsData(sens_data, sl::TIME_REFERENCE::IMAGE);

    //     sens.imu.acc[0] = sens_data.imu.linear_acceleration.x;
    //     sens.imu.acc[1] = sens_data.imu.linear_acceleration.y;
    //     sens.imu.acc[2] = sens_data.imu.linear_acceleration.z;
    //     sens.imu.gyro[0] = sens_data.imu.angular_velocity.x;
    //     sens.imu.gyro[1] = sens_data.imu.angular_velocity.y;
    //     sens.imu.gyro[2] = sens_data.imu.angular_velocity.z;

    //     if (src->zed->getCameraInformation().camera_model != sl::MODEL::ZED_M) {
    //         sens.mag.mag_avail = TRUE;
    //         sens.mag.mag[0] = sens_data.magnetometer.magnetic_field_calibrated.x;
    //         sens.mag.mag[1] = sens_data.magnetometer.magnetic_field_calibrated.y;
    //         sens.mag.mag[2] = sens_data.magnetometer.magnetic_field_calibrated.z;
    //         sens.env.env_avail = TRUE;

    //         float temp;
    //         sens_data.temperature.get(sl::SensorsData::TemperatureData::SENSOR_LOCATION::BAROMETER,
    //                                   temp);
    //         sens.env.temp = temp;
    //         sens.env.press = sens_data.barometer.pressure * 1e-2;

    //         float tempL, tempR;
    //         sens_data.temperature.get(
    //             sl::SensorsData::TemperatureData::SENSOR_LOCATION::ONBOARD_LEFT, tempL);
    //         sens_data.temperature.get(
    //             sl::SensorsData::TemperatureData::SENSOR_LOCATION::ONBOARD_RIGHT, tempR);
    //         sens.temp.temp_avail = TRUE;
    //         sens.temp.temp_cam_left = tempL;
    //         sens.temp.temp_cam_right = tempR;
    //     } else {
    //         sens.mag.mag_avail = FALSE;
    //         sens.env.env_avail = FALSE;
    //         sens.temp.temp_avail = FALSE;
    //     }
    // } else {
    //     sens.sens_avail = FALSE;
    //     sens.imu.imu_avail = FALSE;
    //     sens.mag.mag_avail = FALSE;
    //     sens.env.env_avail = FALSE;
    //     sens.temp.temp_avail = FALSE;
    // }
    // // <---- Sensors metadata metadata

    // ----> Timestamp meta-data
    GST_BUFFER_TIMESTAMP(buf) =
        GST_CLOCK_DIFF(gst_element_get_base_time(GST_ELEMENT(src)), clock_time);
    GST_BUFFER_DTS(buf) = GST_BUFFER_TIMESTAMP(buf);
    GST_BUFFER_OFFSET(buf) = temp_ugly_buf_index++;
    // <---- Timestamp meta-data

    // guint64 offset = GST_BUFFER_OFFSET(buf);
    // GstZedSrcMeta *meta = gst_buffer_add_zed_src_meta(buf, info, pose, sens,
    //                                                   src->object_detection | src->body_tracking,
    //                                                   obj_count, obj_data, offset);

    // Buffer release
    gst_buffer_unmap(buf, &minfo);
    // gst_buffer_unref(buf); // NOTE(Walter) do not uncomment to not crash

    if (src->stop_requested) {
        return GST_FLOW_FLUSHING;
    }

    return GST_FLOW_OK;
}

static gboolean plugin_init(GstPlugin *plugin) {
    GST_DEBUG_CATEGORY_INIT(gst_zedxonesrc_debug, "zedxonesrc", 0, "debug category for zedxonesrc element");
    gst_element_register(plugin, "zedxonesrc", GST_RANK_NONE, gst_zedxonesrc_get_type());

    return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, zedxonesrc, "Zed X One camera source", plugin_init,
                  GST_PACKAGE_VERSION, GST_PACKAGE_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)