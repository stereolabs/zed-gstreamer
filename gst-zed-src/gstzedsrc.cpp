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

#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
#include <gst/allocators/gstdmabuf.h>
#include <nvbufsurface.h>
#endif

#include "gst-zed-meta/gstzedmeta.h"
#include "gstzedsrc.h"

// AI Module
#define OD_INSTANCE_MODULE_ID 0
#define BT_INSTANCE_MODULE_ID 1

GST_DEBUG_CATEGORY_STATIC(gst_zedsrc_debug);
#define GST_CAT_DEFAULT gst_zedsrc_debug

// Additional debug categories for subsystems
GST_DEBUG_CATEGORY_STATIC(gst_zedsrc_tracking_debug);
GST_DEBUG_CATEGORY_STATIC(gst_zedsrc_od_debug);
GST_DEBUG_CATEGORY_STATIC(gst_zedsrc_controls_debug);

// Magic number constants
#define GST_ZEDSRC_MAX_OBJECTS 256

/* prototypes */
static void gst_zedsrc_set_property(GObject *object, guint property_id, const GValue *value,
                                    GParamSpec *pspec);
static void gst_zedsrc_get_property(GObject *object, guint property_id, GValue *value,
                                    GParamSpec *pspec);
static void gst_zedsrc_dispose(GObject *object);
static void gst_zedsrc_finalize(GObject *object);

static gboolean gst_zedsrc_start(GstBaseSrc *src);
static gboolean gst_zedsrc_stop(GstBaseSrc *src);
static GstCaps *gst_zedsrc_get_caps(GstBaseSrc *src, GstCaps *filter);
static gboolean gst_zedsrc_set_caps(GstBaseSrc *src, GstCaps *caps);
static gboolean gst_zedsrc_unlock(GstBaseSrc *src);
static gboolean gst_zedsrc_unlock_stop(GstBaseSrc *src);

static GstFlowReturn gst_zedsrc_fill(GstPushSrc *src, GstBuffer *buf);

#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
static GstFlowReturn gst_zedsrc_create(GstPushSrc *src, GstBuffer **buf);
#endif

static gboolean gst_zedsrc_query(GstBaseSrc *src, GstQuery *query);

enum {
    PROP_0,
    PROP_CAM_RES,
    PROP_CAM_FPS,
    PROP_STREAM_TYPE,
    PROP_SDK_VERBOSE,
    PROP_CAM_FLIP,
    PROP_CAM_ID,
    PROP_CAM_SN,
    PROP_SVO_FILE,
    PROP_OPENCV_CALIB_FILE,
    PROP_STREAM_IP,
    PROP_STREAM_PORT,
    PROP_DEPTH_MIN,
    PROP_DEPTH_MAX,
    PROP_DEPTH_MODE,
    PROP_DIS_SELF_CALIB,
    PROP_ROI,
    PROP_ROI_X,
    PROP_ROI_Y,
    PROP_ROI_W,
    PROP_ROI_H,
    PROP_DEPTH_STAB,
    PROP_CONFIDENCE_THRESH,
    PROP_TEXTURE_CONF_THRESH,
    PROP_3D_REF_FRAME,
    PROP_FILL_MODE,
    PROP_POS_TRACKING,
    PROP_CAMERA_STATIC,
    PROP_POS_AREA_FILE_PATH,
    PROP_POS_ENABLE_AREA_MEMORY,
    PROP_POS_ENABLE_IMU_FUSION,
    PROP_POS_ENABLE_POSE_SMOOTHING,
    PROP_POS_SET_FLOOR_AS_ORIGIN,
    PROP_POS_SET_GRAVITY_AS_ORIGIN,
    PROP_POS_DEPTH_MIN_RANGE,
    PROP_POS_INIT_X,
    PROP_POS_INIT_Y,
    PROP_POS_INIT_Z,
    PROP_POS_INIT_ROLL,
    PROP_POS_INIT_PITCH,
    PROP_POS_INIT_YAW,
    PROP_POS_MODE,
    PROP_COORD_SYS,
    PROP_OD_ENABLE,
    PROP_OD_TRACKING,
    PROP_OD_SEGM,
    PROP_OD_DET_MODEL,
    PROP_OD_FILTER_MODE,
    PROP_OD_CONFIDENCE,
    PROP_OD_MAX_RANGE,
    PROP_OD_BODY_FITTING,
    PROP_OD_PREDICTION_TIMEOUT_S,
    PROP_OD_ALLOW_REDUCED_PRECISION_INFERENCE,
    PROP_OD_PERSON_CONF,
    PROP_OD_VEHICLE_CONF,
    PROP_OD_BAG_CONF,
    PROP_OD_ANIMAL_CONF,
    PROP_OD_ELECTRONICS_CONF,
    PROP_OD_FRUIT_VEGETABLES_CONF,
    PROP_OD_SPORT_CONF,
    PROP_BT_ENABLE,
    PROP_BT_SEGM,
    PROP_BT_SYNC,
    PROP_BT_MODEL,
    PROP_BT_FORMAT,
    PROP_BT_ALLOW_REDUCED_PRECISION_INFERENCE,
    PROP_BT_MAX_RANGE,
    PROP_BT_KP_SELECT,
    PROP_BT_BODY_FITTING,
    PROP_BT_TRACKING,
    PROP_BT_PREDICTION_TIMEOUT_S,
    PROP_BT_CONFIDENCE,
    PROP_BT_MIN_KP_THRESH,
    PROP_BT_SMOOTHING,
    PROP_BRIGHTNESS,
    PROP_CONTRAST,
    PROP_HUE,
    PROP_SATURATION,
    PROP_SHARPNESS,
    PROP_GAMMA,
    PROP_GAIN,
    PROP_EXPOSURE,
    PROP_EXPOSURE_RANGE_MIN,
    PROP_EXPOSURE_RANGE_MAX,
    PROP_AEC_AGC,
    PROP_AEC_AGC_ROI_X,
    PROP_AEC_AGC_ROI_Y,
    PROP_AEC_AGC_ROI_W,
    PROP_AEC_AGC_ROI_H,
    PROP_AEC_AGC_ROI_SIDE,
    PROP_WHITEBALANCE,
    PROP_WHITEBALANCE_AUTO,
    PROP_LEDSTATUS,
    PROP_SVO_REAL_TIME,
    PROP_SDK_GPU_ID,
    PROP_SDK_VERBOSE_LOG_FILE,
    PROP_OPTIONAL_SETTINGS_PATH,
    PROP_SENSORS_REQUIRED,
    PROP_ENABLE_IMAGE_ENHANCEMENT,
    PROP_OPEN_TIMEOUT_SEC,
    PROP_ASYNC_GRAB_CAMERA_RECOVERY,
    PROP_GRAB_COMPUTE_CAPPING_FPS,
    PROP_ENABLE_IMAGE_VALIDITY_CHECK,
    PROP_ASYNC_IMAGE_RETRIEVAL,
    PROP_MAX_WORKING_RES_W,
    PROP_MAX_WORKING_RES_H,
    PROP_REMOVE_SATURATED_AREAS,
    PROP_OD_INSTANCE_ID,
    PROP_OD_CUSTOM_ONNX_FILE,
    PROP_OD_CUSTOM_ONNX_DYNAMIC_INPUT_SHAPE_W,
    PROP_OD_CUSTOM_ONNX_DYNAMIC_INPUT_SHAPE_H,
    PROP_SVO_REC_ENABLE,
    PROP_SVO_REC_FILENAME,
    PROP_SVO_REC_COMPRESSION,
    N_PROPERTIES
};

typedef enum {
    GST_ZEDSRC_HD2K = 0,       // 2208x1242
    GST_ZEDSRC_HD1080 = 1,     // 1920x1080
    GST_ZEDSRC_HD1200 = 2,     // 1920x1200
    GST_ZEDSRC_HD720 = 3,      // 1280x720
    GST_ZEDSRC_SVGA = 4,       // 960x600
    GST_ZEDSRC_VGA = 5,        // 672x376
    GST_ZEDSRC_AUTO_RES = 6,   // Default value for the camera model
} GstZedSrcRes;

typedef enum {
    GST_ZEDSRC_120FPS = 120,
    GST_ZEDSRC_100FPS = 100,
    GST_ZEDSRC_60FPS = 60,
    GST_ZEDSRC_30FPS = 30,
    GST_ZEDSRC_15FPS = 15
} GstZedSrcFPS;

typedef enum {
    GST_ZEDSRC_NO_FLIP = 0,
    GST_ZEDSRC_FLIP = 1,
    GST_ZEDSRC_AUTO = 2,
} GstZedSrcFlip;

typedef enum {
    GST_ZEDSRC_ONLY_LEFT = 0,
    GST_ZEDSRC_ONLY_RIGHT = 1,
    GST_ZEDSRC_LEFT_RIGHT = 2,
    GST_ZEDSRC_DEPTH_16 = 3,
    GST_ZEDSRC_LEFT_DEPTH = 4,
#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
    GST_ZEDSRC_RAW_NV12 = 5,       // Zero-copy NV12 raw buffer (GMSL cameras only)
    GST_ZEDSRC_RAW_NV12_STEREO = 6 // Zero-copy NV12 stereo (left + right)
#endif
} GstZedSrcStreamType;

typedef enum {
    GST_ZEDSRC_COORD_IMAGE = 0,
    GST_ZEDSRC_COORD_LEFT_HANDED_Y_UP = 1,
    GST_ZEDSRC_COORD_RIGHT_HANDED_Y_UP = 2,
    GST_ZEDSRC_COORD_RIGHT_HANDED_Z_UP = 3,
    GST_ZEDSRC_COORD_LEFT_HANDED_Z_UP = 4,
    GST_ZEDSRC_COORD_RIGHT_HANDED_Z_UP_X_FWD = 5
} GstZedSrcCoordSys;

typedef enum {
    GST_ZEDSRC_OD_MULTI_CLASS_BOX_FAST = 0,
    GST_ZEDSRC_OD_MULTI_CLASS_BOX_MEDIUM = 1,
    GST_ZEDSRC_OD_MULTI_CLASS_BOX_ACCURATE = 2,
    GST_ZEDSRC_OD_PERSON_HEAD_BOX_FAST = 3,
    GST_ZEDSRC_OD_PERSON_HEAD_BOX_ACCURATE = 4,
    GST_ZEDSRC_OD_CUSTOM_YOLOLIKE_BOX_OBJECTS = 6
} GstZedSrcOdModel;

typedef enum {
    GST_ZEDSRC_OD_FILTER_MODE_NONE,
    GST_ZEDSRC_OD_FILTER_MODE_NMS3D,
    GST_ZEDSRC_OD_FILTER_MODE_NMS3D_PER_CLASS
} GstZedSrcOdFilterMode;

typedef enum {
    GST_ZEDSRC_BT_HUMAN_BODY_FAST = 0,
    GST_ZEDSRC_BT_HUMAN_BODY_MEDIUM = 1,
    GST_ZEDSRC_BT_HUMAN_BODY_ACCURATE = 2
} GstZedSrcBtModel;

typedef enum {
    GST_ZEDSRC_BT_BODY_18 = 0,
    GST_ZEDSRC_BT_BODY_34 = 1,
    GST_ZEDSRC_BT_BODY_38 = 2
} GstZedSrcBtFormat;

typedef enum { GST_ZEDSRC_BT_KP_FULL = 0, GST_ZEDSRC_BT_KP_UPPER_BODY = 1 } GstZedSrcBtKpSelect;

typedef enum {
    GST_ZEDSRC_SIDE_LEFT = 0,
    GST_ZEDSRC_SIDE_RIGHT = 1,
    GST_ZEDSRC_SIDE_BOTH = 2
} GstZedSrcSide;

typedef enum {
    GST_ZEDSRC_PT_GEN_1 = 0,
    GST_ZEDSRC_PT_GEN_2 = 1,
    GST_ZEDSRC_PT_GEN_3 = 2
} GstZedSrcPtMode;

//////////////// DEFAULT PARAMETERS
/////////////////////////////////////////////////////////////////////////////

// INITIALIZATION
#define DEFAULT_PROP_CAM_RES GST_ZEDSRC_AUTO_RES
#define DEFAULT_PROP_CAM_FPS GST_ZEDSRC_15FPS
#define DEFAULT_PROP_SDK_VERBOSE 0
#define DEFAULT_PROP_CAM_FLIP 2
#define DEFAULT_PROP_CAM_ID 0
#define DEFAULT_PROP_CAM_SN 0
#define DEFAULT_PROP_SVO_FILE ""
#define DEFAULT_PROP_OPENCV_CALIB_FILE ""
#define DEFAULT_PROP_STREAM_IP ""
#define DEFAULT_PROP_STREAM_PORT 30000
#define DEFAULT_PROP_STREAM_TYPE 0
#define DEFAULT_PROP_DEPTH_MIN 300.f
#define DEFAULT_PROP_DEPTH_MAX 20000.f
#define DEFAULT_PROP_DEPTH_MODE static_cast<gint>(sl::DEPTH_MODE::NONE)
#define DEFAULT_PROP_COORD_SYS static_cast<gint>(sl::COORDINATE_SYSTEM::IMAGE)
#define DEFAULT_PROP_DIS_SELF_CALIB FALSE
#define DEFAULT_PROP_DEPTH_STAB 1
#define DEFAULT_PROP_RIGHT_DEPTH FALSE
#define DEFAULT_PROP_SVO_REAL_TIME FALSE
#define DEFAULT_PROP_SDK_GPU_ID -1
#define DEFAULT_PROP_SDK_VERBOSE_LOG_FILE ""
#define DEFAULT_PROP_OPTIONAL_SETTINGS_PATH ""
#define DEFAULT_PROP_SENSORS_REQUIRED FALSE
#define DEFAULT_PROP_ENABLE_IMAGE_ENHANCEMENT TRUE
#define DEFAULT_PROP_OPEN_TIMEOUT_SEC 5.0f
#define DEFAULT_PROP_ASYNC_GRAB_CAMERA_RECOVERY FALSE
#define DEFAULT_PROP_GRAB_COMPUTE_CAPPING_FPS 0.0f
#define DEFAULT_PROP_ENABLE_IMAGE_VALIDITY_CHECK TRUE
#define DEFAULT_PROP_ASYNC_IMAGE_RETRIEVAL FALSE
#define DEFAULT_PROP_MAX_WORKING_RES_W 0
#define DEFAULT_PROP_MAX_WORKING_RES_H 0
#define DEFAULT_PROP_ROI FALSE
#define DEFAULT_PROP_ROI_X -1
#define DEFAULT_PROP_ROI_Y -1
#define DEFAULT_PROP_ROI_W -1
#define DEFAULT_PROP_ROI_H -1

// RUNTIME
#define DEFAULT_PROP_CONFIDENCE_THRESH 50
#define DEFAULT_PROP_TEXTURE_CONF_THRESH 100
#define DEFAULT_PROP_3D_REF_FRAME static_cast<gint>(sl::REFERENCE_FRAME::WORLD)
#define DEFAULT_PROP_FILL_MODE FALSE
#define DEFAULT_PROP_REMOVE_SATURATED_AREAS FALSE

// POSITIONAL TRACKING
#define DEFAULT_PROP_POS_TRACKING FALSE
#define DEFAULT_PROP_PT_MODE GST_ZEDSRC_PT_GEN_1
#define DEFAULT_PROP_CAMERA_STATIC FALSE
#define DEFAULT_PROP_POS_AREA_FILE_PATH ""
#define DEFAULT_PROP_POS_ENABLE_AREA_MEMORY TRUE
#define DEFAULT_PROP_POS_ENABLE_IMU_FUSION TRUE
#define DEFAULT_PROP_POS_ENABLE_POSE_SMOOTHING TRUE
#define DEFAULT_PROP_POS_SET_FLOOR_AS_ORIGIN FALSE
#define DEFAULT_PROP_POS_SET_GRAVITY_AS_ORIGIN TRUE
#define DEFAULT_PROP_POS_DEPTH_MIN_RANGE -1.0
#define DEFAULT_PROP_POS_INIT_X 0.0
#define DEFAULT_PROP_POS_INIT_Y 0.0
#define DEFAULT_PROP_POS_INIT_Z 0.0
#define DEFAULT_PROP_POS_INIT_ROLL 0.0
#define DEFAULT_PROP_POS_INIT_PITCH 0.0
#define DEFAULT_PROP_POS_INIT_YAW 0.0

// OBJECT DETECTION
#define DEFAULT_PROP_OD_ENABLE FALSE
#define DEFAULT_PROP_OD_SYNC TRUE
#define DEFAULT_PROP_OD_TRACKING TRUE
#define DEFAULT_PROP_OD_SEGM FALSE   // NOTE(Walter) for the future
#define DEFAULT_PROP_OD_MODEL GST_ZEDSRC_OD_MULTI_CLASS_BOX_MEDIUM
#define DEFAULT_PROP_OD_FILTER_MODE GST_ZEDSRC_OD_FILTER_MODE_NMS3D_PER_CLASS
#define DEFAULT_PROP_OD_CONFIDENCE 50.0
#define DEFAULT_PROP_OD_MAX_RANGE DEFAULT_PROP_DEPTH_MAX
#define DEFAULT_PROP_OD_PREDICTION_TIMEOUT_S 0.2
#define DEFAULT_PROP_OD_ALLOW_REDUCED_PRECISION_INFERENCE FALSE
#define DEFAULT_PROP_OD_INSTANCE_ID OD_INSTANCE_MODULE_ID
#define DEFAULT_PROP_OD_CUSTOM_ONNX_FILE ""
#define DEFAULT_PROP_OD_CUSTOM_ONNX_DYNAMIC_INPUT_SHAPE_W 512
#define DEFAULT_PROP_OD_CUSTOM_ONNX_DYNAMIC_INPUT_SHAPE_H 512
#define DEFAULT_PROP_OD_PEOPLE_CONF 35.0
#define DEFAULT_PROP_OD_VEHICLE_CONF 35.0
#define DEFAULT_PROP_OD_BAG_CONF 35.0
#define DEFAULT_PROP_OD_ANIMAL_CONF 35.0
#define DEFAULT_PROP_OD_ELECTRONICS_CONF 35.0
#define DEFAULT_PROP_OD_FRUIT_VEGETABLES_CONF 35.0
#define DEFAULT_PROP_OD_SPORT_CONF 35.0

// BODY TRACKING
#define DEFAULT_PROP_BT_ENABLE FALSE
#define DEFAULT_PROP_BT_SEGM FALSE   // NOTE(Walter) for the future
#define DEFAULT_PROP_BT_SYNC TRUE
#define DEFAULT_PROP_BT_MODEL GST_ZEDSRC_BT_HUMAN_BODY_MEDIUM
#define DEFAULT_PROP_BT_FORMAT GST_ZEDSRC_BT_BODY_34
#define DEFAULT_PROP_BT_ALLOW_REDUCED_PRECISION_INFERENCE FALSE
#define DEFAULT_PROP_BT_MAX_RANGE DEFAULT_PROP_DEPTH_MAX
#define DEFAULT_PROP_BT_KP_SELECT GST_ZEDSRC_BT_KP_FULL
#define DEFAULT_PROP_BT_BODY_FITTING TRUE
#define DEFAULT_PROP_BT_TRACKING TRUE
#define DEFAULT_PROP_BT_PREDICTION_TIMEOUT_S 0.2
#define DEFAULT_PROP_BT_CONFIDENCE 20.0
#define DEFAULT_PROP_BT_MIN_KP_THRESH 5
#define DEFAULT_PROP_BT_SMOOTHING 0.0

// CAMERA CONTROLS
#define DEFAULT_PROP_BRIGHTNESS 4
#define DEFAULT_PROP_CONTRAST 4
#define DEFAULT_PROP_HUE 0
#define DEFAULT_PROP_SATURATION 4
#define DEFAULT_PROP_SHARPNESS 4
#define DEFAULT_PROP_GAMMA 8
#define DEFAULT_PROP_GAIN 60
#define DEFAULT_PROP_EXPOSURE 80
#define DEFAULT_PROP_EXPOSURE_RANGE_MIN 28
#define DEFAULT_PROP_EXPOSURE_RANGE_MAX 66000
#define DEFAULT_PROP_AEC_AGC 1
#define DEFAULT_PROP_AEC_AGC_ROI_X -1
#define DEFAULT_PROP_AEC_AGC_ROI_Y -1
#define DEFAULT_PROP_AEC_AGC_ROI_W -1
#define DEFAULT_PROP_AEC_AGC_ROI_H -1
#define DEFAULT_PROP_AEC_AGC_ROI_SIDE GST_ZEDSRC_SIDE_BOTH
#define DEFAULT_PROP_WHITEBALANCE 4600
#define DEFAULT_PROP_WHITEBALANCE_AUTO 1
#define DEFAULT_PROP_LEDSTATUS 1

// SVO RECORDING
#define DEFAULT_PROP_SVO_REC_ENABLE FALSE
#define DEFAULT_PROP_SVO_REC_FILENAME ""
#define DEFAULT_PROP_SVO_REC_COMPRESSION GST_ZEDSRC_SVO_COMPRESSION_H265
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum {
    GST_ZEDSRC_SVO_COMPRESSION_LOSSLESS = 0,
    GST_ZEDSRC_SVO_COMPRESSION_H264 = 1,
    GST_ZEDSRC_SVO_COMPRESSION_H265 = 2,
    GST_ZEDSRC_SVO_COMPRESSION_H264_LOSSLESS = 3,
    GST_ZEDSRC_SVO_COMPRESSION_H265_LOSSLESS = 4,
} GstZedSrcSvoCompression;

#define GST_TYPE_ZED_SVO_COMPRESSION (gst_zedsrc_svo_compression_get_type())
static GType gst_zedsrc_svo_compression_get_type(void) {
    static GType zedsrc_svo_compression_type = 0;

    if (!zedsrc_svo_compression_type) {
        static GEnumValue pattern_types[] = {
            {GST_ZEDSRC_SVO_COMPRESSION_LOSSLESS, "Lossless (PNG/ZSTD, CPU)", "LOSSLESS"},
            {GST_ZEDSRC_SVO_COMPRESSION_H264, "H264 (GPU)", "H264"},
            {GST_ZEDSRC_SVO_COMPRESSION_H265, "H265/HEVC (GPU)", "H265"},
            {GST_ZEDSRC_SVO_COMPRESSION_H264_LOSSLESS, "H264 Lossless (GPU)", "H264_LOSSLESS"},
            {GST_ZEDSRC_SVO_COMPRESSION_H265_LOSSLESS, "H265 Lossless (GPU)", "H265_LOSSLESS"},
            {0, NULL, NULL},
        };

        zedsrc_svo_compression_type =
            g_enum_register_static("GstZedsrcSvoCompression", pattern_types);
    }

    return zedsrc_svo_compression_type;
}

#define GST_TYPE_ZED_SIDE (gst_zedsrc_side_get_type())
static GType gst_zedsrc_side_get_type(void) {
    static GType zedsrc_side_type = 0;

    if (!zedsrc_side_type) {
        static GEnumValue pattern_types[] = {
            {static_cast<gint>(sl::SIDE::LEFT), "Left side only", "LEFT"},
            {static_cast<gint>(sl::SIDE::RIGHT), "Right side only", "RIGHT"},
            {static_cast<gint>(sl::SIDE::BOTH), "Left and Right side", "BOTH"},
            {0, NULL, NULL},
        };

        zedsrc_side_type = g_enum_register_static("GstZedsrcSide", pattern_types);
    }

    return zedsrc_side_type;
}

#define GST_TYPE_ZED_PT_MODE (gst_zedsrc_pt_mode_get_type())
static GType gst_zedsrc_pt_mode_get_type(void) {
    static GType zedsrc_pt_mode_type = 0;

    if (!zedsrc_pt_mode_type) {
        static GEnumValue pattern_types[] = {
            {static_cast<gint>(sl::POSITIONAL_TRACKING_MODE::GEN_1), "Generation 1", "GEN_1"},
            {static_cast<gint>(sl::POSITIONAL_TRACKING_MODE::GEN_2), "Generation 2", "GEN_2"},
            {static_cast<gint>(sl::POSITIONAL_TRACKING_MODE::GEN_3), "Generation 3", "GEN_3"},
            {0, NULL, NULL},
        };

        zedsrc_pt_mode_type = g_enum_register_static("GstZedsrcPtMode", pattern_types);
    }

    return zedsrc_pt_mode_type;
}

#define GST_TYPE_ZED_RESOL (gst_zedsrc_resol_get_type())
static GType gst_zedsrc_resol_get_type(void) {
    static GType zedsrc_resol_type = 0;

    if (!zedsrc_resol_type) {
        static GEnumValue pattern_types[] = {
            {GST_ZEDSRC_HD2K, "2208x1242", "HD2K (USB3)"},
            {GST_ZEDSRC_HD1080, "1920x1080", "HD1080 (USB3/GMSL2)"},
            {GST_ZEDSRC_HD1200, "1920x1200", "HD1200 (GMSL2)"},
            {GST_ZEDSRC_HD720, "1280x720", "HD720 (USB3)"},
            {GST_ZEDSRC_SVGA, "960x600", "SVGA (GMSL2)"},
            {GST_ZEDSRC_VGA, "672x376", "VGA (USB3)"},
            {GST_ZEDSRC_AUTO_RES, "Automatic", "Default value for the camera model"},
            {0, NULL, NULL},
        };

        zedsrc_resol_type = g_enum_register_static("GstZedSrcRes", pattern_types);
    }

    return zedsrc_resol_type;
}

#define GST_TYPE_ZED_FPS (gst_zedsrc_fps_get_type())
static GType gst_zedsrc_fps_get_type(void) {
    static GType zedsrc_fps_type = 0;

    if (!zedsrc_fps_type) {
        static GEnumValue pattern_types[] = {
            {GST_ZEDSRC_120FPS, "only SVGA (GMSL2) resolution", "120 FPS"},
            {GST_ZEDSRC_100FPS, "only VGA (USB3) resolution", "100 FPS"},
            {GST_ZEDSRC_60FPS, "VGA (USB3), HD720, HD1080 (GMSL2), and HD1200 (GMSL2) resolutions",
             "60  FPS"},
            {GST_ZEDSRC_30FPS, "VGA (USB3), HD720 (USB3) and HD1080 (USB3/GMSL2) resolutions",
             "30  FPS"},
            {GST_ZEDSRC_15FPS, "all resolutions (NO GMSL2)", "15  FPS"},
            {0, NULL, NULL},
        };

        zedsrc_fps_type = g_enum_register_static("GstZedSrcFPS", pattern_types);
    }

    return zedsrc_fps_type;
}

#define GST_TYPE_ZED_FLIP (gst_zedsrc_flip_get_type())
static GType gst_zedsrc_flip_get_type(void) {
    static GType zedsrc_flip_type = 0;

    if (!zedsrc_flip_type) {
        static GEnumValue pattern_types[] = {
            {GST_ZEDSRC_NO_FLIP, "Force no flip", "No Flip"},
            {GST_ZEDSRC_FLIP, "Force flip", "Flip"},
            {GST_ZEDSRC_AUTO, "Auto mode (ZED2/ZED2i/ZED-M only)", "Auto"},
            {0, NULL, NULL},
        };

        zedsrc_flip_type = g_enum_register_static("GstZedSrcFlip", pattern_types);
    }

    return zedsrc_flip_type;
}

#define GST_TYPE_ZED_STREAM_TYPE (gst_zedsrc_stream_type_get_type())
static GType gst_zedsrc_stream_type_get_type(void) {
    static GType zedsrc_stream_type_type = 0;

    if (!zedsrc_stream_type_type) {
        static GEnumValue pattern_types[] = {
            {GST_ZEDSRC_ONLY_LEFT, "8 bits- 4 channels Left image", "Left image [BGRA]"},
            {GST_ZEDSRC_ONLY_RIGHT, "8 bits- 4 channels Right image", "Right image [BGRA]"},
            {GST_ZEDSRC_LEFT_RIGHT, "8 bits- 4 channels bit Left and Right",
             "Stereo couple up/down [BGRA]"},
            {GST_ZEDSRC_DEPTH_16, "16 bits depth", "Depth image [GRAY16_LE]"},
            {GST_ZEDSRC_LEFT_DEPTH, "8 bits- 4 channels Left and Depth(image)",
             "Left and Depth up/down [BGRA]"},
#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
            {GST_ZEDSRC_RAW_NV12, "Zero-copy NV12 raw buffer (GMSL cameras only)",
             "Raw NV12 zero-copy [NV12]"},
            {GST_ZEDSRC_RAW_NV12_STEREO, "Zero-copy NV12 stereo (left + right side by side)",
             "Raw NV12 stereo zero-copy [NV12]"},
#endif
            {0, NULL, NULL},
        };

        zedsrc_stream_type_type = g_enum_register_static("GstZedSrcCoordSys", pattern_types);
    }

    return zedsrc_stream_type_type;
}

#define GST_TYPE_ZED_COORD_SYS (gst_zedsrc_coord_sys_get_type())
static GType gst_zedsrc_coord_sys_get_type(void) {
    static GType zedsrc_coord_sys_type = 0;

    if (!zedsrc_coord_sys_type) {
        static GEnumValue pattern_types[] = {
            {GST_ZEDSRC_COORD_IMAGE,
             "Standard coordinates system in computer vision. Used in OpenCV.", "Image"},
            {GST_ZEDSRC_COORD_LEFT_HANDED_Y_UP,
             "Left-Handed with Y up and Z forward. Used in Unity with DirectX.",
             "Left handed, Y up"},
            {GST_ZEDSRC_COORD_RIGHT_HANDED_Y_UP,
             "Right-Handed with Y pointing up and Z backward. Used in OpenGL.",
             "Right handed, Y up"},
            {GST_ZEDSRC_COORD_RIGHT_HANDED_Z_UP,
             "Right-Handed with Z pointing up and Y forward. Used in 3DSMax.",
             "Right handed, Z up"},
            {GST_ZEDSRC_COORD_LEFT_HANDED_Z_UP,
             "Left-Handed with Z axis pointing up and X forward. Used in Unreal Engine.",
             "Left handed, Z up"},
            {GST_ZEDSRC_COORD_RIGHT_HANDED_Z_UP_X_FWD,
             "Right-Handed with Z pointing up and X forward. Used in ROS (REP 103).",
             "Right handed, Z up, X fwd"},
            {0, NULL, NULL},
        };

        zedsrc_coord_sys_type = g_enum_register_static("GstZedsrcStreamType", pattern_types);
    }

    return zedsrc_coord_sys_type;
}

#define GST_TYPE_ZED_OD_MODEL_TYPE (gst_zedsrc_od_model_get_type())
static GType gst_zedsrc_od_model_get_type(void) {
    static GType zedsrc_od_model_type = 0;

    if (!zedsrc_od_model_type) {
        static GEnumValue pattern_types[] = {
            {GST_ZEDSRC_OD_MULTI_CLASS_BOX_FAST, "Any objects, bounding box based",
             "Object Detection Multi class FAST"},
            {GST_ZEDSRC_OD_MULTI_CLASS_BOX_MEDIUM,
             "Any objects, bounding box based, compromise between accuracy and speed",
             "Object Detection Multi class MEDIUM"},
            {GST_ZEDSRC_OD_MULTI_CLASS_BOX_ACCURATE,
             "Any objects, bounding box based, more accurate but slower than the base model",
             "Object Detection Multi class ACCURATE"},
            {GST_ZEDSRC_OD_PERSON_HEAD_BOX_FAST,
             "Bounding Box detector specialized in person heads, particularly well suited for "
             "crowded environments, the person localization is also improved",
             "Person Head FAST"},
            {GST_ZEDSRC_OD_PERSON_HEAD_BOX_ACCURATE,
             "Bounding Box detector specialized in person heads, particularly well suited for "
             "crowded environments, the person localization is also improved, "
             "more accurate but slower than the base model",
             "Person Head ACCURATE"},
            {GST_ZEDSRC_OD_CUSTOM_YOLOLIKE_BOX_OBJECTS,
             "For internal inference using your own custom YOLO-like model. "
             "Requires od-custom-onnx-file property to be set.",
             "Custom YOLO-like Box Objects"},
            {0, NULL, NULL},
        };

        zedsrc_od_model_type = g_enum_register_static("GstZedSrcOdModel", pattern_types);
    }

    return zedsrc_od_model_type;
}

#define GST_TYPE_ZED_OD_FILTER_MODE_TYPE (gst_zedsrc_od_filter_mode_get_type())
static GType gst_zedsrc_od_filter_mode_get_type(void) {
    static GType zedsrc_od_filter_mode_type = 0;

    if (!zedsrc_od_filter_mode_type) {
        static GEnumValue pattern_types[] = {
            {GST_ZEDSRC_OD_FILTER_MODE_NONE,
             "SDK will not apply any preprocessing to the detected objects", "NONE"},
            {GST_ZEDSRC_OD_FILTER_MODE_NMS3D,
             "SDK will remove objects that are in the same 3D position as an already tracked "
             "object (independant of class ID)",
             "NMS3D"},
            {GST_ZEDSRC_OD_FILTER_MODE_NMS3D_PER_CLASS,
             "SDK will remove objects that are in the same 3D position as an already tracked "
             "object of the same class ID.",
             "NMS3D_PER_CLASS"},
            {0, NULL, NULL},
        };

        zedsrc_od_filter_mode_type = g_enum_register_static("GstZedSrcOdFilterMode", pattern_types);
    }

    return zedsrc_od_filter_mode_type;
}

#define GST_TYPE_ZED_BT_MODEL_TYPE (gst_zedsrc_bt_model_get_type())
static GType gst_zedsrc_bt_model_get_type(void) {
    static GType zedsrc_bt_model_type = 0;

    if (!zedsrc_bt_model_type) {
        static GEnumValue pattern_types[] = {
            {GST_ZEDSRC_BT_HUMAN_BODY_FAST,
             "Keypoints based, specific to human skeleton, real time performance even on Jetson or "
             "low end GPU cards",
             "Body Tracking FAST"},
            {GST_ZEDSRC_BT_HUMAN_BODY_MEDIUM,
             "Keypoints based, specific to human skeleton, compromise between accuracy and speed",
             "Body Tracking MEDIUM"},
            {GST_ZEDSRC_BT_HUMAN_BODY_ACCURATE,
             "Keypoints based, specific to human skeleton, state of the art accuracy, requires "
             "powerful GPU",
             "Body Tracking ACCURATE"},
            {0, NULL, NULL},
        };

        zedsrc_bt_model_type = g_enum_register_static("GstZedSrcBtModel", pattern_types);
    }

    return zedsrc_bt_model_type;
}

#define GST_TYPE_ZED_BT_FORMAT_TYPE (gst_zedsrc_bt_format_get_type())
static GType gst_zedsrc_bt_format_get_type(void) {
    static GType zedsrc_bt_format_type = 0;

    if (!zedsrc_bt_format_type) {
        static GEnumValue pattern_types[] = {
            {GST_ZEDSRC_BT_BODY_18, "18 keypoints format. Basic Body format", "Body 18 Key Points"},
            {GST_ZEDSRC_BT_BODY_34,
             "34 keypoints format. Body format, requires body fitting enabled",
             "Body 34 Key Points"},
            {GST_ZEDSRC_BT_BODY_38,
             "38 keypoints format. Body format, including feet simplified face and hands",
             "Body 38 Key Points"},
            {0, NULL, NULL},
        };

        zedsrc_bt_format_type = g_enum_register_static("GstZedSrcBtFormat", pattern_types);
    }

    return zedsrc_bt_format_type;
}

#define GST_TYPE_ZED_BT_KP_SELECT_TYPE (gst_zedsrc_bt_kp_select_get_type())
static GType gst_zedsrc_bt_kp_select_get_type(void) {
    static GType zedsrc_bt_kp_select_type = 0;

    if (!zedsrc_bt_kp_select_type) {
        static GEnumValue pattern_types[] = {
            {GST_ZEDSRC_BT_KP_FULL, "Full keypoint model.", "Full keypoint model."},
            {GST_ZEDSRC_BT_KP_UPPER_BODY,
             "Upper body keypoint model. Only the upper body will be outputted (from hip)",
             "Upper body keypoint model"},
            {0, NULL, NULL},
        };

        zedsrc_bt_kp_select_type = g_enum_register_static("GstZedSrcBtKpSelect", pattern_types);
    }

    return zedsrc_bt_kp_select_type;
}

#define GST_TYPE_ZED_DEPTH_MODE (gst_zedsrc_depth_mode_get_type())
static GType gst_zedsrc_depth_mode_get_type(void) {
    static GType zedsrc_depth_mode_type = 0;

    if (!zedsrc_depth_mode_type) {
        static GEnumValue pattern_types[] = {
            {static_cast<gint>(sl::DEPTH_MODE::NEURAL_PLUS),
             "More accurate Neural disparity estimation, Requires AI module.", "NEURAL_PLUS"},
            {static_cast<gint>(sl::DEPTH_MODE::NEURAL),
             "End to End Neural disparity estimation, requires AI module", "NEURAL"},
            {static_cast<gint>(sl::DEPTH_MODE::NEURAL_LIGHT),
             "End to End Neural disparity estimation (light), requires AI module", "NEURAL_LIGHT"},
            {static_cast<gint>(sl::DEPTH_MODE::ULTRA),
             "Computation mode favorising edges and sharpness. Requires more GPU memory and "
             "computation power.",
             "ULTRA"},
            {static_cast<gint>(sl::DEPTH_MODE::QUALITY),
             "Computation mode designed for challenging areas with untextured surfaces.",
             "QUALITY"},
            {static_cast<gint>(sl::DEPTH_MODE::PERFORMANCE),
             "Computation mode optimized for speed.", "PERFORMANCE"},
            {static_cast<gint>(sl::DEPTH_MODE::NONE),
             "This mode does not compute any depth map. Only rectified stereo images will be "
             "available.",
             "NONE"},
            {0, NULL, NULL},
        };

        zedsrc_depth_mode_type = g_enum_register_static("GstZedsrcDepthMode", pattern_types);
    }

    return zedsrc_depth_mode_type;
}

#define GST_TYPE_ZED_3D_REF_FRAME (gst_zedsrc_3d_meas_ref_frame_get_type())
static GType gst_zedsrc_3d_meas_ref_frame_get_type(void) {
    static GType zedsrc_3d_meas_ref_frame_type = 0;

    if (!zedsrc_3d_meas_ref_frame_type) {
        static GEnumValue pattern_types[] = {
            {static_cast<gint>(sl::REFERENCE_FRAME::WORLD),
             "The positional tracking pose transform will contains the motion with reference to "
             "the world frame.",
             "WORLD"},
            {static_cast<gint>(sl::REFERENCE_FRAME::CAMERA),
             "The  pose transform will contains the motion with reference to the previous camera "
             "frame.",
             "CAMERA"},
            {0, NULL, NULL},
        };

        zedsrc_3d_meas_ref_frame_type =
            g_enum_register_static("GstZedsrc3dMeasRefFrame", pattern_types);
    }

    return zedsrc_3d_meas_ref_frame_type;
}

/* pad templates */
static GstStaticPadTemplate gst_zedsrc_src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS(("video/x-raw, "   // Double stream VGA
                                             "format = (string)BGRA, "
                                             "width = (int)672, "
                                             "height = (int)752 , "
                                             "framerate = (fraction) { 15, 30, 60, 100 }"
                                             ";"
                                             "video/x-raw, "   // Double stream HD720
                                             "format = (string)BGRA, "
                                             "width = (int)1280, "
                                             "height = (int)1440, "
                                             "framerate = (fraction) { 15, 30, 60 }"
                                             ";"
                                             "video/x-raw, "   // Double stream HD1080
                                             "format = (string)BGRA, "
                                             "width = (int)1920, "
                                             "height = (int)2160, "
                                             "framerate = (fraction) { 15, 30, 60 }"
                                             ";"
                                             "video/x-raw, "   // Double stream HD2K
                                             "format = (string)BGRA, "
                                             "width = (int)2208, "
                                             "height = (int)2484, "
                                             "framerate = (fraction)15"
                                             ";"
                                             "video/x-raw, "   // Double stream HD1200 (GMSL2)
                                             "format = (string)BGRA, "
                                             "width = (int)1920, "
                                             "height = (int)2400, "
                                             "framerate = (fraction) { 15, 30, 60 }"
                                             ";"
                                             "video/x-raw, "   // Double stream SVGA (GMSL2)
                                             "format = (string)BGRA, "
                                             "width = (int)960, "
                                             "height = (int)1200, "
                                             "framerate = (fraction) { 15, 30, 60, 120 }"
                                             ";"
                                             "video/x-raw, "   // Color VGA
                                             "format = (string)BGRA, "
                                             "width = (int)672, "
                                             "height =  (int)376, "
                                             "framerate = (fraction) { 15, 30, 60, 100 }"
                                             ";"
                                             "video/x-raw, "   // Color HD720
                                             "format = (string)BGRA, "
                                             "width = (int)1280, "
                                             "height =  (int)720, "
                                             "framerate =  (fraction)  { 15, 30, 60}"
                                             ";"
                                             "video/x-raw, "   // Color HD1080
                                             "format = (string)BGRA, "
                                             "width = (int)1920, "
                                             "height = (int)1080, "
                                             "framerate = (fraction) { 15, 30, 60 }"
                                             ";"
                                             "video/x-raw, "   // Color HD2K
                                             "format = (string)BGRA, "
                                             "width = (int)2208, "
                                             "height = (int)1242, "
                                             "framerate = (fraction)15"
                                             ";"
                                             "video/x-raw, "   // Color HD1200 (GMSL2)
                                             "format = (string)BGRA, "
                                             "width = (int)1920, "
                                             "height = (int)1200, "
                                             "framerate = (fraction) { 15, 30, 60 }"
                                             ";"
                                             "video/x-raw, "   // Color SVGA (GMSL2)
                                             "format = (string)BGRA, "
                                             "width = (int)960, "
                                             "height = (int)600, "
                                             "framerate = (fraction) { 15, 30, 60, 120 }"
                                             ";"
                                             "video/x-raw, "   // Depth VGA
                                             "format = (string)GRAY16_LE, "
                                             "width = (int)672, "
                                             "height =  (int)376, "
                                             "framerate = (fraction) { 15, 30, 60, 100 }"
                                             ";"
                                             "video/x-raw, "   // Depth HD720
                                             "format = (string)GRAY16_LE, "
                                             "width = (int)1280, "
                                             "height =  (int)720, "
                                             "framerate =  (fraction)  { 15, 30, 60}"
                                             ";"
                                             "video/x-raw, "   // Depth HD1080
                                             "format = (string)GRAY16_LE, "
                                             "width = (int)1920, "
                                             "height = (int)1080, "
                                             "framerate = (fraction) { 15, 30, 60 }"
                                             ";"
                                             "video/x-raw, "   // Depth HD2K
                                             "format = (string)GRAY16_LE, "
                                             "width = (int)2208, "
                                             "height = (int)1242, "
                                             "framerate = (fraction)15"
                                             ";"
                                             "video/x-raw, "   // Depth HD1200 (GMSL2)
                                             "format = (string)GRAY16_LE, "
                                             "width = (int)1920, "
                                             "height = (int)1200, "
                                             "framerate = (fraction) { 15, 30, 60 }"
                                             ";"
                                             "video/x-raw, "   // Depth SVGA (GMSL2)
                                             "format = (string)GRAY16_LE, "
                                             "width = (int)960, "
                                             "height = (int)600, "
                                             "framerate = (fraction) { 15, 30, 60, 120 }"
#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
                                             ";"
                                             "video/x-raw(memory:NVMM), "   // NV12 HD1200 (GMSL2 zero-copy)
                                             "format = (string)NV12, "
                                             "width = (int)1920, "
                                             "height = (int)1200, "
                                             "framerate = (fraction) { 15, 30, 60 }"
                                             ";"
                                             "video/x-raw(memory:NVMM), "   // NV12 HD1080 (GMSL2 zero-copy)
                                             "format = (string)NV12, "
                                             "width = (int)1920, "
                                             "height = (int)1080, "
                                             "framerate = (fraction) { 15, 30, 60 }"
                                             ";"
                                             "video/x-raw(memory:NVMM), "   // NV12 SVGA (GMSL2 zero-copy)
                                             "format = (string)NV12, "
                                             "width = (int)960, "
                                             "height = (int)600, "
                                             "framerate = (fraction) { 15, 30, 60, 120 }"
                                             ";"
                                             "video/x-raw(memory:NVMM), "   // NV12 stereo HD1200 (side-by-side)
                                             "format = (string)NV12, "
                                             "width = (int)3840, "
                                             "height = (int)1200, "
                                             "framerate = (fraction) { 15, 30, 60 }"
#endif
                                             )));

/* class initialization */
G_DEFINE_TYPE(GstZedSrc, gst_zedsrc, GST_TYPE_PUSH_SRC);

static void gst_zedsrc_class_init(GstZedSrcClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
    GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS(klass);
    GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS(klass);

    gobject_class->set_property = gst_zedsrc_set_property;
    gobject_class->get_property = gst_zedsrc_get_property;
    gobject_class->dispose = gst_zedsrc_dispose;
    gobject_class->finalize = gst_zedsrc_finalize;

    gst_element_class_add_pad_template(gstelement_class,
                                       gst_static_pad_template_get(&gst_zedsrc_src_template));

    gst_element_class_set_static_metadata(gstelement_class, "ZED Camera Source", "Source/Video",
                                          "Stereolabs ZED Camera source",
                                          "Stereolabs <support@stereolabs.com>");

    gstbasesrc_class->start = GST_DEBUG_FUNCPTR(gst_zedsrc_start);
    gstbasesrc_class->stop = GST_DEBUG_FUNCPTR(gst_zedsrc_stop);
    gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR(gst_zedsrc_get_caps);
    gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR(gst_zedsrc_set_caps);
    gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR(gst_zedsrc_unlock);
    gstbasesrc_class->unlock_stop = GST_DEBUG_FUNCPTR(gst_zedsrc_unlock_stop);
    gstbasesrc_class->query = GST_DEBUG_FUNCPTR(gst_zedsrc_query);

    gstpushsrc_class->fill = GST_DEBUG_FUNCPTR(gst_zedsrc_fill);
#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
    gstpushsrc_class->create = GST_DEBUG_FUNCPTR(gst_zedsrc_create);
#endif

    /* Install GObject properties */
    g_object_class_install_property(
        gobject_class, PROP_CAM_RES,
        g_param_spec_enum("camera-resolution", "Camera Resolution", "Camera Resolution",
                          GST_TYPE_ZED_RESOL, DEFAULT_PROP_CAM_RES,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_FPS,
        g_param_spec_enum("camera-fps", "Camera frame rate", "Camera frame rate", GST_TYPE_ZED_FPS,
                          DEFAULT_PROP_CAM_FPS,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_STREAM_TYPE,
        g_param_spec_enum("stream-type", "Image stream type", "Image stream type",
                          GST_TYPE_ZED_STREAM_TYPE, DEFAULT_PROP_STREAM_TYPE,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_SDK_VERBOSE,
        g_param_spec_int("sdk-verbose", "ZED SDK Verbose", "ZED SDK Verbose level", 0, 1000,
                         DEFAULT_PROP_SDK_VERBOSE,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_FLIP,
        g_param_spec_enum("camera-image-flip", "Camera image flip",
                          "Use the camera in forced flip/no flip or automatic mode",
                          GST_TYPE_ZED_FLIP, DEFAULT_PROP_CAM_FLIP,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_ID,
        g_param_spec_int("camera-id", "Camera ID", "Select camera from cameraID", 0, 255,
                         DEFAULT_PROP_CAM_ID,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_SN,
        g_param_spec_int64("camera-sn", "Camera Serial Number",
                           "Select camera from camera serial number", 0, G_MAXINT64,
                           DEFAULT_PROP_CAM_SN,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_SVO_FILE,
        g_param_spec_string("svo-file-path", "SVO file", "Input from SVO file",
                            DEFAULT_PROP_SVO_FILE,
                            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OPENCV_CALIB_FILE,
        g_param_spec_string("opencv-calibration-file", "Optional OpenCV Calibration File",
                            "Optional OpenCV Calibration File", DEFAULT_PROP_OPENCV_CALIB_FILE,
                            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_STREAM_IP,
        g_param_spec_string("input-stream-ip", "Input Stream IP",
                            "Specify IP address when using streaming input", DEFAULT_PROP_SVO_FILE,
                            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_STREAM_PORT,
        g_param_spec_int("input-stream-port", "Input Stream Port",
                         "Specify port when using streaming input", 1, G_MAXUINT16,
                         DEFAULT_PROP_STREAM_PORT,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_DEPTH_MIN,
        g_param_spec_float("depth-minimum-distance", "Minimum depth value", "Minimum depth value",
                           100.f, 3000.f, DEFAULT_PROP_DEPTH_MIN,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_DEPTH_MAX,
        g_param_spec_float("depth-maximum-distance", "Maximum depth value", "Maximum depth value",
                           500.f, 40000.f, DEFAULT_PROP_DEPTH_MAX,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_DEPTH_MODE,
        g_param_spec_enum("depth-mode", "Depth Mode", "Depth Mode", GST_TYPE_ZED_DEPTH_MODE,
                          DEFAULT_PROP_DEPTH_MODE,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_DIS_SELF_CALIB,
        g_param_spec_boolean("camera-disable-self-calib", "Disable self calibration",
                             "Disable the self calibration processing when the camera is opened",
                             DEFAULT_PROP_DIS_SELF_CALIB,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    /*g_object_class_install_property( gobject_class, PROP_RIGHT_DEPTH_ENABLE,
                                     g_param_spec_boolean("enable-right-side-measure", "Enable right
       side measure", "Enable the MEASURE::DEPTH_RIGHT and other MEASURE::<XXX>_RIGHT at the cost of
       additional computation time", DEFAULT_PROP_RIGHT_DEPTH, (GParamFlags) (G_PARAM_READWRITE |
       G_PARAM_STATIC_STRINGS)));*/

    g_object_class_install_property(
        gobject_class, PROP_DEPTH_STAB,
        g_param_spec_int("depth-stabilization", "Depth stabilization", "Enable depth stabilization",
                         0, 100, DEFAULT_PROP_DEPTH_STAB,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_COORD_SYS,
        g_param_spec_enum("coordinate-system", "SDK Coordinate System", "3D Coordinate System",
                          GST_TYPE_ZED_COORD_SYS, DEFAULT_PROP_COORD_SYS,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ROI,
        g_param_spec_boolean("roi", "Region of interest", "Enable region of interest filtering",
                             DEFAULT_PROP_ROI,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ROI_X,
        g_param_spec_int("roi-x", "Region of interest top left 'X' coordinate",
                         "Region of interest top left 'X' coordinate (-1 to not set ROI)", -1, 2208,
                         DEFAULT_PROP_ROI_X,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ROI_Y,
        g_param_spec_int("roi-y", "Region of interest top left 'Y' coordinate",
                         "Region of interest top left 'Y' coordinate (-1 to not set ROI)", -1, 1242,
                         DEFAULT_PROP_ROI_Y,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ROI_W,
        g_param_spec_int(
            "roi-w", "Region of interest width", "Region of interest width (-1 to not set ROI)", -1,
            2208, DEFAULT_PROP_ROI_W, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ROI_H,
        g_param_spec_int("roi-h", "Region of interest height",
                         "Region of interest height (-1 to not set ROI)", -1, 1242,
                         DEFAULT_PROP_ROI_H,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CONFIDENCE_THRESH,
        g_param_spec_int("confidence-threshold", "Depth Confidence Threshold",
                         "Specify the Depth Confidence Threshold", 0, 100,
                         DEFAULT_PROP_CONFIDENCE_THRESH,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_TEXTURE_CONF_THRESH,
        g_param_spec_int("texture-confidence-threshold", "Texture Confidence Threshold",
                         "Specify the Texture Confidence Threshold", 0, 100,
                         DEFAULT_PROP_TEXTURE_CONF_THRESH,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_3D_REF_FRAME,
        g_param_spec_enum("measure3D-reference-frame", "3D Measures Reference Frame",
                          "Specify the 3D Reference Frame", GST_TYPE_ZED_3D_REF_FRAME,
                          DEFAULT_PROP_3D_REF_FRAME,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_FILL_MODE,
        g_param_spec_boolean("fill-mode", "Depth Fill Mode", "Specify the Depth Fill Mode",
                             DEFAULT_PROP_FILL_MODE,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POS_TRACKING,
        g_param_spec_boolean("enable-positional-tracking", "Positional tracking",
                             "Enable positional tracking", DEFAULT_PROP_POS_TRACKING,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POS_MODE,
        g_param_spec_enum("positional-tracking-mode", "Positional tracking mode",
                          "Positional tracking mode", GST_TYPE_ZED_PT_MODE, DEFAULT_PROP_PT_MODE,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAMERA_STATIC,
        g_param_spec_boolean("set-as-static", "Camera static",
                             "Set to TRUE if the camera is static", DEFAULT_PROP_CAMERA_STATIC,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POS_AREA_FILE_PATH,
        g_param_spec_string("area-file-path", "Area file path",
                            "Area localization file that describes the surroundings, saved"
                            " from a previous tracking session.",
                            DEFAULT_PROP_POS_AREA_FILE_PATH,
                            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POS_ENABLE_AREA_MEMORY,
        g_param_spec_boolean("enable-area-memory", "Enable area memory",
                             "This mode enables the camera to remember its surroundings. "
                             "This helps correct positional tracking drift, and can be "
                             "helpful for positioning different cameras relative to one "
                             "other in space.",
                             DEFAULT_PROP_POS_ENABLE_AREA_MEMORY,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POS_ENABLE_IMU_FUSION,
        g_param_spec_boolean("enable-imu-fusion", "Enable IMU fusion",
                             "This setting allows you to enable or disable IMU fusion. "
                             "When set to false, only the optical odometry will be used.",
                             DEFAULT_PROP_POS_ENABLE_IMU_FUSION,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POS_ENABLE_POSE_SMOOTHING,
        g_param_spec_boolean("enable-pose-smoothing", "Enable Pose Smoothing",
                             "This mode enables smooth pose correction for small drift "
                             "correction.",
                             DEFAULT_PROP_POS_ENABLE_POSE_SMOOTHING,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POS_SET_FLOOR_AS_ORIGIN,
        g_param_spec_boolean("set-floor-as-origin", "Set floor as pose origin",
                             "This mode initializes the tracking to be aligned with the "
                             "floor plane to better position the camera in space.",
                             DEFAULT_PROP_POS_SET_FLOOR_AS_ORIGIN,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POS_SET_GRAVITY_AS_ORIGIN,
        g_param_spec_boolean("set-gravity-as-origin", "Set gravity as pose origin",
                             "This setting allows you to override of 2 of the 3 rotations from "
                             "initial-world-transform using the IMU gravity default: true",
                             DEFAULT_PROP_POS_SET_GRAVITY_AS_ORIGIN,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POS_DEPTH_MIN_RANGE,
        g_param_spec_float("pos-depth-min-range", "Set depth minimum range",
                           "This setting allows you to change the minmum depth used by the "
                           "SDK for Positional Tracking.",
                           -1, 65535, DEFAULT_PROP_POS_DEPTH_MIN_RANGE,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POS_INIT_X,
        g_param_spec_float("initial-world-transform-x", "Initial X coordinate",
                           "X position of the camera in the world frame when the camera is started",
                           -G_MAXFLOAT, G_MAXFLOAT, DEFAULT_PROP_POS_INIT_X,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POS_INIT_Y,
        g_param_spec_float("initial-world-transform-y", "Initial Y coordinate",
                           "Y position of the camera in the world frame when the camera is started",
                           -G_MAXFLOAT, G_MAXFLOAT, DEFAULT_PROP_POS_INIT_Y,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POS_INIT_Z,
        g_param_spec_float("initial-world-transform-z", "Initial Z coordinate",
                           "Z position of the camera in the world frame when the camera is started",
                           -G_MAXFLOAT, G_MAXFLOAT, DEFAULT_PROP_POS_INIT_Z,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POS_INIT_ROLL,
        g_param_spec_float(
            "initial-world-transform-roll", "Initial Roll orientation",
            "Roll orientation of the camera in the world frame when the camera is started", 0.0f,
            360.0f, DEFAULT_PROP_POS_INIT_ROLL,
            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POS_INIT_PITCH,
        g_param_spec_float(
            "initial-world-transform-pitch", "Initial Pitch orientation",
            "Pitch orientation of the camera in the world frame when the camera is started", 0.0f,
            360.0f, DEFAULT_PROP_POS_INIT_PITCH,
            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POS_INIT_YAW,
        g_param_spec_float(
            "initial-world-transform-yaw", "Initial Yaw orientation",
            "Yaw orientation of the camera in the world frame when the camera is started", 0.0f,
            360.0f, DEFAULT_PROP_POS_INIT_YAW,
            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_ENABLE,
        g_param_spec_boolean("od-enabled", "Object Detection enable",
                             "Set to TRUE to enable Object Detection", DEFAULT_PROP_OD_ENABLE,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_TRACKING,
        g_param_spec_boolean("od-enable-tracking", "Object detection tracking",
                             "Set to TRUE to enable tracking for the detected objects",
                             DEFAULT_PROP_OD_TRACKING,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    // Not yet supported
    // g_object_class_install_property(gobject_class, PROP_OD_SEGM,
    //                                 g_param_spec_boolean("od-segm", "OD Segmentation Mask
    //                                 output",
    //                                                      "Set to TRUE to enable segmentation mask
    //                                                      output for the detected objects",
    //                                                      DEFAULT_PROP_OD_SEGM, (GParamFlags)
    //                                                      (G_PARAM_READWRITE |
    //                                                      G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_DET_MODEL,
        g_param_spec_enum("od-detection-model", "Object detection model", "Object Detection Model",
                          GST_TYPE_ZED_OD_MODEL_TYPE, DEFAULT_PROP_OD_MODEL,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_FILTER_MODE,
        g_param_spec_enum("od-detection-filter-mode", "Object detection filter mode",
                          "Object Detection Filter Mode", GST_TYPE_ZED_OD_FILTER_MODE_TYPE,
                          DEFAULT_PROP_OD_FILTER_MODE,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_CONFIDENCE,
        g_param_spec_float("od-confidence", "Minimum Object detection confidence threshold",
                           "Minimum Detection Confidence", 0.0f, 100.0f, DEFAULT_PROP_OD_CONFIDENCE,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_MAX_RANGE,
        g_param_spec_float("od-max-range", "Defines the maximum object detection range",
                           "Maximum Detection Range", -1.0f, 20000.0f, DEFAULT_PROP_OD_MAX_RANGE,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_PREDICTION_TIMEOUT_S,
        g_param_spec_float("od-prediction-timeout-s", "Object detection prediction timeout (sec)",
                           "Object prediction timeout (sec)", 0.0f, 1.0f,
                           DEFAULT_PROP_OD_PREDICTION_TIMEOUT_S,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_ALLOW_REDUCED_PRECISION_INFERENCE,
        g_param_spec_boolean(
            "od-allow-reduced-precision-inference", "Allow inference at reduced precision",
            "Set to TRUE to allow inference to run at a lower precision to improve runtime",
            DEFAULT_PROP_OD_ALLOW_REDUCED_PRECISION_INFERENCE,
            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_PERSON_CONF,
        g_param_spec_float("od-conf-people",
                           "Defines the detection confidence threashold for the PEOPLE class (-1.0 "
                           "to disable the detection)",
                           "People Detection Confidence Threshold", -1.0f, 100.0f,
                           DEFAULT_PROP_OD_PEOPLE_CONF,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_VEHICLE_CONF,
        g_param_spec_float("od-conf-vehicle",
                           "Defines the detection confidence threashold for the VEHICLE class "
                           "(-1.0 to disable the detection)",
                           "Vehicle Detection Confidence Threshold", -1.0f, 100.0f,
                           DEFAULT_PROP_OD_VEHICLE_CONF,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_BAG_CONF,
        g_param_spec_float("od-conf-bag",
                           "Defines the detection confidence threashold for the BAG class (-1.0 to "
                           "disable the detection)",
                           "Bag Detection Confidence Threshold", -1.0f, 100.0f,
                           DEFAULT_PROP_OD_BAG_CONF,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_ANIMAL_CONF,
        g_param_spec_float("od-conf-animal",
                           "Defines the detection confidence threashold for the ANIMAL class (-1.0 "
                           "to disable the detection)",
                           "Animal Detection Confidence Threshold", -1.0f, 100.0f,
                           DEFAULT_PROP_OD_ANIMAL_CONF,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_ELECTRONICS_CONF,
        g_param_spec_float("od-conf-electronics",
                           "Defines the detection confidence threashold for the ELECTRONICS class "
                           "(-1.0 to disable the detection)",
                           "Electronics Detection Confidence Threshold", -1.0f, 100.0f,
                           DEFAULT_PROP_OD_ELECTRONICS_CONF,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_FRUIT_VEGETABLES_CONF,
        g_param_spec_float("od-conf-fruit-vegetables",
                           "Defines the detection confidence threashold for the FRUIT_VEGETABLES "
                           "class (-1.0 to disable the detection)",
                           "Fruit/Vegetables Detection Confidence Threshold", -1.0f, 100.0f,
                           DEFAULT_PROP_OD_FRUIT_VEGETABLES_CONF,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_SPORT_CONF,
        g_param_spec_float("od-conf-sport",
                           "Defines the detection confidence threashold for the SPORT class (-1.0 "
                           "to disable the detection)",
                           "Sport Detection Confidence Threshold", -1.0f, 100.0f,
                           DEFAULT_PROP_OD_SPORT_CONF,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_BT_ENABLE,
        g_param_spec_boolean("bt-enabled", "Body Tracking enable",
                             "Set to TRUE to enable Body Tracking", DEFAULT_PROP_BT_ENABLE,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    // Not yet supported
    // g_object_class_install_property(
    //     gobject_class, DEFAULT_PROP_BT_SEGM,
    //     g_param_spec_boolean(
    //         "bt-segm", "BT Segmentation Mask output",
    //         "Set to TRUE to enable segmentation mask output for the detected bodies",
    //         DEFAULT_PROP_BT_SEGM, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_BT_MODEL,
        g_param_spec_enum("bt-detection-model", "Body Tracking model", "Body Tracking Model",
                          GST_TYPE_ZED_BT_MODEL_TYPE, DEFAULT_PROP_BT_MODEL,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_BT_FORMAT,
        g_param_spec_enum("bt-format", "Body Tracking format", "Body Tracking format",
                          GST_TYPE_ZED_BT_FORMAT_TYPE, DEFAULT_PROP_BT_FORMAT,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_BT_ALLOW_REDUCED_PRECISION_INFERENCE,
        g_param_spec_boolean("bt-allow-red-prec", "Body Tracking reduced inference precision",
                             "Set to TRUE to enable Body Tracking reduced inference precision ",
                             DEFAULT_PROP_BT_ALLOW_REDUCED_PRECISION_INFERENCE,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_BT_MAX_RANGE,
        g_param_spec_float("bt-max-range", "Defines the maximum Body Tracking range",
                           "Maximum Detection Range", -1.0f, 20000.0f, DEFAULT_PROP_BT_MAX_RANGE,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_BT_BODY_FITTING,
        g_param_spec_boolean("bt-body-fitting", "Body Tracking model fitting",
                             "Set to TRUE to enable Body Tracking model fitting ",
                             DEFAULT_PROP_BT_BODY_FITTING,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_BT_TRACKING,
        g_param_spec_boolean("bt-body-tracking", "Body Tracking model tracking",
                             "Set to TRUE to enable body tracking across images flow ",
                             DEFAULT_PROP_BT_TRACKING,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_BT_PREDICTION_TIMEOUT_S,
        g_param_spec_float("bt-prediction-timeout-s", "Body Tracking prediction timeout(sec) ",
                           "Body Tracking prediction timeout (sec)", 0.0f, 1.0f,
                           DEFAULT_PROP_BT_PREDICTION_TIMEOUT_S,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_BT_CONFIDENCE,
        g_param_spec_float("bt-confidence", "Minimum Body tracking detection confidence threshold",
                           "Minimum Detection Confidence", 0.0f, 100.0f, DEFAULT_PROP_BT_CONFIDENCE,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_BT_MIN_KP_THRESH,
        g_param_spec_int("bt-min-keypoints", "Minimum keypoints threshold.",
                         "Specify the Minimum keypoints threshold.", 0, 70,
                         DEFAULT_PROP_BT_MIN_KP_THRESH,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_BT_SMOOTHING,
        g_param_spec_float("bt-smoothing", "Body tracking smoothing of the fitted fused skeletond",
                           "Smoothing of the fitted fused skeleton", 0.0f, 1.0f,
                           DEFAULT_PROP_BT_SMOOTHING,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

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
        g_param_spec_int("ctrl-gamma", "Camera control: gamma", "Image gamma", 1, 9,
                         DEFAULT_PROP_GAMMA,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_GAIN,
        g_param_spec_int("ctrl-gain", "Camera control: gain", "Camera gain", 0, 100,
                         DEFAULT_PROP_GAIN,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_EXPOSURE,
        g_param_spec_int("ctrl-exposure", "Camera control: exposure", "Camera exposure", 0, 100,
                         DEFAULT_PROP_EXPOSURE,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_EXPOSURE_RANGE_MIN,
        g_param_spec_int("ctrl-exposure-range-min", "Minimum Exposure time [µsec]",
                         "Minimum exposure time in microseconds for the automatic exposure setting",
                         28, 66000, DEFAULT_PROP_EXPOSURE_RANGE_MIN,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_EXPOSURE_RANGE_MAX,
        g_param_spec_int("ctrl-exposure-range-max", "Maximum Exposure time [µsec]",
                         "Maximum exposure time in microseconds for the automatic exposure setting",
                         28, 66000, DEFAULT_PROP_EXPOSURE_RANGE_MAX,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_AEC_AGC,
        g_param_spec_boolean("ctrl-aec-agc", "Camera control: automatic gain and exposure",
                             "Camera automatic gain and exposure", DEFAULT_PROP_AEC_AGC,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_AEC_AGC_ROI_X,
        g_param_spec_int("ctrl-aec-agc-roi-x",
                         "Camera control: auto gain/exposure ROI top left 'X' coordinate",
                         "Auto gain/exposure ROI top left 'X' coordinate (-1 to not set ROI)", -1,
                         2208, DEFAULT_PROP_AEC_AGC_ROI_X,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_AEC_AGC_ROI_Y,
        g_param_spec_int("ctrl-aec-agc-roi-y",
                         "Camera control: auto gain/exposure ROI top left 'Y' coordinate",
                         "Auto gain/exposure ROI top left 'Y' coordinate (-1 to not set ROI)", -1,
                         1242, DEFAULT_PROP_AEC_AGC_ROI_Y,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_AEC_AGC_ROI_W,
        g_param_spec_int("ctrl-aec-agc-roi-w", "Camera control: auto gain/exposure ROI width",
                         "Auto gain/exposure ROI width (-1 to not set ROI)", -1, 2208,
                         DEFAULT_PROP_AEC_AGC_ROI_W,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_AEC_AGC_ROI_H,
        g_param_spec_int("ctrl-aec-agc-roi-h", "Camera control: auto gain/exposure ROI height",
                         "Auto gain/exposure ROI height (-1 to not set ROI)", -1, 1242,
                         DEFAULT_PROP_AEC_AGC_ROI_H,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_AEC_AGC_ROI_SIDE,
        g_param_spec_enum("ctrl-aec-agc-roi-side", "Camera control: auto gain/exposure ROI side",
                          "Auto gain/exposure ROI side", GST_TYPE_ZED_SIDE,
                          DEFAULT_PROP_AEC_AGC_ROI_SIDE,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_WHITEBALANCE,
        g_param_spec_int("ctrl-whitebalance-temperature",
                         "Camera control: white balance temperature",
                         "Image white balance temperature", 2800, 6500, DEFAULT_PROP_WHITEBALANCE,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_WHITEBALANCE_AUTO,
        g_param_spec_boolean("ctrl-whitebalance-auto", "Camera control: automatic whitebalance",
                             "Image automatic white balance", DEFAULT_PROP_WHITEBALANCE_AUTO,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_LEDSTATUS,
        g_param_spec_boolean("ctrl-led-status", "Camera control: led status", "Camera LED on/off",
                             DEFAULT_PROP_LEDSTATUS,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    // SVO Recording properties
    g_object_class_install_property(
        gobject_class, PROP_SVO_REC_ENABLE,
        g_param_spec_boolean(
            "svo-recording-enable", "SVO Recording: Enable",
            "Start/stop SVO recording at runtime (requires svo-recording-filename to be set)",
            DEFAULT_PROP_SVO_REC_ENABLE,
            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_SVO_REC_FILENAME,
        g_param_spec_string("svo-recording-filename", "SVO Recording: Filename",
                            "Output filename for SVO recording (.svo2 extension recommended)",
                            DEFAULT_PROP_SVO_REC_FILENAME,
                            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_SVO_REC_COMPRESSION,
        g_param_spec_enum("svo-recording-compression", "SVO Recording: Compression Mode",
                          "Compression mode for SVO recording", GST_TYPE_ZED_SVO_COMPRESSION,
                          DEFAULT_PROP_SVO_REC_COMPRESSION,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_SVO_REAL_TIME,
        g_param_spec_boolean("svo-real-time-mode", "SVO Real Time Mode", "SVO Real Time Mode",
                             DEFAULT_PROP_SVO_REAL_TIME,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_SDK_GPU_ID,
        g_param_spec_int("sdk-gpu-id", "SDK GPU ID", "SDK GPU ID", -1, 128, DEFAULT_PROP_SDK_GPU_ID,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_SDK_VERBOSE_LOG_FILE,
        g_param_spec_string("sdk-verbose-log-file", "SDK Verbose Log File", "SDK Verbose Log File",
                            DEFAULT_PROP_SDK_VERBOSE_LOG_FILE,
                            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OPTIONAL_SETTINGS_PATH,
        g_param_spec_string("optional-settings-path", "Optional Settings Path",
                            "Optional Settings Path", DEFAULT_PROP_OPTIONAL_SETTINGS_PATH,
                            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_SENSORS_REQUIRED,
        g_param_spec_boolean("sensors-required", "Sensors Required", "Sensors Required",
                             DEFAULT_PROP_SENSORS_REQUIRED,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ENABLE_IMAGE_ENHANCEMENT,
        g_param_spec_boolean("enable-image-enhancement", "Enable Image Enhancement",
                             "Enable Image Enhancement", DEFAULT_PROP_ENABLE_IMAGE_ENHANCEMENT,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OPEN_TIMEOUT_SEC,
        g_param_spec_float("open-timeout-sec", "Open Timeout Seconds", "Open Timeout Seconds", 0.0f,
                           600.0f, DEFAULT_PROP_OPEN_TIMEOUT_SEC,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ASYNC_GRAB_CAMERA_RECOVERY,
        g_param_spec_boolean("async-grab-camera-recovery", "Async Grab Camera Recovery",
                             "Async Grab Camera Recovery", DEFAULT_PROP_ASYNC_GRAB_CAMERA_RECOVERY,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_GRAB_COMPUTE_CAPPING_FPS,
        g_param_spec_float("grab-compute-capping-fps", "Grab Compute Capping FPS",
                           "Grab Compute Capping FPS", 0.0f, 1000.0f,
                           DEFAULT_PROP_GRAB_COMPUTE_CAPPING_FPS,
                           (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ENABLE_IMAGE_VALIDITY_CHECK,
        g_param_spec_boolean("enable-image-validity-check", "Enable Image Validity Check",
                             "Enable Image Validity Check",
                             DEFAULT_PROP_ENABLE_IMAGE_VALIDITY_CHECK,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ASYNC_IMAGE_RETRIEVAL,
        g_param_spec_boolean("async-image-retrieval", "Async Image Retrieval",
                             "Async Image Retrieval", DEFAULT_PROP_ASYNC_IMAGE_RETRIEVAL,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_MAX_WORKING_RES_W,
        g_param_spec_int("max-working-res-w", "Maximum Working Resolution Width",
                         "Maximum Working Resolution Width", 0, 10000,
                         DEFAULT_PROP_MAX_WORKING_RES_W,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_MAX_WORKING_RES_H,
        g_param_spec_int("max-working-res-h", "Maximum Working Resolution Height",
                         "Maximum Working Resolution Height", 0, 10000,
                         DEFAULT_PROP_MAX_WORKING_RES_H,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_REMOVE_SATURATED_AREAS,
        g_param_spec_boolean("remove-saturated-areas", "Remove Saturated Areas",
                             "Remove Saturated Areas", DEFAULT_PROP_REMOVE_SATURATED_AREAS,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_INSTANCE_ID,
        g_param_spec_uint("od-instance-id", "Object Detection Instance ID",
                          "Object Detection Instance ID", 0, G_MAXUINT, DEFAULT_PROP_OD_INSTANCE_ID,
                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_CUSTOM_ONNX_FILE,
        g_param_spec_string("od-custom-onnx-file", "Object Detection Custom ONNX File",
                            "Object Detection Custom ONNX File", DEFAULT_PROP_OD_CUSTOM_ONNX_FILE,
                            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_CUSTOM_ONNX_DYNAMIC_INPUT_SHAPE_W,
        g_param_spec_int("od-custom-onnx-dynamic-input-shape-w",
                         "Object Detection Custom ONNX Dynamic Input Shape Width",
                         "Object Detection Custom ONNX Dynamic Input Shape Width", 0, 10000,
                         DEFAULT_PROP_OD_CUSTOM_ONNX_DYNAMIC_INPUT_SHAPE_W,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OD_CUSTOM_ONNX_DYNAMIC_INPUT_SHAPE_H,
        g_param_spec_int("od-custom-onnx-dynamic-input-shape-h",
                         "Object Detection Custom ONNX Dynamic Input Shape Height",
                         "Object Detection Custom ONNX Dynamic Input Shape Height", 0, 10000,
                         DEFAULT_PROP_OD_CUSTOM_ONNX_DYNAMIC_INPUT_SHAPE_H,
                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_zedsrc_reset(GstZedSrc *src) {
    if (src->zed.isOpened()) {
        src->zed.close();
    }

    src->out_framesize = 0;
    src->is_started = FALSE;

    src->last_frame_count = 0;
    src->total_dropped_frames = 0;
    src->buffer_index = 0;

    if (src->caps) {
        gst_caps_unref(src->caps);
        src->caps = NULL;
    }
}

static void gst_zedsrc_init(GstZedSrc *src) {
    /* set source as live (no preroll) */
    gst_base_src_set_live(GST_BASE_SRC(src), TRUE);

    /* override default of BYTES to operate in time mode */
    gst_base_src_set_format(GST_BASE_SRC(src), GST_FORMAT_TIME);

    // ----> Parameters initialization
    src->camera_resolution = DEFAULT_PROP_CAM_RES;
    src->camera_fps = DEFAULT_PROP_CAM_FPS;
    src->sdk_verbose = DEFAULT_PROP_SDK_VERBOSE;
    src->camera_image_flip = DEFAULT_PROP_CAM_FLIP;
    src->camera_id = DEFAULT_PROP_CAM_ID;
    src->camera_sn = DEFAULT_PROP_CAM_SN;
    src->svo_file = g_string_new(DEFAULT_PROP_SVO_FILE);
    src->opencv_calibration_file = g_string_new(DEFAULT_PROP_OPENCV_CALIB_FILE);
    src->stream_ip = g_string_new(DEFAULT_PROP_STREAM_IP);

    src->stream_port = DEFAULT_PROP_STREAM_PORT;
    src->stream_type = DEFAULT_PROP_STREAM_TYPE;
    src->depth_min_dist = DEFAULT_PROP_DEPTH_MIN;
    src->depth_max_dist = DEFAULT_PROP_DEPTH_MAX;
    src->depth_mode = DEFAULT_PROP_DEPTH_MODE;
    src->camera_disable_self_calib = DEFAULT_PROP_DIS_SELF_CALIB;
    src->depth_stabilization = DEFAULT_PROP_DEPTH_STAB;
    src->coord_sys = DEFAULT_PROP_COORD_SYS;
    src->svo_real_time = DEFAULT_PROP_SVO_REAL_TIME;
    src->sdk_gpu_id = DEFAULT_PROP_SDK_GPU_ID;
    src->sdk_verbose_log_file = g_string_new(DEFAULT_PROP_SDK_VERBOSE_LOG_FILE);
    src->optional_settings_path = g_string_new(DEFAULT_PROP_OPTIONAL_SETTINGS_PATH);
    src->sensors_required = DEFAULT_PROP_SENSORS_REQUIRED;
    src->enable_image_enhancement = DEFAULT_PROP_ENABLE_IMAGE_ENHANCEMENT;
    src->open_timeout_sec = DEFAULT_PROP_OPEN_TIMEOUT_SEC;
    src->async_grab_camera_recovery = DEFAULT_PROP_ASYNC_GRAB_CAMERA_RECOVERY;
    src->grab_compute_capping_fps = DEFAULT_PROP_GRAB_COMPUTE_CAPPING_FPS;
    src->enable_image_validity_check = DEFAULT_PROP_ENABLE_IMAGE_VALIDITY_CHECK;
    src->async_image_retrieval = DEFAULT_PROP_ASYNC_IMAGE_RETRIEVAL;
    src->max_working_res_w = DEFAULT_PROP_MAX_WORKING_RES_W;
    src->max_working_res_h = DEFAULT_PROP_MAX_WORKING_RES_H;

    src->confidence_threshold = DEFAULT_PROP_CONFIDENCE_THRESH;
    src->texture_confidence_threshold = DEFAULT_PROP_TEXTURE_CONF_THRESH;
    src->measure3D_reference_frame = DEFAULT_PROP_3D_REF_FRAME;
    src->fill_mode = DEFAULT_PROP_FILL_MODE;
    src->remove_saturated_areas = DEFAULT_PROP_REMOVE_SATURATED_AREAS;
    src->roi = DEFAULT_PROP_ROI;
    src->roi_x = DEFAULT_PROP_ROI_X;
    src->roi_y = DEFAULT_PROP_ROI_Y;
    src->roi_w = DEFAULT_PROP_ROI_W;
    src->roi_h = DEFAULT_PROP_ROI_H;

    src->pos_tracking = DEFAULT_PROP_POS_TRACKING;
    src->camera_static = DEFAULT_PROP_CAMERA_STATIC;
    src->area_file_path = g_string_new(DEFAULT_PROP_POS_AREA_FILE_PATH);
    src->enable_area_memory = DEFAULT_PROP_POS_ENABLE_AREA_MEMORY;
    src->enable_imu_fusion = DEFAULT_PROP_POS_ENABLE_IMU_FUSION;
    src->enable_pose_smoothing = DEFAULT_PROP_POS_ENABLE_POSE_SMOOTHING;
    src->set_floor_as_origin = DEFAULT_PROP_POS_SET_FLOOR_AS_ORIGIN;
    src->set_gravity_as_origin = DEFAULT_PROP_POS_SET_GRAVITY_AS_ORIGIN;
    src->depth_min_range = DEFAULT_PROP_POS_DEPTH_MIN_RANGE;
    src->init_pose_x = DEFAULT_PROP_POS_INIT_X;
    src->init_pose_y = DEFAULT_PROP_POS_INIT_Y;
    src->init_pose_z = DEFAULT_PROP_POS_INIT_Z;
    src->init_orient_roll = DEFAULT_PROP_POS_INIT_ROLL;
    src->init_orient_pitch = DEFAULT_PROP_POS_INIT_PITCH;
    src->init_orient_yaw = DEFAULT_PROP_POS_INIT_YAW;
    src->pos_trk_mode = DEFAULT_PROP_PT_MODE;

    src->object_detection = DEFAULT_PROP_OD_ENABLE;
    src->od_instance_id = DEFAULT_PROP_OD_INSTANCE_ID;
    src->od_custom_onnx_file = g_string_new(DEFAULT_PROP_OD_CUSTOM_ONNX_FILE);
    src->od_custom_onnx_dynamic_input_shape_w = DEFAULT_PROP_OD_CUSTOM_ONNX_DYNAMIC_INPUT_SHAPE_W;
    src->od_custom_onnx_dynamic_input_shape_h = DEFAULT_PROP_OD_CUSTOM_ONNX_DYNAMIC_INPUT_SHAPE_H;
    src->od_enable_tracking = DEFAULT_PROP_OD_TRACKING;
    src->od_enable_segm_output = DEFAULT_PROP_OD_SEGM;
    src->od_detection_model = DEFAULT_PROP_OD_MODEL;
    src->od_filter_mode = DEFAULT_PROP_OD_FILTER_MODE;
    src->od_det_conf = DEFAULT_PROP_OD_CONFIDENCE;
    src->od_max_range = DEFAULT_PROP_OD_MAX_RANGE;
    src->od_prediction_timeout_s = DEFAULT_PROP_OD_PREDICTION_TIMEOUT_S;
    src->od_allow_reduced_precision_inference = DEFAULT_PROP_OD_ALLOW_REDUCED_PRECISION_INFERENCE;
    src->od_person_conf = DEFAULT_PROP_OD_PEOPLE_CONF;
    src->od_vehicle_conf = DEFAULT_PROP_OD_VEHICLE_CONF;
    src->od_animal_conf = DEFAULT_PROP_OD_ANIMAL_CONF;
    src->od_bag_conf = DEFAULT_PROP_OD_BAG_CONF;
    src->od_electronics_conf = DEFAULT_PROP_OD_ELECTRONICS_CONF;
    src->od_fruit_vegetable_conf = DEFAULT_PROP_OD_FRUIT_VEGETABLES_CONF;
    src->od_sport_conf = DEFAULT_PROP_OD_SPORT_CONF;

    src->body_tracking = DEFAULT_PROP_BT_ENABLE;
    src->bt_enable_segm_output = DEFAULT_PROP_BT_SEGM;
    src->bt_model = DEFAULT_PROP_BT_MODEL;
    src->bt_format = DEFAULT_PROP_BT_FORMAT;
    src->bt_reduce_precision = DEFAULT_PROP_BT_ALLOW_REDUCED_PRECISION_INFERENCE;
    src->bt_max_range = DEFAULT_PROP_BT_MAX_RANGE;
    src->bt_kp_sel = DEFAULT_PROP_BT_KP_SELECT;
    src->bt_fitting = DEFAULT_PROP_BT_BODY_FITTING;
    src->bt_enable_trk = DEFAULT_PROP_BT_TRACKING;
    src->bt_pred_timeout = DEFAULT_PROP_BT_PREDICTION_TIMEOUT_S;
    src->bt_rt_det_conf = DEFAULT_PROP_BT_CONFIDENCE;
    src->bt_rt_min_kp_thresh = DEFAULT_PROP_BT_MIN_KP_THRESH;
    src->bt_rt_skel_smoothing = DEFAULT_PROP_BT_SMOOTHING;

    src->brightness = DEFAULT_PROP_BRIGHTNESS;
    src->contrast = DEFAULT_PROP_CONTRAST;
    src->hue = DEFAULT_PROP_HUE;
    src->saturation = DEFAULT_PROP_SATURATION;
    src->sharpness = DEFAULT_PROP_SHARPNESS;
    src->gamma = DEFAULT_PROP_GAMMA;
    src->gain = DEFAULT_PROP_GAIN;
    src->exposure = DEFAULT_PROP_EXPOSURE;
    src->exposureRange_min = DEFAULT_PROP_EXPOSURE_RANGE_MIN;
    src->exposureRange_max = DEFAULT_PROP_EXPOSURE_RANGE_MAX;
    src->aec_agc = DEFAULT_PROP_AEC_AGC;
    src->aec_agc_roi_x = DEFAULT_PROP_AEC_AGC_ROI_X;
    src->aec_agc_roi_y = DEFAULT_PROP_AEC_AGC_ROI_Y;
    src->aec_agc_roi_w = DEFAULT_PROP_AEC_AGC_ROI_W;
    src->aec_agc_roi_h = DEFAULT_PROP_AEC_AGC_ROI_H;
    src->aec_agc_roi_side = DEFAULT_PROP_AEC_AGC_ROI_SIDE;
    src->whitebalance_temperature = DEFAULT_PROP_WHITEBALANCE;
    src->whitebalance_temperature_auto = DEFAULT_PROP_WHITEBALANCE_AUTO;
    src->led_status = DEFAULT_PROP_LEDSTATUS;

    // SVO Recording
    src->svo_rec_enable = DEFAULT_PROP_SVO_REC_ENABLE;
    src->svo_rec_filename = g_string_new(DEFAULT_PROP_SVO_REC_FILENAME);
    src->svo_rec_compression = DEFAULT_PROP_SVO_REC_COMPRESSION;
    src->svo_rec_active = FALSE;
    // <---- Parameters initialization

    src->stop_requested = FALSE;
    src->caps = NULL;

    gst_zedsrc_reset(src);
}

void gst_zedsrc_set_property(GObject *object, guint property_id, const GValue *value,
                             GParamSpec *pspec) {
    GstZedSrc *src;
    const gchar *str;

    src = GST_ZED_SRC(object);

    GST_TRACE_OBJECT(src, "gst_zedsrc_set_property");

    switch (property_id) {
    case PROP_CAM_RES:
        src->camera_resolution = g_value_get_enum(value);
        break;
    case PROP_CAM_FPS:
        src->camera_fps = g_value_get_enum(value);
        break;
    case PROP_SDK_VERBOSE:
        src->sdk_verbose = g_value_get_int(value);
        break;
    case PROP_CAM_FLIP:
        src->camera_image_flip = g_value_get_enum(value);
        break;
    case PROP_CAM_ID:
        src->camera_id = g_value_get_int(value);
        break;
    case PROP_CAM_SN:
        src->camera_sn = g_value_get_int64(value);
        break;
    case PROP_SVO_FILE:
        str = g_value_get_string(value);
        g_string_assign(src->svo_file, str);
        break;
    case PROP_OPENCV_CALIB_FILE:
        str = g_value_get_string(value);
        g_string_assign(src->opencv_calibration_file, str);
        break;
    case PROP_STREAM_IP:
        str = g_value_get_string(value);
        g_string_assign(src->stream_ip, str);
        break;
    case PROP_STREAM_PORT:
        src->stream_port = g_value_get_int(value);
        break;
    case PROP_STREAM_TYPE:
        src->stream_type = g_value_get_enum(value);
        break;
    case PROP_DEPTH_MIN:
        src->depth_min_dist = g_value_get_float(value);
        break;
    case PROP_DEPTH_MAX:
        src->depth_max_dist = g_value_get_float(value);
        break;
    case PROP_DEPTH_MODE:
        src->depth_mode = g_value_get_enum(value);
        break;
    case PROP_DIS_SELF_CALIB:
        src->camera_disable_self_calib = g_value_get_boolean(value);
        break;
    case PROP_DEPTH_STAB:
        src->depth_stabilization = g_value_get_int(value);
        break;
    case PROP_COORD_SYS:
        src->coord_sys = g_value_get_enum(value);
        break;
        /*case PROP_RIGHT_DEPTH_ENABLE:
        src->enable_right_side_measure =  g_value_get_boolean(value);
        break;*/
    case PROP_CONFIDENCE_THRESH:
        src->confidence_threshold = g_value_get_int(value);
        break;
    case PROP_TEXTURE_CONF_THRESH:
        src->texture_confidence_threshold = g_value_get_int(value);
        break;
    case PROP_3D_REF_FRAME:
        src->measure3D_reference_frame = g_value_get_enum(value);
        break;
    case PROP_FILL_MODE:
        src->fill_mode = g_value_get_boolean(value);
        break;
    case PROP_ROI:
        src->roi = g_value_get_boolean(value);
        break;
    case PROP_ROI_X:
        src->roi_x = g_value_get_int(value);
        break;
    case PROP_ROI_Y:
        src->roi_y = g_value_get_int(value);
        break;
    case PROP_ROI_W:
        src->roi_w = g_value_get_int(value);
        break;
    case PROP_ROI_H:
        src->roi_h = g_value_get_int(value);
        break;
    case PROP_POS_TRACKING:
        src->pos_tracking = g_value_get_boolean(value);
        break;
    case PROP_POS_MODE:
        src->pos_trk_mode = g_value_get_enum(value);
        break;
    case PROP_CAMERA_STATIC:
        src->camera_static = g_value_get_boolean(value);
        break;
    case PROP_POS_AREA_FILE_PATH:
        str = g_value_get_string(value);
        g_string_assign(src->area_file_path, str);
        break;
    case PROP_POS_ENABLE_AREA_MEMORY:
        src->enable_area_memory = g_value_get_boolean(value);
        break;
    case PROP_POS_ENABLE_IMU_FUSION:
        src->enable_imu_fusion = g_value_get_boolean(value);
        break;
    case PROP_POS_SET_FLOOR_AS_ORIGIN:
        src->set_floor_as_origin = g_value_get_boolean(value);
        break;
    case PROP_POS_ENABLE_POSE_SMOOTHING:
        src->enable_pose_smoothing = g_value_get_boolean(value);
        break;
    case PROP_POS_SET_GRAVITY_AS_ORIGIN:
        src->set_gravity_as_origin = g_value_get_boolean(value);
        break;
    case PROP_POS_DEPTH_MIN_RANGE:
        src->depth_min_range = g_value_get_float(value);
        break;
    case PROP_POS_INIT_X:
        src->init_pose_x = g_value_get_float(value);
        break;
    case PROP_POS_INIT_Y:
        src->init_pose_y = g_value_get_float(value);
        break;
    case PROP_POS_INIT_Z:
        src->init_pose_z = g_value_get_float(value);
        break;
    case PROP_POS_INIT_ROLL:
        src->init_orient_roll = g_value_get_float(value);
        break;
    case PROP_POS_INIT_PITCH:
        src->init_orient_pitch = g_value_get_float(value);
        break;
    case PROP_POS_INIT_YAW:
        src->init_orient_yaw = g_value_get_float(value);
        break;
    case PROP_OD_ENABLE:
        src->object_detection = g_value_get_boolean(value);
        break;
    case PROP_OD_TRACKING:
        src->od_enable_tracking = g_value_get_boolean(value);
        break;
        /*case PROP_OD_SEGM:
        src->od_enable_segm_output = g_value_get_boolean(value);
        break;*/
    case PROP_OD_DET_MODEL:
        src->od_detection_model = g_value_get_enum(value);
        break;
    case PROP_OD_FILTER_MODE:
        src->od_filter_mode = g_value_get_enum(value);
        break;
    case PROP_OD_CONFIDENCE:
        src->od_det_conf = g_value_get_float(value);
        break;
    case PROP_OD_MAX_RANGE:
        src->od_max_range = g_value_get_float(value);
        break;
    case PROP_OD_PREDICTION_TIMEOUT_S:
        src->od_prediction_timeout_s = g_value_get_float(value);
        break;
    case PROP_OD_ALLOW_REDUCED_PRECISION_INFERENCE:
        src->od_allow_reduced_precision_inference = g_value_get_boolean(value);
        break;
    case PROP_OD_PERSON_CONF:
        src->od_person_conf = g_value_get_float(value);
        break;
    case PROP_OD_VEHICLE_CONF:
        src->od_vehicle_conf = g_value_get_float(value);
        break;
    case PROP_OD_ANIMAL_CONF:
        src->od_animal_conf = g_value_get_float(value);
        break;
    case PROP_OD_BAG_CONF:
        src->od_bag_conf = g_value_get_float(value);
        break;
    case PROP_OD_ELECTRONICS_CONF:
        src->od_electronics_conf = g_value_get_float(value);
        break;
    case PROP_OD_FRUIT_VEGETABLES_CONF:
        src->od_fruit_vegetable_conf = g_value_get_float(value);
        break;
    case PROP_OD_SPORT_CONF:
        src->od_sport_conf = g_value_get_float(value);
        break;
    case PROP_BT_ENABLE:
        src->body_tracking = g_value_get_boolean(value);
        break;
    case PROP_BT_SEGM:
        src->bt_enable_segm_output = g_value_get_boolean(value);
        break;
    case PROP_BT_MODEL:
        src->bt_model = g_value_get_enum(value);
        break;
    case PROP_BT_FORMAT:
        src->bt_format = g_value_get_enum(value);
        break;
    case PROP_BT_ALLOW_REDUCED_PRECISION_INFERENCE:
        src->bt_reduce_precision = g_value_get_boolean(value);
        break;
    case PROP_BT_MAX_RANGE:
        src->bt_max_range = g_value_get_float(value);
        break;
    case PROP_BT_KP_SELECT:
        src->bt_kp_sel = g_value_get_enum(value);
        break;
    case PROP_BT_BODY_FITTING:
        src->bt_fitting = g_value_get_boolean(value);
        break;
    case PROP_BT_TRACKING:
        src->bt_enable_trk = g_value_get_boolean(value);
        break;
    case PROP_BT_PREDICTION_TIMEOUT_S:
        src->bt_pred_timeout = g_value_get_float(value);
        break;
    case PROP_BT_CONFIDENCE:
        src->bt_rt_det_conf = g_value_get_float(value);
        break;
    case PROP_BT_MIN_KP_THRESH:
        src->bt_rt_min_kp_thresh = g_value_get_int(value);
        break;
    case PROP_BT_SMOOTHING:
        src->bt_rt_skel_smoothing = g_value_get_float(value);
        break;
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
        src->exposure_gain_updated = TRUE;
        break;
    case PROP_EXPOSURE:
        src->exposure = g_value_get_int(value);
        src->exposure_gain_updated = TRUE;
        break;
    case PROP_EXPOSURE_RANGE_MIN:
        src->exposureRange_min = g_value_get_int(value);
        break;
    case PROP_EXPOSURE_RANGE_MAX:
        src->exposureRange_max = g_value_get_int(value);
        break;
    case PROP_AEC_AGC:
        src->aec_agc = g_value_get_boolean(value);
        src->exposure_gain_updated = TRUE;
        break;
    case PROP_AEC_AGC_ROI_X:
        src->aec_agc_roi_x = g_value_get_int(value);
        src->exposure_gain_updated = TRUE;
        break;
    case PROP_AEC_AGC_ROI_Y:
        src->aec_agc_roi_y = g_value_get_int(value);
        src->exposure_gain_updated = TRUE;
        break;
    case PROP_AEC_AGC_ROI_W:
        src->aec_agc_roi_w = g_value_get_int(value);
        src->exposure_gain_updated = TRUE;
        break;
    case PROP_AEC_AGC_ROI_H:
        src->aec_agc_roi_h = g_value_get_int(value);
        src->exposure_gain_updated = TRUE;
        break;
    case PROP_AEC_AGC_ROI_SIDE:
        src->aec_agc_roi_side = g_value_get_enum(value);
        src->exposure_gain_updated = TRUE;
        break;
    case PROP_WHITEBALANCE:
        src->whitebalance_temperature = g_value_get_int(value);
        break;
    case PROP_WHITEBALANCE_AUTO:
        src->whitebalance_temperature_auto = g_value_get_boolean(value);
        break;
    case PROP_LEDSTATUS:
        src->led_status = g_value_get_boolean(value);
        break;
    case PROP_SVO_REC_ENABLE:
        src->svo_rec_enable = g_value_get_boolean(value);
        // Handle runtime recording toggle
        if (src->is_started) {
            if (src->svo_rec_enable && !src->svo_rec_active) {
                // Start recording
                if (src->svo_rec_filename && src->svo_rec_filename->len > 0) {
                    sl::RecordingParameters rec_params;
                    rec_params.video_filename.set(src->svo_rec_filename->str);
                    rec_params.compression_mode =
                        static_cast<sl::SVO_COMPRESSION_MODE>(src->svo_rec_compression);
                    sl::ERROR_CODE err = src->zed.enableRecording(rec_params);
                    if (err == sl::ERROR_CODE::SUCCESS) {
                        src->svo_rec_active = TRUE;
                        GST_INFO_OBJECT(src, "SVO recording started: %s",
                                        src->svo_rec_filename->str);
                    } else {
                        GST_WARNING_OBJECT(src, "Failed to start SVO recording: %s",
                                           sl::toString(err).c_str());
                        src->svo_rec_enable = FALSE;
                    }
                } else {
                    GST_WARNING_OBJECT(src, "Cannot start SVO recording: filename not set");
                    src->svo_rec_enable = FALSE;
                }
            } else if (!src->svo_rec_enable && src->svo_rec_active) {
                // Stop recording
                src->zed.disableRecording();
                src->svo_rec_active = FALSE;
                GST_INFO_OBJECT(src, "SVO recording stopped");
            }
        }
        break;
    case PROP_SVO_REC_FILENAME:
        str = g_value_get_string(value);
        g_string_assign(src->svo_rec_filename, str);
        break;
    case PROP_SVO_REC_COMPRESSION:
        src->svo_rec_compression = g_value_get_enum(value);
        break;
    case PROP_SVO_REAL_TIME:
        src->svo_real_time = g_value_get_boolean(value);
        break;
    case PROP_SDK_GPU_ID:
        src->sdk_gpu_id = g_value_get_int(value);
        break;
    case PROP_SDK_VERBOSE_LOG_FILE:
        str = g_value_get_string(value);
        g_string_assign(src->sdk_verbose_log_file, str);
        break;
    case PROP_OPTIONAL_SETTINGS_PATH:
        str = g_value_get_string(value);
        g_string_assign(src->optional_settings_path, str);
        break;
    case PROP_SENSORS_REQUIRED:
        src->sensors_required = g_value_get_boolean(value);
        break;
    case PROP_ENABLE_IMAGE_ENHANCEMENT:
        src->enable_image_enhancement = g_value_get_boolean(value);
        break;
    case PROP_OPEN_TIMEOUT_SEC:
        src->open_timeout_sec = g_value_get_float(value);
        break;
    case PROP_ASYNC_GRAB_CAMERA_RECOVERY:
        src->async_grab_camera_recovery = g_value_get_boolean(value);
        break;
    case PROP_GRAB_COMPUTE_CAPPING_FPS:
        src->grab_compute_capping_fps = g_value_get_float(value);
        break;
    case PROP_ENABLE_IMAGE_VALIDITY_CHECK:
        src->enable_image_validity_check = g_value_get_boolean(value);
        break;
    case PROP_ASYNC_IMAGE_RETRIEVAL:
        src->async_image_retrieval = g_value_get_boolean(value);
        break;
    case PROP_MAX_WORKING_RES_W:
        src->max_working_res_w = g_value_get_int(value);
        break;
    case PROP_MAX_WORKING_RES_H:
        src->max_working_res_h = g_value_get_int(value);
        break;
    case PROP_REMOVE_SATURATED_AREAS:
        src->remove_saturated_areas = g_value_get_boolean(value);
        break;
    case PROP_OD_INSTANCE_ID:
        src->od_instance_id = g_value_get_uint(value);
        break;
    case PROP_OD_CUSTOM_ONNX_FILE:
        str = g_value_get_string(value);
        g_string_assign(src->od_custom_onnx_file, str);
        break;
    case PROP_OD_CUSTOM_ONNX_DYNAMIC_INPUT_SHAPE_W:
        src->od_custom_onnx_dynamic_input_shape_w = g_value_get_int(value);
        break;
    case PROP_OD_CUSTOM_ONNX_DYNAMIC_INPUT_SHAPE_H:
        src->od_custom_onnx_dynamic_input_shape_h = g_value_get_int(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_zedsrc_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    GstZedSrc *src;

    g_return_if_fail(GST_IS_ZED_SRC(object));
    src = GST_ZED_SRC(object);

    GST_TRACE_OBJECT(src, "gst_zedsrc_get_property");

    switch (property_id) {
    case PROP_CAM_RES:
        g_value_set_enum(value, src->camera_resolution);
        break;
    case PROP_CAM_FPS:
        g_value_set_enum(value, src->camera_fps);
        break;
    case PROP_SDK_VERBOSE:
        g_value_set_int(value, src->sdk_verbose);
        break;
    case PROP_CAM_FLIP:
        g_value_set_enum(value, src->camera_image_flip);
        break;
    case PROP_CAM_ID:
        g_value_set_int(value, src->camera_id);
        break;
    case PROP_CAM_SN:
        g_value_set_int64(value, src->camera_id);
        break;
    case PROP_SVO_FILE:
        g_value_set_string(value, src->svo_file->str);
        break;
    case PROP_OPENCV_CALIB_FILE:
        g_value_set_string(value, src->opencv_calibration_file->str);
        break;
    case PROP_STREAM_IP:
        g_value_set_string(value, src->stream_ip->str);
        break;
    case PROP_STREAM_PORT:
        g_value_set_int(value, src->stream_port);
        break;
    case PROP_STREAM_TYPE:
        g_value_set_enum(value, src->stream_type);
        break;
    case PROP_DEPTH_MIN:
        g_value_set_float(value, src->depth_min_dist);
        break;
    case PROP_DEPTH_MAX:
        g_value_set_float(value, src->depth_max_dist);
        break;
    case PROP_DEPTH_MODE:
        g_value_set_enum(value, src->depth_mode);
        break;
    case PROP_COORD_SYS:
        g_value_set_enum(value, src->coord_sys);
        break;
    case PROP_DIS_SELF_CALIB:
        g_value_set_boolean(value, src->camera_disable_self_calib);
        break;
        /*case PROP_RIGHT_DEPTH_ENABLE:
        g_value_set_boolean( value, src->enable_right_side_measure);
        break;*/
    case PROP_DEPTH_STAB:
        g_value_set_int(value, src->depth_stabilization);
        break;
    case PROP_CONFIDENCE_THRESH:
        g_value_set_int(value, src->confidence_threshold);
        break;
    case PROP_TEXTURE_CONF_THRESH:
        g_value_set_int(value, src->texture_confidence_threshold);
        break;
    case PROP_3D_REF_FRAME:
        g_value_set_enum(value, src->measure3D_reference_frame);
        break;
    case PROP_FILL_MODE:
        g_value_set_boolean(value, src->fill_mode);
        break;
    case PROP_ROI:
        g_value_set_boolean(value, src->roi);
        break;
    case PROP_ROI_X:
        g_value_set_int(value, src->roi_x);
        break;
    case PROP_ROI_Y:
        g_value_set_int(value, src->roi_y);
        break;
    case PROP_ROI_W:
        g_value_set_int(value, src->roi_w);
        break;
    case PROP_ROI_H:
        g_value_set_int(value, src->roi_h);
        break;
    case PROP_POS_TRACKING:
        g_value_set_boolean(value, src->pos_tracking);
        break;
    case PROP_POS_MODE:
        g_value_set_enum(value, src->pos_trk_mode);
        break;
    case PROP_CAMERA_STATIC:
        g_value_set_boolean(value, src->camera_static);
        break;
    case PROP_POS_AREA_FILE_PATH:
        g_value_set_string(value, src->area_file_path->str);
        break;
    case PROP_POS_ENABLE_AREA_MEMORY:
        g_value_set_boolean(value, src->enable_area_memory);
        break;
    case PROP_POS_ENABLE_IMU_FUSION:
        g_value_set_boolean(value, src->enable_imu_fusion);
        break;
    case PROP_POS_SET_FLOOR_AS_ORIGIN:
        g_value_set_boolean(value, src->set_floor_as_origin);
        break;
    case PROP_POS_ENABLE_POSE_SMOOTHING:
        g_value_set_boolean(value, src->enable_pose_smoothing);
        break;
    case PROP_POS_SET_GRAVITY_AS_ORIGIN:
        g_value_set_boolean(value, src->set_gravity_as_origin);
        break;
    case PROP_POS_DEPTH_MIN_RANGE:
        g_value_set_float(value, src->depth_min_range);
        break;
    case PROP_POS_INIT_X:
        g_value_set_float(value, src->init_pose_x);
        break;
    case PROP_POS_INIT_Y:
        g_value_set_float(value, src->init_pose_y);
        break;
    case PROP_POS_INIT_Z:
        g_value_set_float(value, src->init_pose_z);
        break;
    case PROP_POS_INIT_ROLL:
        g_value_set_float(value, src->init_orient_roll);
        break;
    case PROP_POS_INIT_PITCH:
        g_value_set_float(value, src->init_orient_pitch);
        break;
    case PROP_POS_INIT_YAW:
        g_value_set_float(value, src->init_orient_yaw);
        break;
    case PROP_OD_ENABLE:
        g_value_set_boolean(value, src->object_detection);
        break;
    case PROP_OD_TRACKING:
        g_value_set_boolean(value, src->od_enable_tracking);
        break;
        /*case PROP_OD_SEGM:
        g_value_set_boolean( value, src->od_enable_segm_output );
        break;*/
    case PROP_OD_DET_MODEL:
        g_value_set_enum(value, src->od_detection_model);
        break;
    case PROP_OD_FILTER_MODE:
        g_value_set_enum(value, src->od_filter_mode);
        break;
    case PROP_OD_CONFIDENCE:
        g_value_set_float(value, src->od_det_conf);
        break;
    case PROP_OD_MAX_RANGE:
        g_value_set_float(value, src->od_max_range);
        break;
    case PROP_OD_PREDICTION_TIMEOUT_S:
        g_value_set_float(value, src->od_prediction_timeout_s);
        break;
    case PROP_OD_ALLOW_REDUCED_PRECISION_INFERENCE:
        g_value_set_boolean(value, src->od_allow_reduced_precision_inference);
        break;
    case PROP_OD_PERSON_CONF:
        g_value_set_float(value, src->od_person_conf);
        break;
    case PROP_OD_VEHICLE_CONF:
        g_value_set_float(value, src->od_vehicle_conf);
        break;
    case PROP_OD_ANIMAL_CONF:
        g_value_set_float(value, src->od_animal_conf);
        break;
    case PROP_OD_BAG_CONF:
        g_value_set_float(value, src->od_bag_conf);
        break;
    case PROP_OD_ELECTRONICS_CONF:
        g_value_set_float(value, src->od_electronics_conf);
        break;
    case PROP_OD_FRUIT_VEGETABLES_CONF:
        g_value_set_float(value, src->od_fruit_vegetable_conf);
        break;
    case PROP_OD_SPORT_CONF:
        g_value_set_float(value, src->od_sport_conf);
        break;
    case PROP_BT_ENABLE:
        g_value_set_boolean(value, src->body_tracking);
        break;
    /*case PROP_BT_SEGM:
    g_value_set_float(value, src->bt_enable_segm_output);
    break;*/
    case PROP_BT_MODEL:
        g_value_set_enum(value, src->bt_model);
        break;
    case PROP_BT_FORMAT:
        g_value_set_enum(value, src->bt_format);
        break;
    case PROP_BT_ALLOW_REDUCED_PRECISION_INFERENCE:
        g_value_set_boolean(value, src->bt_reduce_precision);
        break;
    case PROP_BT_MAX_RANGE:
        g_value_set_float(value, src->bt_max_range);
        break;
    case PROP_BT_KP_SELECT:
        g_value_set_float(value, src->bt_kp_sel);
        break;
    case PROP_BT_BODY_FITTING:
        g_value_set_boolean(value, src->bt_fitting);
        break;
    case PROP_BT_TRACKING:
        g_value_set_boolean(value, src->bt_enable_trk);
        break;
    case PROP_BT_PREDICTION_TIMEOUT_S:
        g_value_set_float(value, src->bt_pred_timeout);
        break;
    case PROP_BT_CONFIDENCE:
        g_value_set_float(value, src->bt_rt_det_conf);
        break;
    case PROP_BT_MIN_KP_THRESH:
        g_value_set_int(value, src->bt_rt_min_kp_thresh);
        break;
    case PROP_BT_SMOOTHING:
        g_value_set_float(value, src->bt_rt_skel_smoothing);
        break;
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
    case PROP_EXPOSURE_RANGE_MIN:
        g_value_set_int(value, src->exposureRange_min);
        break;
    case PROP_EXPOSURE_RANGE_MAX:
        g_value_set_int(value, src->exposureRange_max);
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
    case PROP_LEDSTATUS:
        g_value_set_boolean(value, src->led_status);
        break;
    case PROP_SVO_REC_ENABLE:
        g_value_set_boolean(value, src->svo_rec_active);   // Return actual state
        break;
    case PROP_SVO_REC_FILENAME:
        g_value_set_string(value, src->svo_rec_filename ? src->svo_rec_filename->str : "");
        break;
    case PROP_SVO_REC_COMPRESSION:
        g_value_set_enum(value, src->svo_rec_compression);
        break;
    case PROP_SVO_REAL_TIME:
        g_value_set_boolean(value, src->svo_real_time);
        break;
    case PROP_SDK_GPU_ID:
        g_value_set_int(value, src->sdk_gpu_id);
        break;
    case PROP_SDK_VERBOSE_LOG_FILE:
        g_value_set_string(value, src->sdk_verbose_log_file->str);
        break;
    case PROP_OPTIONAL_SETTINGS_PATH:
        g_value_set_string(value, src->optional_settings_path->str);
        break;
    case PROP_SENSORS_REQUIRED:
        g_value_set_boolean(value, src->sensors_required);
        break;
    case PROP_ENABLE_IMAGE_ENHANCEMENT:
        g_value_set_boolean(value, src->enable_image_enhancement);
        break;
    case PROP_OPEN_TIMEOUT_SEC:
        g_value_set_float(value, src->open_timeout_sec);
        break;
    case PROP_ASYNC_GRAB_CAMERA_RECOVERY:
        g_value_set_boolean(value, src->async_grab_camera_recovery);
        break;
    case PROP_GRAB_COMPUTE_CAPPING_FPS:
        g_value_set_float(value, src->grab_compute_capping_fps);
        break;
    case PROP_ENABLE_IMAGE_VALIDITY_CHECK:
        g_value_set_boolean(value, src->enable_image_validity_check);
        break;
    case PROP_ASYNC_IMAGE_RETRIEVAL:
        g_value_set_boolean(value, src->async_image_retrieval);
        break;
    case PROP_MAX_WORKING_RES_W:
        g_value_set_int(value, src->max_working_res_w);
        break;
    case PROP_MAX_WORKING_RES_H:
        g_value_set_int(value, src->max_working_res_h);
        break;
    case PROP_REMOVE_SATURATED_AREAS:
        g_value_set_boolean(value, src->remove_saturated_areas);
        break;
    case PROP_OD_INSTANCE_ID:
        g_value_set_uint(value, src->od_instance_id);
        break;
    case PROP_OD_CUSTOM_ONNX_FILE:
        g_value_set_string(value, src->od_custom_onnx_file->str);
        break;
    case PROP_OD_CUSTOM_ONNX_DYNAMIC_INPUT_SHAPE_W:
        g_value_set_int(value, src->od_custom_onnx_dynamic_input_shape_w);
        break;
    case PROP_OD_CUSTOM_ONNX_DYNAMIC_INPUT_SHAPE_H:
        g_value_set_int(value, src->od_custom_onnx_dynamic_input_shape_h);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_zedsrc_dispose(GObject *object) {
    GstZedSrc *src;

    g_return_if_fail(GST_IS_ZED_SRC(object));
    src = GST_ZED_SRC(object);

    GST_TRACE_OBJECT(src, "gst_zedsrc_dispose");

    /* clean up as possible.  may be called multiple times */

    G_OBJECT_CLASS(gst_zedsrc_parent_class)->dispose(object);
}

void gst_zedsrc_finalize(GObject *object) {
    GstZedSrc *src;

    g_return_if_fail(GST_IS_ZED_SRC(object));
    src = GST_ZED_SRC(object);

    GST_TRACE_OBJECT(src, "gst_zedsrc_finalize");

    /* clean up object here */
    if (src->caps) {
        gst_caps_unref(src->caps);
        src->caps = NULL;
    }

    if (src->svo_file) {
        g_string_free(src->svo_file, TRUE);
    }
    if (src->opencv_calibration_file) {
        g_string_free(src->opencv_calibration_file, TRUE);
    }
    if (src->stream_ip) {
        g_string_free(src->stream_ip, TRUE);
    }
    if (src->sdk_verbose_log_file) {
        g_string_free(src->sdk_verbose_log_file, TRUE);
    }
    if (src->optional_settings_path) {
        g_string_free(src->optional_settings_path, TRUE);
    }
    if (src->area_file_path) {
        g_string_free(src->area_file_path, TRUE);
    }
    if (src->od_custom_onnx_file) {
        g_string_free(src->od_custom_onnx_file, TRUE);
    }
    if (src->svo_rec_filename) {
        g_string_free(src->svo_rec_filename, TRUE);
    }

    G_OBJECT_CLASS(gst_zedsrc_parent_class)->finalize(object);
}

static gboolean gst_zedsrc_calculate_caps(GstZedSrc *src) {
    GST_TRACE_OBJECT(src, "gst_zedsrc_calculate_caps");

    guint32 width, height;
    gint fps;
    GstVideoInfo vinfo;
    GstVideoFormat format = GST_VIDEO_FORMAT_BGRA;

    if (src->stream_type == GST_ZEDSRC_DEPTH_16) {
        format = GST_VIDEO_FORMAT_GRAY16_LE;
    }
#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
    else if (src->stream_type == GST_ZEDSRC_RAW_NV12 || src->stream_type == GST_ZEDSRC_RAW_NV12_STEREO) {
        format = GST_VIDEO_FORMAT_NV12;
    }
#endif

    sl::CameraInformation cam_info = src->zed.getCameraInformation();

    width = cam_info.camera_configuration.resolution.width;
    height = cam_info.camera_configuration.resolution.height;

    if (src->stream_type == GST_ZEDSRC_LEFT_RIGHT || src->stream_type == GST_ZEDSRC_LEFT_DEPTH) {
        height *= 2;
    }
#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
    else if (src->stream_type == GST_ZEDSRC_RAW_NV12_STEREO) {
        width *= 2;  // Side-by-side stereo
    }
#endif

    fps = static_cast<gint>(cam_info.camera_configuration.fps);

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

#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
        // Add memory:NVMM feature for zero-copy NV12 modes
        if (src->stream_type == GST_ZEDSRC_RAW_NV12 || src->stream_type == GST_ZEDSRC_RAW_NV12_STEREO) {
            GstCapsFeatures *features = gst_caps_features_new("memory:NVMM", NULL);
            gst_caps_set_features(src->caps, 0, features);
            GST_INFO_OBJECT(src, "Added memory:NVMM feature for zero-copy");
        }
#endif
    }

    gst_base_src_set_blocksize(GST_BASE_SRC(src), src->out_framesize);
    gst_base_src_set_caps(GST_BASE_SRC(src), src->caps);
    GST_DEBUG_OBJECT(src, "Created caps %" GST_PTR_FORMAT, src->caps);

    return TRUE;
}

static gboolean gst_zedsrc_start(GstBaseSrc *bsrc) {
    GstZedSrc *src = GST_ZED_SRC(bsrc);
    sl::ERROR_CODE ret;

#if (ZED_SDK_MAJOR_VERSION != 5)
    GST_ELEMENT_ERROR(src, LIBRARY, FAILED,
                      ("Wrong ZED SDK version. SDK v5.0 EA or newer required "), (NULL));
    return FALSE;
#endif

    GST_TRACE_OBJECT(src, "gst_zedsrc_calculate_caps");

    // ----> Set init parameters
    sl::InitParameters init_params;

    GST_INFO("CAMERA INITIALIZATION PARAMETERS");

    switch (src->camera_resolution) {
    case GST_ZEDSRC_HD2K:
        init_params.camera_resolution = sl::RESOLUTION::HD2K;
        break;
    case GST_ZEDSRC_HD1080:
        init_params.camera_resolution = sl::RESOLUTION::HD1080;
        break;
    case GST_ZEDSRC_HD1200:
        init_params.camera_resolution = sl::RESOLUTION::HD1200;
        break;
    case GST_ZEDSRC_HD720:
        init_params.camera_resolution = sl::RESOLUTION::HD720;
        break;
    case GST_ZEDSRC_SVGA:
        init_params.camera_resolution = sl::RESOLUTION::SVGA;
        break;
    case GST_ZEDSRC_VGA:
        init_params.camera_resolution = sl::RESOLUTION::VGA;
        break;
    case GST_ZEDSRC_AUTO_RES:
        init_params.camera_resolution = sl::RESOLUTION::AUTO;
        break;
    default:
        GST_ELEMENT_ERROR(src, RESOURCE, NOT_FOUND, ("Failed to set camera resolution"), (NULL));
        return FALSE;
    }
    GST_INFO(" * Camera resolution: %s", sl::toString(init_params.camera_resolution).c_str());
    init_params.camera_fps = src->camera_fps;
    GST_INFO(" * Camera FPS: %d", init_params.camera_fps);
    init_params.sdk_verbose = src->sdk_verbose;
    GST_INFO(" * SDK verbose: %d", init_params.sdk_verbose);
    init_params.camera_image_flip = src->camera_image_flip;
    GST_INFO(" * Camera flipped: %s",
             sl::toString(static_cast<sl::FLIP_MODE>(init_params.camera_image_flip)).c_str());

    init_params.depth_mode = static_cast<sl::DEPTH_MODE>(src->depth_mode);
    if ((src->object_detection || src->body_tracking) &&
        init_params.depth_mode == sl::DEPTH_MODE::NONE) {
        init_params.depth_mode = sl::DEPTH_MODE::NEURAL;
        src->depth_mode = static_cast<gint>(init_params.depth_mode);

        GST_WARNING_OBJECT(
            src, "Object detection requires DEPTH_MODE!=NONE. Depth mode value forced to NEURAL");

        if (!src->pos_tracking) {
            src->pos_tracking = TRUE;
            GST_WARNING_OBJECT(
                src,
                "Object detection requires Positional Tracking to be active. Positional Tracking "
                "automatically activated");
        }
    }
    if (src->pos_tracking && init_params.depth_mode == sl::DEPTH_MODE::NONE) {
        init_params.depth_mode = sl::DEPTH_MODE::NEURAL;
        src->depth_mode = static_cast<gint>(init_params.depth_mode);

        GST_WARNING_OBJECT(
            src,
            "Positional tracking requires DEPTH_MODE!=NONE. Depth mode value forced to NEURAL");
    }
    if ((src->stream_type == GST_ZEDSRC_LEFT_DEPTH || src->stream_type == GST_ZEDSRC_DEPTH_16) &&
        init_params.depth_mode == sl::DEPTH_MODE::NONE) {
        init_params.depth_mode = sl::DEPTH_MODE::NEURAL;
        src->depth_mode = static_cast<gint>(init_params.depth_mode);
        GST_WARNING_OBJECT(
            src,
            "'stream-type' setting requires depth calculation. Depth mode value forced to NEURAL");
    }
    GST_INFO(" * Depth Mode: %s", sl::toString(init_params.depth_mode).c_str());
    init_params.coordinate_units = sl::UNIT::MILLIMETER;   // ready for 16bit depth image
    GST_INFO(" * Coordinate units: %s", sl::toString(init_params.coordinate_units).c_str());
    init_params.coordinate_system = static_cast<sl::COORDINATE_SYSTEM>(src->coord_sys);
    GST_INFO(" * Coordinate system: %s", sl::toString(init_params.coordinate_system).c_str());
    init_params.depth_minimum_distance = src->depth_min_dist;
    GST_INFO(" * MIN depth: %g", init_params.depth_minimum_distance);
    init_params.depth_maximum_distance = src->depth_max_dist;
    GST_INFO(" * MAX depth: %g", init_params.depth_maximum_distance);
    init_params.depth_stabilization = src->depth_stabilization;
    GST_INFO(" * Depth Stabilization: %d", init_params.depth_stabilization);
    init_params.camera_disable_self_calib = src->camera_disable_self_calib == TRUE;
    GST_INFO(" * Disable self calibration: %s",
             (init_params.camera_disable_self_calib ? "TRUE" : "FALSE"));
    init_params.sdk_gpu_id = src->sdk_gpu_id;
    GST_INFO(" * SDK GPU ID: %d", init_params.sdk_gpu_id);
    init_params.sdk_verbose_log_file = sl::String(src->sdk_verbose_log_file->str);
    GST_INFO(" * SDK Verbose Log File: %s", init_params.sdk_verbose_log_file.c_str());
    init_params.optional_settings_path = sl::String(src->optional_settings_path->str);
    GST_INFO(" * Optional Settings Path: %s", init_params.optional_settings_path.c_str());
    init_params.sensors_required = src->sensors_required == TRUE;
    GST_INFO(" * Sensors Required: %s", (init_params.sensors_required ? "TRUE" : "FALSE"));
    init_params.enable_image_enhancement = src->enable_image_enhancement == TRUE;
    GST_INFO(" * Enable Image Enhancement: %s",
             (init_params.enable_image_enhancement ? "TRUE" : "FALSE"));
    init_params.open_timeout_sec = src->open_timeout_sec;
    GST_INFO(" * Open Timeout Seconds: %g", init_params.open_timeout_sec);
    init_params.async_grab_camera_recovery = src->async_grab_camera_recovery == TRUE;
    GST_INFO(" * Async Grab Camera Recovery: %s",
             (init_params.async_grab_camera_recovery ? "TRUE" : "FALSE"));
    init_params.grab_compute_capping_fps = src->grab_compute_capping_fps;
    GST_INFO(" * Grab Compute Capping FPS: %g", init_params.grab_compute_capping_fps);
    init_params.enable_image_validity_check = src->enable_image_validity_check;
    GST_INFO(" * Enable Image Validity Check: %d", init_params.enable_image_validity_check);
    init_params.async_image_retrieval = src->async_image_retrieval == TRUE;
    GST_INFO(" * Async Image Retrieval: %s",
             (init_params.async_image_retrieval ? "TRUE" : "FALSE"));
    if (src->max_working_res_w > 0 && src->max_working_res_h > 0) {
        init_params.maximum_working_resolution =
            sl::Resolution(src->max_working_res_w, src->max_working_res_h);
        GST_INFO(" * Maximum Working Resolution: %dx%d", src->max_working_res_w,
                 src->max_working_res_h);
    }

    sl::String opencv_calibration_file(src->opencv_calibration_file->str);
    init_params.optional_opencv_calibration_file = opencv_calibration_file;
    GST_INFO(" * OpenCV Calibration File: %s ",
             init_params.optional_opencv_calibration_file.c_str());

    std::cout << "Setting depth_mode to " << init_params.depth_mode << std::endl;

    if (src->svo_file->len != 0) {
        sl::String svo(static_cast<char *>(src->svo_file->str));
        init_params.input.setFromSVOFile(svo);
        init_params.svo_real_time_mode = src->svo_real_time == TRUE;

        GST_INFO(" * Input SVO filename: %s", src->svo_file->str);
        GST_INFO(" * SVO Real Time Mode: %s", (init_params.svo_real_time_mode ? "TRUE" : "FALSE"));
    } else if (src->camera_id != DEFAULT_PROP_CAM_ID) {
        init_params.input.setFromCameraID(src->camera_id);

        GST_INFO(" * Input Camera ID: %d", src->camera_id);
    } else if (src->camera_sn != DEFAULT_PROP_CAM_SN) {
        init_params.input.setFromSerialNumber(src->camera_sn);

        GST_INFO(" * Input Camera SN: %ld", src->camera_sn);
    } else if (src->stream_ip->len != 0) {
        sl::String ip(static_cast<char *>(src->stream_ip->str));
        init_params.input.setFromStream(ip, src->stream_port);

        GST_INFO(" * Input Stream: %s:%d", src->stream_ip->str, src->stream_port);
    } else {
        GST_INFO(" * Input from default device");
    }
    // <---- Set init parameters

    // ----> Open camera
    ret = src->zed.open(init_params);

    if (ret > sl::ERROR_CODE::SUCCESS) {
        GST_ELEMENT_ERROR(src, RESOURCE, NOT_FOUND,
                          ("Failed to open camera, '%s'", sl::toString(ret).c_str()), (NULL));
        return FALSE;
    }
    // <---- Open camera

    // ----> Camera Controls
    GST_INFO("CAMERA CONTROLS");
    src->zed.setCameraSettings((sl::VIDEO_SETTINGS::BRIGHTNESS), (src->brightness));
    GST_INFO(" * BRIGHTNESS: %d", src->brightness);
    src->zed.setCameraSettings(sl::VIDEO_SETTINGS::CONTRAST, src->contrast);
    GST_INFO(" * CONTRAST: %d", src->contrast);
    src->zed.setCameraSettings(sl::VIDEO_SETTINGS::HUE, src->hue);
    GST_INFO(" * HUE: %d", src->hue);
    src->zed.setCameraSettings(sl::VIDEO_SETTINGS::SATURATION, src->saturation);
    GST_INFO(" * SATURATION: %d", src->saturation);
    src->zed.setCameraSettings(sl::VIDEO_SETTINGS::SHARPNESS, src->sharpness);
    GST_INFO(" * SHARPNESS: %d", src->sharpness);
    src->zed.setCameraSettings(sl::VIDEO_SETTINGS::GAMMA, src->gamma);
    GST_INFO(" * GAMMA: %d", src->gamma);
    if (src->aec_agc == FALSE) {
        src->zed.setCameraSettings(sl::VIDEO_SETTINGS::AEC_AGC, src->aec_agc);
        GST_INFO(" * AEC_AGC: %s", (src->aec_agc ? "TRUE" : "FALSE"));
        src->zed.setCameraSettings(sl::VIDEO_SETTINGS::EXPOSURE, src->exposure);
        GST_INFO(" * EXPOSURE: %d", src->exposure);
        src->zed.setCameraSettings(sl::VIDEO_SETTINGS::GAIN, src->gain);
        GST_INFO(" * GAIN: %d", src->gain);
    } else {
        src->zed.setCameraSettings(sl::VIDEO_SETTINGS::AEC_AGC, src->aec_agc);
        GST_INFO(" * AEC_AGC: %s", (src->aec_agc ? "TRUE" : "FALSE"));

        if (src->aec_agc_roi_x != -1 && src->aec_agc_roi_y != -1 && src->aec_agc_roi_w != -1 &&
            src->aec_agc_roi_h != -1) {
            sl::Rect roi;
            roi.x = src->aec_agc_roi_x;
            roi.y = src->aec_agc_roi_y;
            roi.width = src->aec_agc_roi_w;
            roi.height = src->aec_agc_roi_h;

            sl::SIDE side = static_cast<sl::SIDE>(src->aec_agc_roi_side);

            GST_INFO(" * AEC_AGC_ROI: (%d,%d)-%dx%d - Side: %d", src->aec_agc_roi_x,
                     src->aec_agc_roi_y, src->aec_agc_roi_w, src->aec_agc_roi_h,
                     src->aec_agc_roi_side);

            src->zed.setCameraSettings(sl::VIDEO_SETTINGS::AEC_AGC_ROI, roi, side);
        }

        src->zed.setCameraSettings(sl::VIDEO_SETTINGS::AUTO_EXPOSURE_TIME_RANGE,
                                   src->exposureRange_min, src->exposureRange_max);
        GST_INFO(" * AUTO EXPOSURE TIME RANGE: [%d,%d]", src->exposureRange_min,
                 src->exposureRange_max);
    }
    if (src->whitebalance_temperature_auto == FALSE) {
        src->zed.setCameraSettings(sl::VIDEO_SETTINGS::WHITEBALANCE_AUTO,
                                   src->whitebalance_temperature_auto);
        GST_INFO(" * WHITEBALANCE_AUTO: %s",
                 (src->whitebalance_temperature_auto ? "TRUE" : "FALSE"));
        src->whitebalance_temperature /= 100;
        src->whitebalance_temperature *= 100;
        src->zed.setCameraSettings(sl::VIDEO_SETTINGS::WHITEBALANCE_TEMPERATURE,
                                   src->whitebalance_temperature);
        GST_INFO(" * WHITEBALANCE_TEMPERATURE: %d", src->whitebalance_temperature);

    } else {
        src->zed.setCameraSettings(sl::VIDEO_SETTINGS::WHITEBALANCE_AUTO,
                                   src->whitebalance_temperature_auto);
        GST_INFO(" * WHITEBALANCE_AUTO: %s",
                 (src->whitebalance_temperature_auto ? "TRUE" : "FALSE"));
    }
    src->zed.setCameraSettings(sl::VIDEO_SETTINGS::LED_STATUS, src->led_status);
    GST_INFO(" * LED_STATUS: %s", (src->led_status ? "ON" : "OFF"));
    // <---- Camera Controls

    // ----> Runtime parameters
    GST_TRACE_OBJECT(src, "CAMERA RUNTIME PARAMETERS");
    if (src->depth_mode == static_cast<gint>(sl::DEPTH_MODE::NONE) && !src->pos_tracking) {
        GST_INFO(" * Depth calculation: OFF");
    } else {
        GST_INFO(" * Depth calculation: ON");
    }

    GST_INFO(" * Depth Confidence threshold: %d", src->confidence_threshold);
    GST_INFO(" * Depth Texture Confidence threshold: %d", src->texture_confidence_threshold);
    GST_INFO(" * 3D Reference Frame: %s",
             sl::toString((sl::COORDINATE_SYSTEM) src->measure3D_reference_frame).c_str());
    GST_INFO(" * Fill Mode: %s", (src->fill_mode ? "TRUE" : "FALSE"));

    if (src->roi) {
        if (src->roi_x != -1 && src->roi_y != -1 && src->roi_w != -1 && src->roi_h != -1) {
            int roi_x_end = src->roi_x + src->roi_w;
            int roi_y_end = src->roi_y + src->roi_h;
            sl::Resolution resolution = sl::getResolution(init_params.camera_resolution);
            if (src->roi_x >= 0 && src->roi_x < resolution.width && src->roi_y >= 0 &&
                src->roi_y < resolution.height && roi_x_end <= resolution.width &&
                roi_y_end <= resolution.height) {

                sl::Mat roi_mask(resolution, sl::MAT_TYPE::U8_C1, sl::MEM::CPU);
                roi_mask.setTo<sl::uchar1>(0, sl::MEM::CPU);
                for (unsigned int v = src->roi_y; v < roi_y_end; v++)
                    for (unsigned int u = src->roi_x; u < roi_x_end; u++)
                        roi_mask.setValue<sl::uchar1>(u, v, 255, sl::MEM::CPU);

                GST_INFO(" * ROI mask: (%d,%d)-%dx%d", src->roi_x, src->roi_y, src->roi_w,
                         src->roi_h);

                ret = src->zed.setRegionOfInterest(roi_mask);
                if (ret != sl::ERROR_CODE::SUCCESS) {
                    GST_ELEMENT_ERROR(
                        src, RESOURCE, NOT_FOUND,
                        ("Failed to set region of interest, '%s'", sl::toString(ret).c_str()),
                        (NULL));
                    return FALSE;
                }
            }
        }
    }
    // <---- Runtime parameters

    // ----> Positional tracking
    GST_INFO("POSITIONAL TRACKING PARAMETERS");
    GST_INFO(" * Positional tracking status: %s", (src->pos_tracking ? "ON" : "OFF"));
    if (src->pos_tracking) {
        sl::PositionalTrackingParameters pos_trk_params;
        pos_trk_params.mode = static_cast<sl::POSITIONAL_TRACKING_MODE>(src->pos_trk_mode);
        GST_INFO(" * Pos. Tracking mode: %s", sl::toString(pos_trk_params.mode).c_str());
        pos_trk_params.set_as_static = (src->camera_static == TRUE);
        GST_INFO(" * Camera static: %s", (pos_trk_params.set_as_static ? "TRUE" : "FALSE"));
        sl::String area_file_path(static_cast<char *>(src->area_file_path->str));
        pos_trk_params.area_file_path = area_file_path;
        GST_INFO(" * Area file path: %s", pos_trk_params.area_file_path.c_str());
        pos_trk_params.enable_area_memory = (src->enable_area_memory == TRUE);
        GST_INFO(" * Area memory: %s", (pos_trk_params.enable_area_memory ? "TRUE" : "FALSE"));
        pos_trk_params.enable_imu_fusion = (src->enable_imu_fusion == TRUE);
        GST_INFO(" * IMU fusion: %s", (pos_trk_params.enable_imu_fusion ? "TRUE" : "FALSE"));
        pos_trk_params.enable_pose_smoothing = (src->enable_pose_smoothing == TRUE);
        GST_INFO(" * Pose smoothing: %s",
                 (pos_trk_params.enable_pose_smoothing ? "TRUE" : "FALSE"));
        pos_trk_params.set_floor_as_origin = (src->set_floor_as_origin == TRUE);
        GST_INFO(" * Floor as origin: %s", (pos_trk_params.set_floor_as_origin ? "TRUE" : "FALSE"));
        pos_trk_params.set_gravity_as_origin = (src->set_gravity_as_origin == TRUE);
        GST_INFO(" * Gravity as origin: %s",
                 (pos_trk_params.set_gravity_as_origin ? "TRUE" : "FALSE"));
        pos_trk_params.depth_min_range = src->depth_min_range;
        GST_INFO(" * Depth min range: %g mm", (pos_trk_params.depth_min_range));

        sl::Translation init_pos(src->init_pose_x, src->init_pose_y, src->init_pose_z);
        sl::Rotation init_or;
        switch (init_params.coordinate_system) {
        case sl::COORDINATE_SYSTEM::IMAGE:
        case sl::COORDINATE_SYSTEM::LEFT_HANDED_Y_UP:
        case sl::COORDINATE_SYSTEM::RIGHT_HANDED_Y_UP:
            init_or.setEulerAngles(
                sl::float3(src->init_orient_pitch, src->init_orient_yaw, src->init_orient_roll),
                false);
            break;
        case sl::COORDINATE_SYSTEM::RIGHT_HANDED_Z_UP:
            init_or.setEulerAngles(
                sl::float3(src->init_orient_pitch, src->init_orient_roll, src->init_orient_yaw),
                false);
            break;
        case sl::COORDINATE_SYSTEM::LEFT_HANDED_Z_UP:
        case sl::COORDINATE_SYSTEM::RIGHT_HANDED_Z_UP_X_FWD:
            init_or.setEulerAngles(
                sl::float3(src->init_orient_roll, src->init_orient_pitch, src->init_orient_yaw),
                false);
            break;
        }

        pos_trk_params.initial_world_transform = sl::Transform(init_or, init_pos);
        GST_INFO(" * Initial world transform: T(%g,%g,%g) OR(%g,%g,%g)", src->init_pose_x,
                 src->init_pose_y, src->init_pose_z, src->init_orient_roll, src->init_orient_pitch,
                 src->init_orient_yaw);

        ret = src->zed.enablePositionalTracking(pos_trk_params);
        if (ret != sl::ERROR_CODE::SUCCESS) {
            GST_ELEMENT_ERROR(
                src, RESOURCE, NOT_FOUND,
                ("Failed to start positional tracking, '%s'", sl::toString(ret).c_str()), (NULL));
            return FALSE;
        } else {
            GST_INFO("*** Positional Tracking enabled ***");
        }
    }
    // <---- Positional tracking

    // ----> Object Detection
    GST_INFO("OBJECT DETECTION PARAMETERS");
    GST_INFO(" * Object Detection status: %s", (src->object_detection ? "ON" : "OFF"));
    if (src->object_detection) {
        sl::ObjectDetectionParameters od_params;
        od_params.instance_module_id = src->od_instance_id;
        od_params.enable_tracking = (src->od_enable_tracking == TRUE);
        GST_INFO(" * Object tracking: %s", (od_params.enable_tracking ? "TRUE" : "FALSE"));
        od_params.enable_segmentation = (src->od_enable_segm_output == TRUE);
        GST_INFO(" * Segmentation Mask output: %s",
                 (od_params.enable_segmentation ? "TRUE" : "FALSE"));
        od_params.detection_model =
            static_cast<sl::OBJECT_DETECTION_MODEL>(src->od_detection_model);
        GST_INFO(" * Detection model: %s", sl::toString(od_params.detection_model).c_str());
        od_params.filtering_mode = static_cast<sl::OBJECT_FILTERING_MODE>(src->od_filter_mode);
        GST_INFO(" * Filter mode: %s", sl::toString(od_params.filtering_mode).c_str());
        od_params.max_range = src->od_max_range;
        GST_INFO(" * Max range: %g mm", od_params.max_range);
        od_params.prediction_timeout_s = src->od_prediction_timeout_s;
        GST_INFO(" * Prediction timeout: %g sec", (od_params.prediction_timeout_s));
        od_params.allow_reduced_precision_inference = src->od_allow_reduced_precision_inference;
        GST_INFO(" * Allow reduced precision inference: %s",
                 (od_params.allow_reduced_precision_inference ? "TRUE" : "FALSE"));
        od_params.custom_onnx_file = sl::String(src->od_custom_onnx_file->str);
        GST_INFO(" * Custom ONNX file: %s", od_params.custom_onnx_file.c_str());
        od_params.custom_onnx_dynamic_input_shape = sl::Resolution(
            src->od_custom_onnx_dynamic_input_shape_w, src->od_custom_onnx_dynamic_input_shape_h);
        GST_INFO(" * Custom ONNX dynamic input shape: %dx%d",
                 od_params.custom_onnx_dynamic_input_shape.width,
                 od_params.custom_onnx_dynamic_input_shape.height);

        GST_INFO(" * Confidence thresh.: %g", src->od_det_conf);

        GST_INFO(" * Person conf: %g", src->od_person_conf);
        GST_INFO(" * Vehicle conf: %g", src->od_vehicle_conf);
        GST_INFO(" * Animal conf: %g", src->od_animal_conf);
        GST_INFO(" * Bag conf: %g", src->od_bag_conf);
        GST_INFO(" * Electronics conf: %g", src->od_electronics_conf);
        GST_INFO(" * Fruit/Vegetables conf: %g", src->od_fruit_vegetable_conf);
        GST_INFO(" * Sport conf: %g", src->od_sport_conf);

        ret = src->zed.enableObjectDetection(od_params);
        if (ret != sl::ERROR_CODE::SUCCESS) {
            GST_ELEMENT_ERROR(src, RESOURCE, NOT_FOUND,
                              ("Failed to start Object Detection: '%s' - %s",
                               sl::toString(ret).c_str(), sl::toVerbose(ret).c_str()),
                              (NULL));
            return FALSE;
        } else {
            GST_INFO("*** Object Detection enabled ***");
        }
    }
    // <---- Object Detection

    // ----> Body Tracking
    GST_INFO("BODY TRACKING PARAMETERS");
    GST_INFO(" * Body Tracking status: %s", (src->body_tracking ? "ON" : "OFF"));
    if (src->body_tracking) {
        sl::BodyTrackingParameters bt_params;
        bt_params.instance_module_id = BT_INSTANCE_MODULE_ID;
        bt_params.detection_model = static_cast<sl::BODY_TRACKING_MODEL>(src->bt_model);
        GST_INFO(" * Body Tracking model: %s", sl::toString(bt_params.detection_model).c_str());
        bt_params.body_format = static_cast<sl::BODY_FORMAT>(src->bt_format);
        GST_INFO(" * Body Format: %s", sl::toString(bt_params.body_format).c_str());
        bt_params.allow_reduced_precision_inference = src->bt_reduce_precision;
        GST_INFO(" * Allow reduced precision inference: %s",
                 (bt_params.allow_reduced_precision_inference ? "TRUE" : "FALSE"));
        bt_params.body_selection = static_cast<sl::BODY_KEYPOINTS_SELECTION>(src->bt_kp_sel);
        GST_INFO(" * Body KP Selection: %s", sl::toString(bt_params.body_selection).c_str());
        bt_params.enable_body_fitting = src->bt_fitting;
        GST_INFO(" * Body Fitting: %s", (bt_params.enable_body_fitting ? "TRUE" : "FALSE"));
        bt_params.enable_segmentation = src->bt_enable_segm_output;
        GST_INFO(" * Body Segmentation: %s", (bt_params.enable_segmentation ? "TRUE" : "FALSE"));
        bt_params.enable_tracking = src->bt_enable_trk;
        GST_INFO(" * Tracking: %s", (bt_params.enable_tracking ? "TRUE" : "FALSE"));
        bt_params.max_range = src->bt_max_range;
        GST_INFO(" * Max Range: %g mm", bt_params.max_range);
        bt_params.prediction_timeout_s = src->bt_pred_timeout;
        GST_INFO(" * Prediction timeout: %g sec", bt_params.prediction_timeout_s);

        GST_INFO(" * Det. Confidence: %g", src->bt_rt_det_conf);
        GST_INFO(" * Min. KP selection: %d", src->bt_rt_min_kp_thresh);
        GST_INFO(" * Skeleton Smoothing: %g", src->bt_rt_skel_smoothing);

        ret = src->zed.enableBodyTracking(bt_params);
        if (ret != sl::ERROR_CODE::SUCCESS) {
            GST_ELEMENT_ERROR(src, RESOURCE, NOT_FOUND,
                              ("Failed to start Body Tracking: '%s' - %s",
                               sl::toString(ret).c_str(), sl::toVerbose(ret).c_str()),
                              (NULL));
            return FALSE;
        } else {
            GST_INFO("*** Body Tracking enabled ***");
        }
    }
    // <---- Body Tracking

    if (!gst_zedsrc_calculate_caps(src)) {
        return FALSE;
    }

    return TRUE;
}

static gboolean gst_zedsrc_stop(GstBaseSrc *bsrc) {
    GstZedSrc *src = GST_ZED_SRC(bsrc);

    GST_TRACE_OBJECT(src, "gst_zedsrc_stop");

    // Stop SVO recording if active
    if (src->svo_rec_active) {
        src->zed.disableRecording();
        src->svo_rec_active = FALSE;
        GST_INFO_OBJECT(src, "SVO recording stopped on pipeline stop");
    }

    gst_zedsrc_reset(src);

    return TRUE;
}

static GstCaps *gst_zedsrc_get_caps(GstBaseSrc *bsrc, GstCaps *filter) {
    GstZedSrc *src = GST_ZED_SRC(bsrc);
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

static gboolean gst_zedsrc_set_caps(GstBaseSrc *bsrc, GstCaps *caps) {
    GstZedSrc *src = GST_ZED_SRC(bsrc);
    GST_TRACE_OBJECT(src, "gst_zedsrc_set_caps");

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

static gboolean gst_zedsrc_unlock(GstBaseSrc *bsrc) {
    GstZedSrc *src = GST_ZED_SRC(bsrc);

    GST_TRACE_OBJECT(src, "gst_zedsrc_unlock");

    src->stop_requested = TRUE;

    return TRUE;
}

static gboolean gst_zedsrc_unlock_stop(GstBaseSrc *bsrc) {
    GstZedSrc *src = GST_ZED_SRC(bsrc);

    GST_TRACE_OBJECT(src, "gst_zedsrc_unlock_stop");

    src->stop_requested = FALSE;

    return TRUE;
}

static gboolean gst_zedsrc_query(GstBaseSrc *bsrc, GstQuery *query) {
    GstZedSrc *src = GST_ZED_SRC(bsrc);
    gboolean ret;

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_LATENCY: {
        GstClockTime min_latency, max_latency;

        if (!src->is_started) {
            GST_DEBUG_OBJECT(src, "Latency query before started, returning FALSE");
            return FALSE;
        }

        // Calculate latency based on configured FPS
        // Latency is approximately one frame duration
        if (src->camera_fps > 0) {
            min_latency = gst_util_uint64_scale_int(GST_SECOND, 1, src->camera_fps);
            max_latency = min_latency * 2;   // Allow some buffer for processing
        } else {
            // Fallback to 30 FPS assumption
            min_latency = gst_util_uint64_scale_int(GST_SECOND, 1, 30);
            max_latency = min_latency * 2;
        }

        GST_DEBUG_OBJECT(src, "Latency query: min=%.3f ms, max=%.3f ms",
                         (gdouble) min_latency / GST_MSECOND, (gdouble) max_latency / GST_MSECOND);

        gst_query_set_latency(query, TRUE, min_latency, max_latency);
        ret = TRUE;
        break;
    }
    default:
        ret = GST_BASE_SRC_CLASS(gst_zedsrc_parent_class)->query(bsrc, query);
        break;
    }

    return ret;
}

static void gst_zedsrc_setup_runtime_parameters(GstZedSrc *src, sl::RuntimeParameters &zedRtParams) {
    GST_TRACE_OBJECT(src, "CAMERA RUNTIME PARAMETERS");
    if (src->depth_mode == static_cast<gint>(sl::DEPTH_MODE::NONE) && !src->pos_tracking) {
        zedRtParams.enable_depth = false;
    } else {
        zedRtParams.enable_depth = true;
    }
    zedRtParams.confidence_threshold = src->confidence_threshold;
    zedRtParams.texture_confidence_threshold = src->texture_confidence_threshold;
    zedRtParams.measure3D_reference_frame =
        static_cast<sl::REFERENCE_FRAME>(src->measure3D_reference_frame);
    zedRtParams.enable_fill_mode = src->fill_mode;
    zedRtParams.remove_saturated_areas = src->remove_saturated_areas == TRUE;

    // Runtime exposure control
    if (src->exposure_gain_updated) {
        if (src->aec_agc == FALSE) {
            // Manual exposure control
            src->zed.setCameraSettings(sl::VIDEO_SETTINGS::AEC_AGC, src->aec_agc);
            src->zed.setCameraSettings(sl::VIDEO_SETTINGS::EXPOSURE, src->exposure);
            src->zed.setCameraSettings(sl::VIDEO_SETTINGS::GAIN, src->gain);
            GST_INFO(" Runtime EXPOSURE %d - GAIN %d", src->exposure, src->gain);
            src->exposure_gain_updated = FALSE;
        } else {
            // Auto exposure control
            if (src->aec_agc_roi_x != -1 && src->aec_agc_roi_y != -1 && src->aec_agc_roi_w != -1 &&
                src->aec_agc_roi_h != -1) {
                sl::Rect roi;
                roi.x = src->aec_agc_roi_x;
                roi.y = src->aec_agc_roi_y;
                roi.width = src->aec_agc_roi_w;
                roi.height = src->aec_agc_roi_h;
                sl::SIDE side = static_cast<sl::SIDE>(src->aec_agc_roi_side);
                GST_INFO(" Runtime AEC_AGC_ROI: (%d,%d)-%dx%d - Side: %d", src->aec_agc_roi_x,
                         src->aec_agc_roi_y, src->aec_agc_roi_w, src->aec_agc_roi_h,
                         src->aec_agc_roi_side);
                src->zed.setCameraSettings(sl::VIDEO_SETTINGS::AEC_AGC, src->aec_agc);
                src->zed.setCameraSettings(sl::VIDEO_SETTINGS::AEC_AGC_ROI, roi, side);
                src->exposure_gain_updated = FALSE;
            }
        }
    }
}

static void gst_zedsrc_attach_metadata(GstZedSrc *src, GstBuffer *buf, GstClockTime clock_time) {
    ZedInfo info;
    ZedPose pose;
    ZedSensors sens;
    ZedObjectData *obj_data = new ZedObjectData[GST_ZEDSRC_MAX_OBJECTS];
    memset(obj_data, 0, GST_ZEDSRC_MAX_OBJECTS * sizeof(ZedObjectData));
    
    guint8 obj_count = 0;
    guint64 offset = 0;
    sl::ERROR_CODE ret;

    sl::CameraInformation cam_info;
    sl::ObjectDetectionRuntimeParameters od_rt_params;
    std::vector<sl::OBJECT_CLASS> class_filter;
    std::map<sl::OBJECT_CLASS, float> class_det_conf;
    sl::Objects det_objs;
    sl::BodyTrackingRuntimeParameters bt_rt_params;
    sl::Bodies bodies;

    // ----> Info metadata
    cam_info = src->zed.getCameraInformation();
    info.cam_model = (gint) cam_info.camera_model;
    info.stream_type = src->stream_type;
    info.grab_single_frame_width = cam_info.camera_configuration.resolution.width;
    info.grab_single_frame_height = cam_info.camera_configuration.resolution.height;
    if (info.grab_single_frame_height == 752 || info.grab_single_frame_height == 1440 ||
        info.grab_single_frame_height == 2160 || info.grab_single_frame_height == 2484) {
        info.grab_single_frame_height /= 2;   // Only half buffer size if the stream is composite
    }
    // <---- Info metadata

    // ----> Positional Tracking metadata
    if (src->pos_tracking) {
        sl::Pose cam_pose;
        sl::POSITIONAL_TRACKING_STATE state = src->zed.getPosition(cam_pose);

        sl::Translation pos = cam_pose.getTranslation();
        pose.pose_avail = TRUE;
        pose.pos_tracking_state = static_cast<int>(state);
        pose.pos[0] = pos(0);
        pose.pos[1] = pos(1);
        pose.pos[2] = pos(2);

        sl::Orientation orient = cam_pose.getOrientation();
        sl::float3 euler = orient.getRotationMatrix().getEulerAngles();
        pose.orient[0] = euler[0];
        pose.orient[1] = euler[1];
        pose.orient[2] = euler[2];
    } else {
        pose.pose_avail = FALSE;
        pose.pos_tracking_state = static_cast<int>(sl::POSITIONAL_TRACKING_STATE::OFF);
        pose.pos[0] = 0.0;
        pose.pos[1] = 0.0;
        pose.pos[2] = 0.0;
        pose.orient[0] = 0.0;
        pose.orient[1] = 0.0;
        pose.orient[2] = 0.0;
    }
    // <---- Positional Tracking

    // ----> Sensors metadata
    if (src->zed.getCameraInformation().camera_model != sl::MODEL::ZED) {
        sens.sens_avail = TRUE;
        sens.imu.imu_avail = TRUE;

        sl::SensorsData sens_data;
        src->zed.getSensorsData(sens_data, sl::TIME_REFERENCE::IMAGE);

        sens.imu.acc[0] = sens_data.imu.linear_acceleration.x;
        sens.imu.acc[1] = sens_data.imu.linear_acceleration.y;
        sens.imu.acc[2] = sens_data.imu.linear_acceleration.z;
        sens.imu.gyro[0] = sens_data.imu.angular_velocity.x;
        sens.imu.gyro[1] = sens_data.imu.angular_velocity.y;
        sens.imu.gyro[2] = sens_data.imu.angular_velocity.z;

        if (src->zed.getCameraInformation().camera_model != sl::MODEL::ZED_M) {
            sens.mag.mag_avail = TRUE;
            sens.mag.mag[0] = sens_data.magnetometer.magnetic_field_calibrated.x;
            sens.mag.mag[1] = sens_data.magnetometer.magnetic_field_calibrated.y;
            sens.mag.mag[2] = sens_data.magnetometer.magnetic_field_calibrated.z;
            sens.env.env_avail = TRUE;

            float temp;
            sens_data.temperature.get(sl::SensorsData::TemperatureData::SENSOR_LOCATION::BAROMETER,
                                      temp);
            sens.env.temp = temp;
            sens.env.press = sens_data.barometer.pressure * 1e-2;

            float tempL, tempR;
            sens_data.temperature.get(
                sl::SensorsData::TemperatureData::SENSOR_LOCATION::ONBOARD_LEFT, tempL);
            sens_data.temperature.get(
                sl::SensorsData::TemperatureData::SENSOR_LOCATION::ONBOARD_RIGHT, tempR);
            sens.temp.temp_avail = TRUE;
            sens.temp.temp_cam_left = tempL;
            sens.temp.temp_cam_right = tempR;
        } else {
            sens.mag.mag_avail = FALSE;
            sens.env.env_avail = FALSE;
            sens.temp.temp_avail = FALSE;
        }
    } else {
        sens.sens_avail = FALSE;
        sens.imu.imu_avail = FALSE;
        sens.mag.mag_avail = FALSE;
        sens.env.env_avail = FALSE;
        sens.temp.temp_avail = FALSE;
    }
    // <---- Sensors metadata

    // ----> Object detection metadata
    if (src->object_detection) {
        GST_LOG_OBJECT(src, "Object Detection enabled");

        od_rt_params.detection_confidence_threshold = src->od_det_conf;

        class_filter.clear();
        if (src->od_person_conf > 0.0f)
            class_filter.push_back(sl::OBJECT_CLASS::PERSON);
        if (src->od_vehicle_conf > 0.0f)
            class_filter.push_back(sl::OBJECT_CLASS::VEHICLE);
        if (src->od_animal_conf > 0.0f)
            class_filter.push_back(sl::OBJECT_CLASS::ANIMAL);
        if (src->od_bag_conf > 0.0f)
            class_filter.push_back(sl::OBJECT_CLASS::BAG);
        if (src->od_electronics_conf > 0.0f)
            class_filter.push_back(sl::OBJECT_CLASS::ELECTRONICS);
        if (src->od_fruit_vegetable_conf > 0.0f)
            class_filter.push_back(sl::OBJECT_CLASS::FRUIT_VEGETABLE);
        if (src->od_sport_conf > 0.0f)
            class_filter.push_back(sl::OBJECT_CLASS::SPORT);
        od_rt_params.object_class_filter = class_filter;

        class_det_conf.clear();
        class_det_conf[sl::OBJECT_CLASS::PERSON] = src->od_person_conf;
        class_det_conf[sl::OBJECT_CLASS::VEHICLE] = src->od_vehicle_conf;
        class_det_conf[sl::OBJECT_CLASS::ANIMAL] = src->od_animal_conf;
        class_det_conf[sl::OBJECT_CLASS::ELECTRONICS] = src->od_electronics_conf;
        class_det_conf[sl::OBJECT_CLASS::BAG] = src->od_bag_conf;
        class_det_conf[sl::OBJECT_CLASS::FRUIT_VEGETABLE] = src->od_fruit_vegetable_conf;
        class_det_conf[sl::OBJECT_CLASS::SPORT] = src->od_sport_conf;
        od_rt_params.object_class_detection_confidence_threshold = class_det_conf;

        ret = src->zed.retrieveObjects(det_objs, od_rt_params, OD_INSTANCE_MODULE_ID);

        if (ret == sl::ERROR_CODE::SUCCESS) {
            if (det_objs.is_new) {
                GST_LOG_OBJECT(src, "OD new data");

                obj_count = det_objs.object_list.size();
                if (obj_count > 255)
                    obj_count = 255;

                GST_LOG_OBJECT(src, "Number of detected objects (clamped): %d", obj_count);

                uint8_t idx = 0;
                for (auto i = det_objs.object_list.begin();
                     i != det_objs.object_list.end() && idx < obj_count; ++i, ++idx) {
                    sl::ObjectData obj = *i;

                    obj_data[idx].skeletons_avail = FALSE;
                    obj_data[idx].id = obj.id;

                    obj_data[idx].label = static_cast<OBJECT_CLASS>(obj.label);
                    obj_data[idx].sublabel = static_cast<OBJECT_SUBCLASS>(obj.sublabel);

                    obj_data[idx].tracking_state =
                        static_cast<OBJECT_TRACKING_STATE>(obj.tracking_state);
                    obj_data[idx].action_state = static_cast<OBJECT_ACTION_STATE>(obj.action_state);

                    obj_data[idx].confidence = obj.confidence;

                    memcpy(obj_data[idx].position, (void *) obj.position.ptr(), 3 * sizeof(float));
                    memcpy(obj_data[idx].position_covariance, (void *) obj.position_covariance,
                           6 * sizeof(float));
                    memcpy(obj_data[idx].velocity, (void *) obj.velocity.ptr(), 3 * sizeof(float));

                    if (obj.bounding_box_2d.size() > 0) {
                        memcpy((uint8_t *) obj_data[idx].bounding_box_2d,
                               (uint8_t *) obj.bounding_box_2d.data(),
                               obj.bounding_box_2d.size() * 2 * sizeof(unsigned int));
                    }
                    if (obj.bounding_box.size() > 0) {
                        memcpy(obj_data[idx].bounding_box_3d, (void *) obj.bounding_box.data(),
                               24 * sizeof(float));
                    }

                    memcpy(obj_data[idx].dimensions, (void *) obj.dimensions.ptr(),
                           3 * sizeof(float));
                }
            } else {
                obj_count = 0;
            }
        } else {
            GST_WARNING_OBJECT(src, "Object detection problem: '%s' - %s",
                               sl::toString(ret).c_str(), sl::toVerbose(ret).c_str());
        }
    }
    // <---- Object detection metadata

    // ----> Body Tracking metadata
    if (src->body_tracking) {
        guint8 b_idx = obj_count;

        GST_LOG_OBJECT(src, "Body Tracking enabled");

        bt_rt_params.detection_confidence_threshold = src->bt_rt_det_conf;
        bt_rt_params.minimum_keypoints_threshold = src->bt_rt_min_kp_thresh;
        bt_rt_params.skeleton_smoothing = src->bt_rt_skel_smoothing;

        ret = src->zed.retrieveBodies(bodies, bt_rt_params, BT_INSTANCE_MODULE_ID);

        if (ret == sl::ERROR_CODE::SUCCESS) {
            if (bodies.is_new) {
                GST_LOG_OBJECT(src, "BT new data");

                int bodies_count = bodies.body_list.size();
                GST_LOG_OBJECT(src, "Number of detected bodies: %d", bodies_count);

                for (auto i = bodies.body_list.begin(); i != bodies.body_list.end() && b_idx < 256;
                     ++i, ++b_idx) {
                    sl::BodyData obj = *i;

                    obj_data[b_idx].skeletons_avail = TRUE;
                    obj_data[b_idx].id = obj.id;
                    obj_data[b_idx].label = OBJECT_CLASS::PERSON;
                    obj_data[b_idx].sublabel = OBJECT_SUBCLASS::PERSON;

                    obj_data[b_idx].tracking_state =
                        static_cast<OBJECT_TRACKING_STATE>(obj.tracking_state);
                    obj_data[b_idx].action_state =
                        static_cast<OBJECT_ACTION_STATE>(obj.action_state);

                    obj_data[b_idx].confidence = obj.confidence;

                    memcpy(obj_data[b_idx].position, (void *) obj.position.ptr(),
                           3 * sizeof(float));
                    memcpy(obj_data[b_idx].position_covariance, (void *) obj.position_covariance,
                           6 * sizeof(float));
                    memcpy(obj_data[b_idx].velocity, (void *) obj.velocity.ptr(),
                           3 * sizeof(float));

                    if (obj.bounding_box_2d.size() > 0) {
                        memcpy((uint8_t *) obj_data[b_idx].bounding_box_2d,
                               (uint8_t *) obj.bounding_box_2d.data(),
                               obj.bounding_box_2d.size() * 2 * sizeof(unsigned int));
                    }
                    if (obj.bounding_box.size() > 0) {
                        memcpy(obj_data[b_idx].bounding_box_3d, (void *) obj.bounding_box.data(),
                               24 * sizeof(float));
                    }

                    memcpy(obj_data[b_idx].dimensions, (void *) obj.dimensions.ptr(),
                           3 * sizeof(float));

                    switch (static_cast<sl::BODY_FORMAT>(src->bt_format)) {
                    case sl::BODY_FORMAT::BODY_18:
                        obj_data[b_idx].skel_format = 18;
                        break;
                    case sl::BODY_FORMAT::BODY_34:
                        obj_data[b_idx].skel_format = 34;
                        break;
                    case sl::BODY_FORMAT::BODY_38:
                        obj_data[b_idx].skel_format = 38;
                        break;
                    default:
                        obj_data[b_idx].skel_format = 0;
                        break;
                    }

                    if (obj.keypoint_2d.size() > 0 && obj_data[b_idx].skel_format > 0) {
                        memcpy(obj_data[b_idx].keypoint_2d, (void *) obj.keypoint_2d.data(),
                               2 * obj_data[b_idx].skel_format * sizeof(float));
                    }
                    if (obj.keypoint.size() > 0 && obj_data[b_idx].skel_format > 0) {
                        memcpy(obj_data[b_idx].keypoint_3d, (void *) obj.keypoint.data(),
                               3 * obj_data[b_idx].skel_format * sizeof(float));
                    }

                    if (obj.head_bounding_box_2d.size() > 0) {
                        memcpy(obj_data[b_idx].head_bounding_box_2d,
                               (void *) obj.head_bounding_box_2d.data(), 8 * sizeof(unsigned int));
                    }
                    if (obj.head_bounding_box.size() > 0) {
                        memcpy(obj_data[b_idx].head_bounding_box_3d,
                               (void *) obj.head_bounding_box.data(), 24 * sizeof(float));
                    }
                    memcpy(obj_data[b_idx].head_position, (void *) obj.head_position.ptr(),
                           3 * sizeof(float));
                }

                obj_count = b_idx;
            }
        } else {
            GST_WARNING_OBJECT(src, "Body Tracking problem: '%s' - %s", sl::toString(ret).c_str(),
                               sl::toVerbose(ret).c_str());
        }
    }
    // <---- Body Tracking metadata

    // ----> Timestamp meta-data
    GST_BUFFER_TIMESTAMP(buf) =
        GST_CLOCK_DIFF(gst_element_get_base_time(GST_ELEMENT(src)), clock_time);
    GST_BUFFER_DTS(buf) = GST_BUFFER_TIMESTAMP(buf);
    GST_BUFFER_OFFSET(buf) = src->buffer_index++;
    // <---- Timestamp meta-data

    offset = GST_BUFFER_OFFSET(buf);
    gst_buffer_add_zed_src_meta(buf, info, pose, sens,
                                       src->object_detection | src->body_tracking, obj_count,
                                       obj_data, offset);
    
    delete[] obj_data;
}

static GstFlowReturn gst_zedsrc_fill(GstPushSrc *psrc, GstBuffer *buf) {
    GstZedSrc *src = GST_ZED_SRC(psrc);
    GST_TRACE_OBJECT(src, "gst_zedsrc_fill");

    /// All locals here for goto
    sl::ERROR_CODE ret;
    GstMapInfo minfo;
    GstClock *clock = nullptr;
    GstClockTime clock_time = GST_CLOCK_TIME_NONE;

    gboolean mapped = FALSE;
    GstFlowReturn flow_ret = GST_FLOW_OK;

    // objects we use later, but must be declared before any goto
    ZedObjectData obj_data[GST_ZEDSRC_MAX_OBJECTS] = {0};
    guint8 obj_count = 0;
    guint64 offset = 0;
    GstZedSrcMeta *meta = nullptr;

    sl::RuntimeParameters zedRtParams;
    CUcontext zctx;

    sl::Mat left_img;
    sl::Mat right_img;
    sl::Mat depth_data;

    sl::CameraInformation cam_info;
    ZedInfo info;
    ZedPose pose;
    ZedSensors sens;

    sl::ObjectDetectionRuntimeParameters od_rt_params;
    std::vector<sl::OBJECT_CLASS> class_filter;
    std::map<sl::OBJECT_CLASS, float> class_det_conf;
    sl::Objects det_objs;

    sl::BodyTrackingRuntimeParameters bt_rt_params;
    sl::Bodies bodies;

    //// Acquisition start time
    if (!src->is_started) {
        GstClock *start_clock = gst_element_get_clock(GST_ELEMENT(src));
        if (start_clock) {
            src->acq_start_time = gst_clock_get_time(start_clock);
            gst_object_unref(start_clock);
        }
        src->is_started = TRUE;
    }

    // ----> Set runtime parameters
    gst_zedsrc_setup_runtime_parameters(src, zedRtParams);
    // <---- Set runtime parameters

    /// Push zed cuda context as current
    int cu_err = (int) cudaGetLastError();
    if (cu_err > 0) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("Cuda ERROR trigger before ZED SDK : %d", cu_err),
                          (NULL));
        return GST_FLOW_ERROR;
    }

    zctx = src->zed.getCUDAContext();
    cuCtxPushCurrent_v2(zctx);

    /// Utils for check ret value and send to out
#define CHECK_RET_OR_GOTO(_ret_expr)                                                               \
    do {                                                                                           \
        ret = (_ret_expr);                                                                         \
        if (ret != sl::ERROR_CODE::SUCCESS) {                                                      \
            GST_ELEMENT_ERROR(src, RESOURCE, FAILED,                                               \
                              ("Grabbing failed with error: '%s' - %s", sl::toString(ret).c_str(), \
                               sl::toVerbose(ret).c_str()),                                        \
                              (NULL));                                                             \
            flow_ret = GST_FLOW_ERROR;                                                             \
            goto out;                                                                              \
        }                                                                                          \
    } while (0)

    // ----> ZED grab
    ret = src->zed.grab(zedRtParams);
    if (ret > sl::ERROR_CODE::SUCCESS) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED,
                          ("Grabbing failed with error: '%s' - %s", sl::toString(ret).c_str(),
                           sl::toVerbose(ret).c_str()),
                          (NULL));
        flow_ret = GST_FLOW_ERROR;
        goto out;
    }
    // <---- ZED grab

    // ----> Clock update
    clock = gst_element_get_clock(GST_ELEMENT(src));
    if (clock) {
        clock_time = gst_clock_get_time(clock);
        gst_object_unref(clock);
        clock = nullptr;
    }
    // <---- Clock update

    // Memory mapping
    if (FALSE == gst_buffer_map(buf, &minfo, GST_MAP_WRITE)) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("Failed to map buffer for writing"), (NULL));
        flow_ret = GST_FLOW_ERROR;
        goto out;
    }
    mapped = TRUE;

    // ----> Mats retrieving
    // NOTE: NV12 zero-copy modes (GST_ZEDSRC_RAW_NV12, GST_ZEDSRC_RAW_NV12_STEREO) are handled
    // by gst_zedsrc_create() which wraps NvBufSurface directly without memcpy.
    // This fill() function only handles non-NVMM stream types.
    if (src->stream_type == GST_ZEDSRC_ONLY_LEFT) {
        CHECK_RET_OR_GOTO(src->zed.retrieveImage(left_img, sl::VIEW::LEFT, sl::MEM::CPU));
    } else if (src->stream_type == GST_ZEDSRC_ONLY_RIGHT) {
        CHECK_RET_OR_GOTO(src->zed.retrieveImage(left_img, sl::VIEW::RIGHT, sl::MEM::CPU));
    } else if (src->stream_type == GST_ZEDSRC_LEFT_RIGHT) {
        CHECK_RET_OR_GOTO(src->zed.retrieveImage(left_img, sl::VIEW::LEFT, sl::MEM::CPU));
        CHECK_RET_OR_GOTO(src->zed.retrieveImage(right_img, sl::VIEW::RIGHT, sl::MEM::CPU));
    } else if (src->stream_type == GST_ZEDSRC_DEPTH_16) {
        CHECK_RET_OR_GOTO(
            src->zed.retrieveMeasure(depth_data, sl::MEASURE::DEPTH_U16_MM, sl::MEM::CPU));
    } else if (src->stream_type == GST_ZEDSRC_LEFT_DEPTH) {
        CHECK_RET_OR_GOTO(src->zed.retrieveImage(left_img, sl::VIEW::LEFT, sl::MEM::CPU));
        CHECK_RET_OR_GOTO(src->zed.retrieveMeasure(depth_data, sl::MEASURE::DEPTH, sl::MEM::CPU));
    }

    /* --- Memory copy into GstBuffer ------------------------------------ */
    if (src->stream_type == GST_ZEDSRC_DEPTH_16) {
        memcpy(minfo.data, depth_data.getPtr<sl::ushort1>(), minfo.size);
    } else if (src->stream_type == GST_ZEDSRC_LEFT_RIGHT) {
        /* Left RGB data on half top */
        memcpy(minfo.data, left_img.getPtr<sl::uchar4>(), minfo.size / 2);
        /* Right RGB data on half bottom */
        memcpy(minfo.data + minfo.size / 2, right_img.getPtr<sl::uchar4>(), minfo.size / 2);
    } else if (src->stream_type == GST_ZEDSRC_LEFT_DEPTH) {
        /* RGB data on half top */
        memcpy(minfo.data, left_img.getPtr<sl::uchar4>(), minfo.size / 2);

        /* Depth data on half bottom */
        {
            uint32_t *gst_data = (uint32_t *) (minfo.data + minfo.size / 2);
            sl::float1 *depthDataPtr = depth_data.getPtr<sl::float1>();

            for (unsigned long i = 0; i < minfo.size / 8; i++) {
                *(gst_data++) = static_cast<uint32_t>(*(depthDataPtr++));
            }
        }
    } else {
        memcpy(minfo.data, left_img.getPtr<sl::uchar4>(), minfo.size);
    }
    // <---- Memory copy

    gst_zedsrc_attach_metadata(src, buf, clock_time);

out:
    if (mapped)
        gst_buffer_unmap(buf, &minfo);
    cuCtxPopCurrent_v2(NULL);

    if (flow_ret != GST_FLOW_OK)
        return flow_ret;

    if (src->stop_requested)
        return GST_FLOW_FLUSHING;

    return GST_FLOW_OK;
}

#ifdef SL_ENABLE_ADVANCED_CAPTURE_API
/**
 * @brief Destroy callback for RawBuffer attached to GstBuffer
 * 
 * This is called when the GstBuffer is unreffed, releasing the RawBuffer
 * back to the ZED SDK capture pipeline.
 */
static void raw_buffer_destroy_notify(gpointer data) {
    sl::RawBuffer *raw = static_cast<sl::RawBuffer *>(data);
    if (raw) {
        GST_DEBUG("Releasing RawBuffer back to SDK");
        delete raw;
    }
}

/**
 * @brief Create function for NVMM zero-copy mode
 * 
 * Unlike fill(), this creates a new GstBuffer wrapping the DMA-BUF FD
 * from the NvBufSurface directly, enabling true zero-copy to downstream
 * elements like nvvidconv, nv3dsink, nvv4l2h265enc.
 */
static GstFlowReturn gst_zedsrc_create(GstPushSrc *psrc, GstBuffer **outbuf) {
    GstZedSrc *src = GST_ZED_SRC(psrc);
    
    // For non-NVMM modes, fall back to the fill() path
    // We must allocate the buffer ourselves and call fill() directly,
    // because delegating to parent->create doesn't work correctly when
    // both create and fill vmethods are set on the class.
    if (src->stream_type != GST_ZEDSRC_RAW_NV12 && src->stream_type != GST_ZEDSRC_RAW_NV12_STEREO) {
        GstBuffer *buf;
        GstFlowReturn ret;
        GstBaseSrc *basesrc = GST_BASE_SRC(psrc);
        GstBufferPool *pool;
        
        // Try to use buffer pool if available
        pool = gst_base_src_get_buffer_pool(basesrc);
        if (pool) {
            ret = gst_buffer_pool_acquire_buffer(pool, &buf, NULL);
            gst_object_unref(pool);
        } else {
            // Allocate buffer directly
            buf = gst_buffer_new_allocate(NULL, src->out_framesize, NULL);
            ret = (buf != NULL) ? GST_FLOW_OK : GST_FLOW_ERROR;
        }
        
        if (ret != GST_FLOW_OK || buf == NULL) {
            GST_ERROR_OBJECT(src, "Failed to allocate buffer");
            return GST_FLOW_ERROR;
        }
        
        // Fill the buffer using our fill function
        ret = gst_zedsrc_fill(psrc, buf);
        if (ret != GST_FLOW_OK) {
            gst_buffer_unref(buf);
            return ret;
        }
        
        *outbuf = buf;
        return GST_FLOW_OK;
    }

    GST_TRACE_OBJECT(src, "gst_zedsrc_create (NVMM zero-copy)");

    sl::ERROR_CODE ret;
    GstFlowReturn flow_ret = GST_FLOW_OK;
    GstClock *clock = nullptr;
    GstClockTime clock_time = GST_CLOCK_TIME_NONE;
    CUcontext zctx;

    // Acquisition start time
    if (!src->is_started) {
        GstClock *start_clock = gst_element_get_clock(GST_ELEMENT(src));
        if (start_clock) {
            src->acq_start_time = gst_clock_get_time(start_clock);
            gst_object_unref(start_clock);
        }
        src->is_started = TRUE;
    }

    // Runtime parameters - Unified path
    sl::RuntimeParameters zedRtParams;
    gst_zedsrc_setup_runtime_parameters(src, zedRtParams);

    // Grab frame
    if (cuCtxPushCurrent_v2(src->zed.getCUDAContext()) != CUDA_SUCCESS) {
        GST_ERROR_OBJECT(src, "Failed to push CUDA context");
        return GST_FLOW_ERROR;
    }

    ret = src->zed.grab(zedRtParams);
    if (ret == sl::ERROR_CODE::END_OF_SVOFILE_REACHED) {
        GST_INFO_OBJECT(src, "End of SVO file");
        cuCtxPopCurrent_v2(NULL);
        return GST_FLOW_EOS;
    } else if (ret != sl::ERROR_CODE::SUCCESS) {
        GST_ERROR_OBJECT(src, "grab() failed: %s", sl::toString(ret).c_str());
        cuCtxPopCurrent_v2(NULL);
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
    ret = src->zed.retrieveImage(*raw_buffer);
    if (ret != sl::ERROR_CODE::SUCCESS) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED,
                          ("Failed to retrieve RawBuffer: '%s'", sl::toString(ret).c_str()), (NULL));
        delete raw_buffer;
        cuCtxPopCurrent_v2(NULL);
        return GST_FLOW_ERROR;
    }

    // Get NvBufSurface
    NvBufSurface *nvbuf = static_cast<NvBufSurface *>(raw_buffer->getRawBuffer());
    if (!nvbuf) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("RawBuffer returned null NvBufSurface"), (NULL));
        delete raw_buffer;
        cuCtxPopCurrent_v2(NULL);
        return GST_FLOW_ERROR;
    }

    if (nvbuf->numFilled == 0) {
        GST_WARNING_OBJECT(src, "NvBufSurface has no filled surfaces");
        delete raw_buffer;
        cuCtxPopCurrent_v2(NULL);
        // We cannot return NO_BUFFER here easily without implementing a retry loop.
        // For debugging purposes, let's fail softly.
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("NvBufSurface empty"), (NULL)); 
        return GST_FLOW_ERROR;
    }

    // Log buffer info
    NvBufSurfaceParams *params = &nvbuf->surfaceList[0];
    GST_DEBUG_OBJECT(src, "NvBufSurface: %p, FD: %ld, size: %d, memType: %d, format: %d",
                     nvbuf, params->bufferDesc, params->dataSize, nvbuf->memType, params->colorFormat);

    // Create GstBuffer wrapping the NvBufSurface pointer directly
    // This is the NVIDIA convention: the buffer's memory data IS the NvBufSurface*
    // DeepStream and other NVIDIA elements expect this format
    GstBuffer *buf = gst_buffer_new_wrapped_full(
        (GstMemoryFlags)0,          // NVIDIA elements require writable memory even for reading   // NVIDIA elements require writable memory even for reading
        nvbuf,                      // Data pointer is the NvBufSurface*
        sizeof(NvBufSurface),       // Max size
        0,                          // Offset
        sizeof(NvBufSurface),       // Size
        raw_buffer,                 // User data for destroy callback
        raw_buffer_destroy_notify   // Called when buffer is unreffed
    );

    // Attach Unified Metadata
    gst_zedsrc_attach_metadata(src, buf, clock_time);

    cuCtxPopCurrent_v2(NULL);

    if (src->stop_requested) {
        gst_buffer_unref(buf);
        return GST_FLOW_FLUSHING;
    }

    *outbuf = buf;
    return GST_FLOW_OK;
}
#endif // SL_ENABLE_ADVANCED_CAPTURE_API

static gboolean plugin_init(GstPlugin *plugin) {
    GST_DEBUG_CATEGORY_INIT(gst_zedsrc_debug, "zedsrc", 0, "debug category for zedsrc element");
    GST_DEBUG_CATEGORY_INIT(gst_zedsrc_tracking_debug, "zedsrc-tracking", 0,
                            "zedsrc positional tracking debug");
    GST_DEBUG_CATEGORY_INIT(gst_zedsrc_od_debug, "zedsrc-od", 0, "zedsrc object detection debug");
    GST_DEBUG_CATEGORY_INIT(gst_zedsrc_controls_debug, "zedsrc-controls", 0,
                            "zedsrc camera controls debug");
    gst_element_register(plugin, "zedsrc", GST_RANK_NONE, gst_zedsrc_get_type());

    return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, zedsrc, "Zed camera source", plugin_init,
                  GST_PACKAGE_VERSION, GST_PACKAGE_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
