// /////////////////////////////////////////////////////////////////////////

//
// Copyright (c) 2020, STEREOLABS.
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

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>

#include "gstzedsrc.h"
#include "gst-zed-meta/gstzedmeta.h"

GST_DEBUG_CATEGORY_STATIC (gst_zedsrc_debug);
#define GST_CAT_DEFAULT gst_zedsrc_debug

/* prototypes */
static void gst_zedsrc_set_property (GObject * object,
                                     guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_zedsrc_get_property (GObject * object,
                                     guint property_id, GValue * value, GParamSpec * pspec);
static void gst_zedsrc_dispose (GObject * object);
static void gst_zedsrc_finalize (GObject * object);

static gboolean gst_zedsrc_start (GstBaseSrc * src);
static gboolean gst_zedsrc_stop (GstBaseSrc * src);
static GstCaps* gst_zedsrc_get_caps (GstBaseSrc * src, GstCaps * filter);
static gboolean gst_zedsrc_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_zedsrc_unlock (GstBaseSrc * src);
static gboolean gst_zedsrc_unlock_stop (GstBaseSrc * src);

static GstFlowReturn gst_zedsrc_fill (GstPushSrc * src, GstBuffer * buf);

enum
{
    PROP_0,
    PROP_CAM_RES,
    PROP_CAM_FPS,
    PROP_STREAM_TYPE,
    PROP_SDK_VERBOSE,
    PROP_CAM_FLIP,
    PROP_CAM_ID,
    PROP_CAM_SN,
    PROP_SVO_FILE,
    PROP_STREAM_IP,
    PROP_STREAM_PORT,
    PROP_DEPTH_MIN,
    PROP_DEPTH_MAX,
    PROP_DEPTH_MODE,
    PROP_DIS_SELF_CALIB,
    //PROP_RIGHT_DEPTH_ENABLE,
    PROP_DEPTH_STAB,
    PROP_CONFIDENCE_THRESH,
    PROP_TEXTURE_CONF_THRESH,
    PROP_3D_REF_FRAME,
    PROP_SENSING_MODE,
    PROP_POS_TRACKING,
    PROP_CAMERA_STATIC,
    PROP_POS_AREA_FILE_PATH,
    PROP_POS_ENABLE_AREA_MEMORY,
    PROP_POS_ENABLE_IMU_FUSION,
    PROP_POS_ENABLE_POSE_SMOOTHING,
    PROP_POS_SET_FLOOR_AS_ORIGIN,
    PROP_POS_INIT_X,
    PROP_POS_INIT_Y,
    PROP_POS_INIT_Z,
    PROP_POS_INIT_ROLL,
    PROP_POS_INIT_PITCH,
    PROP_POS_INIT_YAW,
    PROP_COORD_SYS,
    PROP_OD_ENABLE,
    PROP_OD_IMAGE_SYNC,
    PROP_OD_TRACKING,
    PROP_OD_MASK,
    PROP_OD_DET_MODEL,
    PROP_OD_CONFIDENCE,
    PROP_OD_MAX_RANGE,
    PROP_OD_BODY_FITTING,
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
    PROP_LEDSTATUS,
    N_PROPERTIES
};

typedef enum {
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
    GST_ZEDSRC_LEFT_DEPTH = 4
} GstZedSrcStreamType;

typedef enum {
    GST_ZEDSRC_COORD_IMAGE = 0,
    GST_ZEDSRC_COORD_LEFT_HANDED_Y_UP = 1,
    GST_ZEDSRC_COORD_RIGHT_HANDED_Y_UP  = 2,
    GST_ZEDSRC_COORD_RIGHT_HANDED_Z_UP = 3,
    GST_ZEDSRC_COORD_LEFT_HANDED_Z_UP = 4,
    GST_ZEDSRC_COORD_RIGHT_HANDED_Z_UP_X_FWD = 5
} GstZedSrcCoordSys;

typedef enum {
    GST_ZEDSRC_OD_MULTI_CLASS_BOX = 0,
    GST_ZEDSRC_OD_MULTI_CLASS_BOX_ACCURATE = 1,
    GST_ZEDSRC_OD_HUMAN_BODY_FAST = 2,
    GST_ZEDSRC_OD_HUMAN_BODY_ACCURATE  = 3
} GstZedSrcOdModel;

typedef enum {
    GST_ZEDSRC_SIDE_LEFT= 0,
    GST_ZEDSRC_SIDE_RIGHT = 1,
    GST_ZEDSRC_SIDE_BOTH = 2
} GstZedSrcSide;

//////////////// DEFAULT PARAMETERS //////////////////////////////////////////////////////////////////////////

// INITIALIZATION
#define DEFAULT_PROP_CAM_RES                    static_cast<gint>(sl::RESOLUTION::HD1080)
#define DEFAULT_PROP_CAM_FPS                    GST_ZEDSRC_30FPS
#define DEFAULT_PROP_SDK_VERBOSE                FALSE
#define DEFAULT_PROP_CAM_FLIP                   2
#define DEFAULT_PROP_CAM_ID                     0
#define DEFAULT_PROP_CAM_SN                     0
#define DEFAULT_PROP_SVO_FILE                   ""
#define DEFAULT_PROP_STREAM_IP                  ""
#define DEFAULT_PROP_STREAM_PORT                30000
#define DEFAULT_PROP_STREAM_TYPE                0
#define DEFAULT_PROP_DEPTH_MIN                  300.f
#define DEFAULT_PROP_DEPTH_MAX                  20000.f
#define DEFAULT_PROP_DEPTH_MODE                 static_cast<gint>(sl::DEPTH_MODE::NONE)
#define DEFAULT_PROP_COORD_SYS                  static_cast<gint>(sl::COORDINATE_SYSTEM::IMAGE)
#define DEFAULT_PROP_DIS_SELF_CALIB             FALSE
#define DEFAULT_PROP_DEPTH_STAB                 TRUE
//#define DEFAULT_PROP_RIGHT_DEPTH              FALSE

// RUNTIME
#define DEFAULT_PROP_CONFIDENCE_THRESH          80
#define DEFAULT_PROP_TEXTURE_CONF_THRESH        100
#define DEFAULT_PROP_3D_REF_FRAME               static_cast<gint>(sl::REFERENCE_FRAME::WORLD)
#define DEFAULT_PROP_SENSING_MODE               static_cast<gint>(sl::SENSING_MODE::STANDARD)

// POSITIONAL TRACKING
#define DEFAULT_PROP_POS_TRACKING               FALSE
#define DEFAULT_PROP_CAMERA_STATIC              FALSE
#define DEFAULT_PROP_POS_AREA_FILE_PATH         ""
#define DEFAULT_PROP_POS_ENABLE_AREA_MEMORY     TRUE
#define DEFAULT_PROP_POS_ENABLE_IMU_FUSION      TRUE
#define DEFAULT_PROP_POS_ENABLE_POSE_SMOOTHING  TRUE
#define DEFAULT_PROP_POS_SET_FLOOR_AS_ORIGIN    FALSE
#define DEFAULT_PROP_POS_INIT_X                 0.0
#define DEFAULT_PROP_POS_INIT_Y                 0.0
#define DEFAULT_PROP_POS_INIT_Z                 0.0
#define DEFAULT_PROP_POS_INIT_ROLL              0.0
#define DEFAULT_PROP_POS_INIT_PITCH             0.0
#define DEFAULT_PROP_POS_INIT_YAW               0.0

//OBJECT DETECTION
#define DEFAULT_PROP_OD_ENABLE                  FALSE
#define DEFAULT_PROP_OD_SYNC                    TRUE
#define DEFAULT_PROP_OD_TRACKING                TRUE
#define DEFAULT_PROP_OD_MASK                    FALSE // NOTE for the future
#define DEFAULT_PROP_OD_MODEL                   GST_ZEDSRC_OD_MULTI_CLASS_BOX
#define DEFAULT_PROP_OD_CONFIDENCE              50.0
#define DEFAULT_PROP_OD_MAX_RANGE               DEFAULT_PROP_DEPTH_MAX
#define DEFAULT_PROP_OD_BODY_FITTING            TRUE

// CAMERA CONTROLS
#define DEFAULT_PROP_BRIGHTNESS                 4
#define DEFAULT_PROP_CONTRAST                   4
#define DEFAULT_PROP_HUE                        0
#define DEFAULT_PROP_SATURATION                 4
#define DEFAULT_PROP_SHARPNESS                  4
#define DEFAULT_PROP_GAMMA                      8
#define DEFAULT_PROP_GAIN                       60
#define DEFAULT_PROP_EXPOSURE                   80
#define DEFAULT_PROP_AEG_AGC                    1
#define DEFAULT_PROP_AEG_AGC_ROI_X              -1
#define DEFAULT_PROP_AEG_AGC_ROI_Y              -1
#define DEFAULT_PROP_AEG_AGC_ROI_W              -1
#define DEFAULT_PROP_AEG_AGC_ROI_H              -1
#define DEFAULT_PROP_AEG_AGC_ROI_SIDE           GST_ZEDSRC_SIDE_BOTH
#define DEFAULT_PROP_WHITEBALANCE               4600
#define DEFAULT_PROP_WHITEBALANCE_AUTO          1
#define DEFAULT_PROP_LEDSTATUS                  1
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define GST_TYPE_ZED_SIDE (gst_zedsrc_side_get_type ())
static GType gst_zedsrc_side_get_type (void)
{
    static GType zedsrc_side_type = 0;

    if (!zedsrc_side_type) {
        static GEnumValue pattern_types[] = {
            { static_cast<gint>(sl::SIDE::LEFT),
              "Left side only",
              "LEFT" },
            { static_cast<gint>(sl::SIDE::RIGHT),
              "Right side only",
              "RIGHT"  },
            { static_cast<gint>(sl::SIDE::BOTH),
              "Left and Right side",
              "BOTH"},
            { 0, NULL, NULL },
        };

        zedsrc_side_type = g_enum_register_static( "GstZedsrcSide",
                                                   pattern_types);
    }

    return zedsrc_side_type;
}

#define GST_TYPE_ZED_RESOL (gst_zedsrc_resol_get_type ())
static GType gst_zedsrc_resol_get_type (void)
{
    static GType zedsrc_resol_type = 0;

    if (!zedsrc_resol_type) {
        static GEnumValue pattern_types[] = {
            { static_cast<gint>(sl::RESOLUTION::VGA),
              "672x376",
              "VGA" },
            { static_cast<gint>(sl::RESOLUTION::HD720),
              "1280x720",
              "HD720"  },
            { static_cast<gint>(sl::RESOLUTION::HD1080),
              "1920x1080",
              "HD1080" },
            { static_cast<gint>(sl::RESOLUTION::HD2K),
              "2208x1242",
              "HD2K" },
            { 0, NULL, NULL },
        };

        zedsrc_resol_type = g_enum_register_static( "GstZedsrcResolution",
                                                    pattern_types);
    }

    return zedsrc_resol_type;
}

#define GST_TYPE_ZED_FPS (gst_zedsrc_fps_get_type ())
static GType gst_zedsrc_fps_get_type (void)
{
    static GType zedsrc_fps_type = 0;

    if (!zedsrc_fps_type) {
        static GEnumValue pattern_types[] = {
            { GST_ZEDSRC_100FPS,
              "only VGA resolution",
              "100 FPS" },
            { GST_ZEDSRC_60FPS,
              "only VGA and HD720 resolutions",
              "60  FPS" },
            { GST_ZEDSRC_30FPS,
              "only VGA, HD720 and HD1080 resolutions",
              "30  FPS" },
            { GST_ZEDSRC_15FPS,
              "all resolutions",
              "15  FPS" },
            { 0, NULL, NULL },
        };

        zedsrc_fps_type = g_enum_register_static( "GstZedSrcFPS",
                                                  pattern_types);
    }

    return zedsrc_fps_type;
}

#define GST_TYPE_ZED_FLIP (gst_zedsrc_flip_get_type ())
static GType gst_zedsrc_flip_get_type (void)
{
    static GType zedsrc_flip_type = 0;

    if (!zedsrc_flip_type) {
        static GEnumValue pattern_types[] = {
            { GST_ZEDSRC_NO_FLIP,
              "Force no flip",
              "No Flip" },
            { GST_ZEDSRC_FLIP,
              "Force flip",
              "Flip" },
            { GST_ZEDSRC_AUTO,
              "Auto mode (ZED2/ZED-M only)",
              "Auto" },
            { 0, NULL, NULL },
        };

        zedsrc_flip_type = g_enum_register_static( "GstZedSrcFlip",
                                                   pattern_types);
    }

    return zedsrc_flip_type;
}

#define GST_TYPE_ZED_STREAM_TYPE (gst_zedsrc_stream_type_get_type ())
static GType gst_zedsrc_stream_type_get_type (void)
{
    static GType zedsrc_stream_type_type = 0;

    if (!zedsrc_stream_type_type) {
        static GEnumValue pattern_types[] = {
            { GST_ZEDSRC_ONLY_LEFT,
              "8 bits- 4 channels Left image",
              "Left image [BGRA]" },
            { GST_ZEDSRC_ONLY_RIGHT,
              "8 bits- 4 channels Right image",
              "Right image [BGRA]"  },
            { GST_ZEDSRC_LEFT_RIGHT,
              "8 bits- 4 channels bit Left and Right",
              "Stereo couple up/down [BGRA]" },
            { GST_ZEDSRC_DEPTH_16,
              "16 bits depth",
              "Depth image [GRAY16_LE]" },
            { GST_ZEDSRC_LEFT_DEPTH,
              "8 bits- 4 channels Left and Depth(image)",
              "Left and Depth up/down [BGRA]" },
            { 0, NULL, NULL },
        };

        zedsrc_stream_type_type = g_enum_register_static( "GstZedSrcCoordSys",
                                                          pattern_types);
    }

    return zedsrc_stream_type_type;
}

#define GST_TYPE_ZED_COORD_SYS (gst_zedsrc_coord_sys_get_type ())
static GType gst_zedsrc_coord_sys_get_type (void)
{
    static GType zedsrc_coord_sys_type = 0;

    if (!zedsrc_coord_sys_type) {
        static GEnumValue pattern_types[] = {
            { GST_ZEDSRC_COORD_IMAGE,
              "Standard coordinates system in computer vision. Used in OpenCV.",
              "Image" },
            { GST_ZEDSRC_COORD_LEFT_HANDED_Y_UP,
              "Left-Handed with Y up and Z forward. Used in Unity with DirectX.",
              "Left handed, Y up" },
            { GST_ZEDSRC_COORD_RIGHT_HANDED_Y_UP,
              "Right-Handed with Y pointing up and Z backward. Used in OpenGL.",
              "Right handed, Y up" },
            { GST_ZEDSRC_COORD_RIGHT_HANDED_Z_UP,
              "Right-Handed with Z pointing up and Y forward. Used in 3DSMax.",
              "Right handed, Z up" },
            { GST_ZEDSRC_COORD_LEFT_HANDED_Z_UP,
              "Left-Handed with Z axis pointing up and X forward. Used in Unreal Engine.",
              "Left handed, Z up" },
            { GST_ZEDSRC_COORD_RIGHT_HANDED_Z_UP_X_FWD,
              "Right-Handed with Z pointing up and X forward. Used in ROS (REP 103).",
              "Right handed, Z up, X fwd"  },
            { 0, NULL, NULL },
        };

        zedsrc_coord_sys_type = g_enum_register_static( "GstZedsrcStreamType",
                                                        pattern_types);
    }

    return zedsrc_coord_sys_type;
}

#define GST_TYPE_ZED_OD_MODEL_TYPE (gst_zedsrc_od_model_get_type ())
static GType gst_zedsrc_od_model_get_type (void)
{
    static GType zedsrc_od_model_type = 0;

    if (!zedsrc_od_model_type) {
        static GEnumValue pattern_types[] = {
            { GST_ZEDSRC_OD_MULTI_CLASS_BOX,
              "Any objects, bounding box based",
              "Object Detection Multi class" },
            { GST_ZEDSRC_OD_MULTI_CLASS_BOX_ACCURATE,
              "Any objects, bounding box based, state of the art accuracy, requires powerful GPU",
              "Object Detection Multi class ACCURATE" },
            { GST_ZEDSRC_OD_HUMAN_BODY_FAST,
              "Keypoints based, specific to human skeleton, real time performance even on Jetson or low end GPU cards",
              "Skeleton tracking FAST" },
            { GST_ZEDSRC_OD_HUMAN_BODY_ACCURATE,
              "Keypoints based, specific to human skeleton, state of the art accuracy, requires powerful GPU",
              "Skeleton tracking ACCURATE"  },
            { 0, NULL, NULL },
        };

        zedsrc_od_model_type = g_enum_register_static( "GstZedSrcOdModel",
                                                       pattern_types);
    }

    return zedsrc_od_model_type;
}

#define GST_TYPE_ZED_DEPTH_MODE (gst_zedsrc_depth_mode_get_type ())
static GType gst_zedsrc_depth_mode_get_type (void)
{
    static GType zedsrc_depth_mode_type = 0;

    if (!zedsrc_depth_mode_type) {
        static GEnumValue pattern_types[] = {
            { static_cast<gint>(sl::DEPTH_MODE::ULTRA),
              "Computation mode favorising edges and sharpness. Requires more GPU memory and computation power.",
              "ULTRA" },
            { static_cast<gint>(sl::DEPTH_MODE::QUALITY),
              "Computation mode designed for challenging areas with untextured surfaces.",
              "QUALITY" },
            { static_cast<gint>(sl::DEPTH_MODE::PERFORMANCE),
              "Computation mode optimized for speed.",
              "PERFORMANCE" },
            { static_cast<gint>(sl::DEPTH_MODE::NONE),
              "This mode does not compute any depth map. Only rectified stereo images will be available.",
              "NONE" },
            { 0, NULL, NULL },
        };

        zedsrc_depth_mode_type = g_enum_register_static( "GstZedsrcDepthMode",
                                                         pattern_types);
    }

    return zedsrc_depth_mode_type;
}

#define GST_TYPE_ZED_3D_REF_FRAME (gst_zedsrc_3d_meas_ref_frame_get_type ())
static GType gst_zedsrc_3d_meas_ref_frame_get_type (void)
{
    static GType zedsrc_3d_meas_ref_frame_type = 0;

    if (!zedsrc_3d_meas_ref_frame_type) {
        static GEnumValue pattern_types[] = {
            { static_cast<gint>(sl::REFERENCE_FRAME::WORLD),
              "The positional tracking pose transform will contains the motion with reference to the world frame.",
              "WORLD" },
            { static_cast<gint>(sl::REFERENCE_FRAME::CAMERA),
              "The  pose transform will contains the motion with reference to the previous camera frame.",
              "CAMERA" },
            { 0, NULL, NULL },
        };

        zedsrc_3d_meas_ref_frame_type = g_enum_register_static( "GstZedsrc3dMeasRefFrame",
                                                                pattern_types);
    }

    return zedsrc_3d_meas_ref_frame_type;
}

#define GST_TYPE_ZED_SENSING_MODE (gst_zedsrc_sensing_mode_get_type ())
static GType gst_zedsrc_sensing_mode_get_type (void)
{
    static GType zedsrc_sensing_mode_type = 0;

    if (!zedsrc_sensing_mode_type) {
        static GEnumValue pattern_types[] = {
            { static_cast<gint>(sl::SENSING_MODE::STANDARD),
              "This mode outputs ZED standard depth map that preserves edges and depth accuracy. Applications example: "
              "Obstacle detection, Automated navigation, People detection, 3D reconstruction, measurements.",
              "STANDARD" },
            { static_cast<gint>(sl::SENSING_MODE::FILL),
              "This mode outputs a smooth and fully dense depth map. Applications example: AR/VR, Mixed-reality capture, "
              "Image post-processing.",
              "FILL" },
            { 0, NULL, NULL },
        };

        zedsrc_sensing_mode_type = g_enum_register_static( "GstZedsrcSensingMode",
                                                           pattern_types);
    }

    return zedsrc_sensing_mode_type;
}



/* pad templates */
static GstStaticPadTemplate gst_zedsrc_src_template =
        GST_STATIC_PAD_TEMPLATE ("src",
                                 GST_PAD_SRC,
                                 GST_PAD_ALWAYS,
                                 GST_STATIC_CAPS( ("video/x-raw, " // Double stream
                                                   "format = (string)BGRA, "
                                                   "width = (int)672, "
                                                   "height = (int)752 , "
                                                   "framerate = (fraction) { 15, 30, 60, 100 }"
                                                   ";"
                                                   "video/x-raw, " // Double stream
                                                   "format = (string)BGRA, "
                                                   "width = (int)1280, "
                                                   "height = (int)1440, "
                                                   "framerate = (fraction) { 15, 30, 60 }"
                                                   ";"
                                                   "video/x-raw, " // Double stream
                                                   "format = (string)BGRA, "
                                                   "width = (int)1920, "
                                                   "height = (int)2160, "
                                                   "framerate = (fraction) { 15, 30 }"
                                                   ";"
                                                   "video/x-raw, " // Double stream
                                                   "format = (string)BGRA, "
                                                   "width = (int)2208, "
                                                   "height = (int)2484, "
                                                   "framerate = (fraction)15"
                                                   ";"
                                                   "video/x-raw, "
                                                   "format = (string)BGRA, "
                                                   "width = (int)672, "
                                                   "height =  (int)376, "
                                                   "framerate = (fraction) { 15, 30, 60, 100 }"
                                                   ";"
                                                   "video/x-raw, "
                                                   "format = (string)BGRA, "
                                                   "width = (int)1280, "
                                                   "height =  (int)720, "
                                                   "framerate =  (fraction)  { 15, 30, 60}"
                                                   ";"
                                                   "video/x-raw, "
                                                   "format = (string)BGRA, "
                                                   "width = (int)1920, "
                                                   "height = (int)1080, "
                                                   "framerate = (fraction) { 15, 30 }"
                                                   ";"
                                                   "video/x-raw, "
                                                   "format = (string)BGRA, "
                                                   "width = (int)2208, "
                                                   "height = (int)1242, "
                                                   "framerate = (fraction)15"
                                                   ";"
                                                   "video/x-raw, "
                                                   "format = (string)GRAY16_LE, "
                                                   "width = (int)672, "
                                                   "height =  (int)376, "
                                                   "framerate = (fraction) { 15, 30, 60, 100 }"
                                                   ";"
                                                   "video/x-raw, "
                                                   "format = (string)GRAY16_LE, "
                                                   "width = (int)1280, "
                                                   "height =  (int)720, "
                                                   "framerate =  (fraction)  { 15, 30, 60}"
                                                   ";"
                                                   "video/x-raw, "
                                                   "format = (string)GRAY16_LE, "
                                                   "width = (int)1920, "
                                                   "height = (int)1080, "
                                                   "framerate = (fraction) { 15, 30 }"
                                                   ";"
                                                   "video/x-raw, "
                                                   "format = (string)GRAY16_LE, "
                                                   "width = (int)2208, "
                                                   "height = (int)1242, "
                                                   "framerate = (fraction)15"
                                                   ";"
                                                   "video/x-raw, " // Double stream
                                                   "format = (string)BGRA, "
                                                   "width = (int)672, "
                                                   "height = (int)752 , "
                                                   "framerate = (fraction) { 15, 30, 60, 100 }"
                                                   ";"
                                                   "video/x-raw, " // Double stream
                                                   "format = (string)BGRA, "
                                                   "width = (int)1280, "
                                                   "height = (int)1440, "
                                                   "framerate = (fraction) { 15, 30, 60 }"
                                                   ";"
                                                   "video/x-raw, " // Double stream
                                                   "format = (string)BGRA, "
                                                   "width = (int)1920, "
                                                   "height = (int)2160, "
                                                   "framerate = (fraction) { 15, 30 }"
                                                   ";"
                                                   "video/x-raw, " // Double stream
                                                   "format = (string)BGRA, "
                                                   "width = (int)2208, "
                                                   "height = (int)2484, "
                                                   "framerate = (fraction)15") ) );

/* class initialization */
G_DEFINE_TYPE( GstZedSrc, gst_zedsrc, GST_TYPE_PUSH_SRC );

static void gst_zedsrc_class_init (GstZedSrcClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
    GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
    GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);

    gobject_class->set_property = gst_zedsrc_set_property;
    gobject_class->get_property = gst_zedsrc_get_property;
    gobject_class->dispose = gst_zedsrc_dispose;
    gobject_class->finalize = gst_zedsrc_finalize;

    gst_element_class_add_pad_template (gstelement_class,
                                        gst_static_pad_template_get (&gst_zedsrc_src_template));

    gst_element_class_set_static_metadata (gstelement_class,
                                           "ZED Camera Source",
                                           "Source/Video",
                                           "Stereolabs ZED Camera source",
                                           "Stereolabs <support@stereolabs.com>");

    gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_zedsrc_start);
    gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_zedsrc_stop);
    gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_zedsrc_get_caps);
    gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_zedsrc_set_caps);
    gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_zedsrc_unlock);
    gstbasesrc_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_zedsrc_unlock_stop);

    gstpushsrc_class->fill = GST_DEBUG_FUNCPTR (gst_zedsrc_fill);

    /* Install GObject properties */
    g_object_class_install_property( gobject_class, PROP_CAM_RES,
                                     g_param_spec_enum("camera-resolution", "Camera Resolution",
                                                       "Camera Resolution", GST_TYPE_ZED_RESOL, DEFAULT_PROP_CAM_RES,
                                                       (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_CAM_FPS,
                                     g_param_spec_enum("camera-fps", "Camera frame rate",
                                                       "Camera frame rate", GST_TYPE_ZED_FPS, DEFAULT_PROP_CAM_FPS,
                                                       (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_STREAM_TYPE,
                                     g_param_spec_enum("stream-type", "Image stream type",
                                                       "Image stream type", GST_TYPE_ZED_STREAM_TYPE,
                                                       DEFAULT_PROP_STREAM_TYPE,
                                                       (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_SDK_VERBOSE,
                                     g_param_spec_boolean("sdk-verbose", "ZED SDK Verbose",
                                                          "ZED SDK Verbose", DEFAULT_PROP_SDK_VERBOSE,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_CAM_FLIP,
                                     g_param_spec_enum("camera-image-flip", "Camera image flip",
                                                       "Use the camera in forced flip/no flip or automatic mode",GST_TYPE_ZED_FLIP,
                                                       DEFAULT_PROP_CAM_FLIP,
                                                       (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_CAM_ID,
                                     g_param_spec_int("camera-id", "Camera ID",
                                                      "Select camera from cameraID",0,255,
                                                      DEFAULT_PROP_CAM_ID,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_CAM_SN,
                                     g_param_spec_int64("camera-sn", "Camera Serial Number",
                                                        "Select camera from camera serial number",0,G_MAXINT64,
                                                        DEFAULT_PROP_CAM_SN,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_SVO_FILE,
                                     g_param_spec_string("svo-file-path", "SVO file",
                                                         "Input from SVO file",
                                                         DEFAULT_PROP_SVO_FILE,
                                                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_STREAM_IP,
                                     g_param_spec_string("input-stream-ip", "Input Stream IP",
                                                         "Specify IP adress when using streaming input",
                                                         DEFAULT_PROP_SVO_FILE,
                                                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_STREAM_PORT,
                                     g_param_spec_int("input-stream-port", "Input Stream Port",
                                                      "Specify port when using streaming input",1,G_MAXUINT16,
                                                      DEFAULT_PROP_STREAM_PORT,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_DEPTH_MIN,
                                     g_param_spec_float("depth-minimum-distance", "Minimum depth value",
                                                        "Minimum depth value", 100.f, 3000.f, DEFAULT_PROP_DEPTH_MIN,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_DEPTH_MAX,
                                     g_param_spec_float("depth-maximum-distance", "Maximum depth value",
                                                        "Maximum depth value", 500.f, 40000.f, DEFAULT_PROP_DEPTH_MAX,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_DEPTH_MODE,
                                     g_param_spec_enum("depth-mode", "Depth Mode",
                                                       "Depth Mode", GST_TYPE_ZED_DEPTH_MODE, DEFAULT_PROP_DEPTH_MODE,
                                                       (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_DIS_SELF_CALIB,
                                     g_param_spec_boolean("camera-disable-self-calib", "Disable self calibration",
                                                          "Disable the self calibration processing when the camera is opened",
                                                          DEFAULT_PROP_DIS_SELF_CALIB,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    /*g_object_class_install_property( gobject_class, PROP_RIGHT_DEPTH_ENABLE,
                                     g_param_spec_boolean("enable-right-side-measure", "Enable right side measure",
                                                          "Enable the MEASURE::DEPTH_RIGHT and other MEASURE::<XXX>_RIGHT at the cost of additional computation time",
                                                          DEFAULT_PROP_RIGHT_DEPTH,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));*/

    g_object_class_install_property( gobject_class, PROP_DEPTH_STAB,
                                     g_param_spec_boolean("depth-stabilization", "Depth stabilization",
                                                          "Enable depth stabilization",
                                                          DEFAULT_PROP_DEPTH_STAB,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_COORD_SYS,
                                     g_param_spec_enum("coordinate-system", "SDK Coordinate System",
                                                       "3D Coordinate System", GST_TYPE_ZED_COORD_SYS,
                                                       DEFAULT_PROP_COORD_SYS,
                                                       (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));



    g_object_class_install_property( gobject_class, PROP_CONFIDENCE_THRESH,
                                     g_param_spec_int("confidence-threshold", "Depth Confidence Threshold",
                                                      "Specify the Depth Confidence Threshold",0,100,
                                                      DEFAULT_PROP_CONFIDENCE_THRESH,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_TEXTURE_CONF_THRESH,
                                     g_param_spec_int("texture-confidence-threshold", "Texture Confidence Threshold",
                                                      "Specify the Texture Confidence Threshold",0,100,
                                                      DEFAULT_PROP_TEXTURE_CONF_THRESH,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_3D_REF_FRAME,
                                     g_param_spec_enum("measure3D-reference-frame", "3D Measures Reference Frame",
                                                       "Specify the 3D Reference Frame", GST_TYPE_ZED_3D_REF_FRAME,
                                                       DEFAULT_PROP_3D_REF_FRAME,
                                                       (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_SENSING_MODE,
                                     g_param_spec_enum("sensing-mode", "Depth Sensing Mode",
                                                       "Specify the Depth Sensing Mode", GST_TYPE_ZED_SENSING_MODE,
                                                       DEFAULT_PROP_SENSING_MODE,
                                                       (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));



    g_object_class_install_property( gobject_class, PROP_POS_TRACKING,
                                     g_param_spec_boolean("enable-positional-tracking", "Positional tracking",
                                                          "Enable positional tracking",
                                                          DEFAULT_PROP_POS_TRACKING,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_CAMERA_STATIC,
                                     g_param_spec_boolean("set-as-static", "Camera static",
                                                          "Set to TRUE if the camera is static",
                                                          DEFAULT_PROP_CAMERA_STATIC,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_POS_AREA_FILE_PATH,
                                     g_param_spec_string("area-file-path", "Area file path",
                                                         "Area localization file that describes the surroundings, saved"
                                                         " from a previous tracking session.",
                                                         DEFAULT_PROP_POS_AREA_FILE_PATH,
                                                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_POS_ENABLE_AREA_MEMORY,
                                     g_param_spec_boolean("enable-area-memory", "Enable area memory",
                                                          "This mode enables the camera to remember its surroundings. "
                                                          "This helps correct positional tracking drift, and can be "
                                                          "helpful for positioning different cameras relative to one "
                                                          "other in space.",
                                                          DEFAULT_PROP_POS_ENABLE_AREA_MEMORY,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_POS_ENABLE_IMU_FUSION,
                                     g_param_spec_boolean("enable-imu-fusion", "Enable IMU fusion",
                                                          "This setting allows you to enable or disable IMU fusion. "
                                                          "When set to false, only the optical odometry will be used.",
                                                          DEFAULT_PROP_POS_ENABLE_IMU_FUSION,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_POS_ENABLE_POSE_SMOOTHING,
                                     g_param_spec_boolean("enable-pose-smoothing", "Enable Pose Smoothing",
                                                          "This mode enables smooth pose correction for small drift "
                                                          "correction.",
                                                          DEFAULT_PROP_POS_ENABLE_POSE_SMOOTHING,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_POS_SET_FLOOR_AS_ORIGIN,
                                     g_param_spec_boolean("set-floor-as-origin", "Set floor as pose origin",
                                                          "This mode initializes the tracking to be aligned with the "
                                                          "floor plane to better position the camera in space.",
                                                          DEFAULT_PROP_POS_SET_FLOOR_AS_ORIGIN,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_POS_INIT_X,
                                     g_param_spec_float("initial-world-transform-x",
                                                        "Initial X coordinate",
                                                        "X position of the camera in the world frame when the camera is started",
                                                        G_MINFLOAT, G_MAXFLOAT,
                                                        DEFAULT_PROP_POS_INIT_X,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_POS_INIT_Y,
                                     g_param_spec_float("initial-world-transform-y",
                                                        "Initial Y coordinate",
                                                        "Y position of the camera in the world frame when the camera is started",
                                                        G_MINFLOAT, G_MAXFLOAT,
                                                        DEFAULT_PROP_POS_INIT_Y,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_POS_INIT_Z,
                                     g_param_spec_float("initial-world-transform-z",
                                                        "Initial Z coordinate",
                                                        "Z position of the camera in the world frame when the camera is started",
                                                        G_MINFLOAT, G_MAXFLOAT,
                                                        DEFAULT_PROP_POS_INIT_Z,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_POS_INIT_ROLL,
                                     g_param_spec_float("initial-world-transform-roll",
                                                        "Initial Roll orientation",
                                                        "Roll orientation of the camera in the world frame when the camera is started",
                                                        0.0f, 360.0f,
                                                        DEFAULT_PROP_POS_INIT_ROLL,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_POS_INIT_PITCH,
                                     g_param_spec_float("initial-world-transform-pitch",
                                                        "Initial Pitch orientation",
                                                        "Pitch orientation of the camera in the world frame when the camera is started",
                                                        0.0f, 360.0f,
                                                        DEFAULT_PROP_POS_INIT_PITCH,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_POS_INIT_YAW,
                                     g_param_spec_float("initial-world-transform-yaw",
                                                        "Initial Yaw orientation",
                                                        "Yaw orientation of the camera in the world frame when the camera is started",
                                                        0.0f, 360.0f,
                                                        DEFAULT_PROP_POS_INIT_YAW,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));


    g_object_class_install_property( gobject_class, PROP_OD_ENABLE,
                                     g_param_spec_boolean("od-enabled", "Object Detection enable",
                                                          "Set to TRUE to enable Object Detection",
                                                          DEFAULT_PROP_OD_ENABLE,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_OD_IMAGE_SYNC,
                                     g_param_spec_boolean("od-image-sync", "Object detection frame sync",
                                                          "Set to TRUE to enable Object Detection frame synchronization ",
                                                          DEFAULT_PROP_OD_SYNC,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_OD_TRACKING,
                                     g_param_spec_boolean("od-enable-tracking", "Object detection tracking",
                                                          "Set to TRUE to enable tracking for the detected objects",
                                                          DEFAULT_PROP_OD_TRACKING,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    //Not yet supported
    /*g_object_class_install_property( gobject_class, PROP_OD_MASK,
                                     g_param_spec_boolean("od-mask", "OD Mask output",
                                                          "Set to TRUE to enable mask output for the detected objects",
                                                          DEFAULT_PROP_OD_MASK,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));*/


    g_object_class_install_property( gobject_class, PROP_OD_DET_MODEL,
                                     g_param_spec_enum("od-detection-model", "Object detection model",
                                                       "Object Detection Model", GST_TYPE_ZED_OD_MODEL_TYPE,
                                                       DEFAULT_PROP_OD_MODEL,
                                                       (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_OD_CONFIDENCE,
                                     g_param_spec_float("od-confidence", "Minimum Object detection confidence threshold",
                                                        "Minimum Detection Confidence", 0.0f, 100.0f, DEFAULT_PROP_OD_CONFIDENCE,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_OD_MAX_RANGE,
                                     g_param_spec_float("od-max-range", "Defines if the body fitting will be applied when using Skeleton Tracking",
                                                        "Maximum Detection Range", -1.0f, 20000.0f, DEFAULT_PROP_OD_MAX_RANGE,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_OD_BODY_FITTING,
                                     g_param_spec_boolean("od-body-fitting", "Minimum Object detection confidence threshold",
                                                          "Set to TRUE to enable body fitting for skeleton tracking",
                                                          DEFAULT_PROP_OD_BODY_FITTING,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_BRIGHTNESS,
                                     g_param_spec_int("brightness", "Camera control: brightness",
                                                      "Image brightness", 0, 8, DEFAULT_PROP_BRIGHTNESS,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_CONTRAST,
                                     g_param_spec_int("contrast", "Camera control: contrast",
                                                      "Image contrast", 0, 8, DEFAULT_PROP_CONTRAST,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_HUE,
                                     g_param_spec_int("hue", "Camera control: hue",
                                                      "Image hue", 0, 11, DEFAULT_PROP_HUE,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_SATURATION,
                                     g_param_spec_int("saturation", "Camera control: saturation",
                                                      "Image saturation", 0, 8, DEFAULT_PROP_SATURATION,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_SHARPNESS,
                                     g_param_spec_int("sharpness", "Camera control: sharpness",
                                                      "Image sharpness", 0, 8, DEFAULT_PROP_SHARPNESS,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_GAMMA,
                                     g_param_spec_int("gamma", "Camera control: gamma",
                                                      "Image gamma", 1, 9, DEFAULT_PROP_GAMMA,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_GAIN,
                                     g_param_spec_int("gain", "Camera control: gain",
                                                      "Camera gain", 0, 100, DEFAULT_PROP_GAIN,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_EXPOSURE,
                                     g_param_spec_int("exposure", "Camera control: exposure",
                                                      "Camera exposure", 0, 100, DEFAULT_PROP_EXPOSURE,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_AEC_AGC,
                                     g_param_spec_boolean("aec-agc", "Camera control: automatic gain and exposure",
                                                          "Camera automatic gain and exposure", DEFAULT_PROP_AEG_AGC,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_AEC_AGC_ROI_X,
                                     g_param_spec_int("aec-agc-roi-x", "Camera control: auto gain/exposure ROI top left 'X' coordinate",
                                                      "Auto gain/exposure ROI top left 'X' coordinate (-1 to not set ROI)",
                                                      -1, 2208, DEFAULT_PROP_AEG_AGC_ROI_X,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_AEC_AGC_ROI_Y,
                                     g_param_spec_int("aec-agc-roi-y", "Camera control: auto gain/exposure ROI top left 'Y' coordinate",
                                                      "Auto gain/exposure ROI top left 'Y' coordinate (-1 to not set ROI)",
                                                      -1, 1242, DEFAULT_PROP_AEG_AGC_ROI_Y,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_AEC_AGC_ROI_W,
                                     g_param_spec_int("aec-agc-roi-w", "Camera control: auto gain/exposure ROI width",
                                                      "Auto gain/exposure ROI width (-1 to not set ROI)",
                                                      -1, 2208, DEFAULT_PROP_AEG_AGC_ROI_W,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_AEC_AGC_ROI_H,
                                     g_param_spec_int("aec-agc-roi-h", "Camera control: auto gain/exposure ROI height",
                                                      "Auto gain/exposure ROI height (-1 to not set ROI)",
                                                      -1, 1242, DEFAULT_PROP_AEG_AGC_ROI_H,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_AEC_AGC_ROI_SIDE,
                                     g_param_spec_enum("aec-agc-roi-side", "Camera control: auto gain/exposure ROI side",
                                                       "Auto gain/exposure ROI side", GST_TYPE_ZED_SIDE,
                                                       DEFAULT_PROP_AEG_AGC_ROI_SIDE,
                                                       (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_WHITEBALANCE,
                                     g_param_spec_int("whitebalance-temperature", "Camera control: white balance temperature",
                                                      "Image white balance temperature", 2800, 6500, DEFAULT_PROP_WHITEBALANCE,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_WHITEBALANCE_AUTO,
                                     g_param_spec_boolean("whitebalance-auto", "Camera control: automatic whitebalance",
                                                          "Image automatic white balance", DEFAULT_PROP_WHITEBALANCE_AUTO,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property( gobject_class, PROP_LEDSTATUS,
                                     g_param_spec_boolean("led-status", "Camera control: led status",
                                                          "Camera LED on/off", DEFAULT_PROP_LEDSTATUS,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_zedsrc_reset (GstZedSrc * src)
{
    if(src->zed.isOpened())
    {
        src->zed.close();
    }

    src->out_framesize = 0;
    src->is_started = FALSE;

    src->last_frame_count = 0;
    src->total_dropped_frames = 0;

    if (src->caps) {
        gst_caps_unref (src->caps);
        src->caps = NULL;
    }
}

static void gst_zedsrc_init (GstZedSrc * src)
{
    /* set source as live (no preroll) */
    gst_base_src_set_live (GST_BASE_SRC (src), TRUE);

    /* override default of BYTES to operate in time mode */
    gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);

    // ----> Parameters initialization
    src->camera_resolution = DEFAULT_PROP_CAM_RES;
    src->camera_fps = DEFAULT_PROP_CAM_FPS;
    src->sdk_verbose = DEFAULT_PROP_SDK_VERBOSE;
    src->camera_image_flip = DEFAULT_PROP_CAM_FLIP;
    src->camera_id = DEFAULT_PROP_CAM_ID;
    src->camera_sn = DEFAULT_PROP_CAM_SN;
    src->svo_file = *g_string_new( DEFAULT_PROP_SVO_FILE );
    src->stream_ip = *g_string_new( DEFAULT_PROP_STREAM_IP );;
    src->stream_port = DEFAULT_PROP_STREAM_PORT;
    src->stream_type = DEFAULT_PROP_STREAM_TYPE;

    src->depth_min_dist = DEFAULT_PROP_DEPTH_MIN;
    src->depth_max_dist = DEFAULT_PROP_DEPTH_MAX;
    src->depth_mode = DEFAULT_PROP_DEPTH_MODE;
    src->camera_disable_self_calib = DEFAULT_PROP_DIS_SELF_CALIB;
    src->depth_stabilization = DEFAULT_PROP_DEPTH_STAB;
    src->coord_sys = DEFAULT_PROP_COORD_SYS;
    src->confidence_threshold = DEFAULT_PROP_CONFIDENCE_THRESH;
    src->texture_confidence_threshold = DEFAULT_PROP_TEXTURE_CONF_THRESH;
    src->measure3D_reference_frame = DEFAULT_PROP_3D_REF_FRAME;
    src->sensing_mode = DEFAULT_PROP_SENSING_MODE;

    src->pos_tracking = DEFAULT_PROP_POS_TRACKING;
    src->camera_static = DEFAULT_PROP_CAMERA_STATIC;
    src->area_file_path = *g_string_new(DEFAULT_PROP_POS_AREA_FILE_PATH);
    src->enable_area_memory = DEFAULT_PROP_POS_ENABLE_AREA_MEMORY;
    src->enable_imu_fusion = DEFAULT_PROP_POS_ENABLE_IMU_FUSION;
    src->enable_pose_smoothing = DEFAULT_PROP_POS_ENABLE_POSE_SMOOTHING;
    src->set_floor_as_origin = DEFAULT_PROP_POS_SET_FLOOR_AS_ORIGIN;
    src->init_pose_x = DEFAULT_PROP_POS_INIT_X;
    src->init_pose_y = DEFAULT_PROP_POS_INIT_Y;
    src->init_pose_z = DEFAULT_PROP_POS_INIT_Z;
    src->init_orient_roll = DEFAULT_PROP_POS_INIT_ROLL;
    src->init_orient_pitch = DEFAULT_PROP_POS_INIT_PITCH;
    src->init_orient_yaw = DEFAULT_PROP_POS_INIT_YAW;

    src->object_detection = DEFAULT_PROP_OD_ENABLE;
    src->od_image_sync = DEFAULT_PROP_OD_SYNC;
    src->od_enable_tracking = DEFAULT_PROP_OD_TRACKING;
    src->od_enable_mask_output = DEFAULT_PROP_OD_MASK;
    src->od_detection_model = DEFAULT_PROP_OD_MODEL;
    src->od_det_conf = DEFAULT_PROP_OD_CONFIDENCE;
    src->od_max_range = DEFAULT_PROP_OD_MAX_RANGE;
    src->od_body_fitting = DEFAULT_PROP_OD_BODY_FITTING;

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
    src->aec_agc_roi_side = DEFAULT_PROP_AEG_AGC_ROI_SIDE;
    src->whitebalance_temperature = DEFAULT_PROP_WHITEBALANCE;
    src->whitebalance_temperature_auto = DEFAULT_PROP_WHITEBALANCE_AUTO;
    src->led_status = DEFAULT_PROP_LEDSTATUS;
    // <---- Parameters initialization

    src->stop_requested = FALSE;
    src->caps = NULL;

    gst_zedsrc_reset (src);
}

void gst_zedsrc_set_property (GObject * object, guint property_id,
                              const GValue * value, GParamSpec * pspec)
{
    GstZedSrc *src;
    const gchar* str;

    src = GST_ZED_SRC(object);

    switch (property_id) {
    case PROP_CAM_RES:
        src->camera_resolution = g_value_get_enum(value);
        break;
    case PROP_CAM_FPS:
        src->camera_fps = g_value_get_enum(value);
        break;
    case PROP_SDK_VERBOSE:
        src->sdk_verbose = g_value_get_boolean(value);
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
        src->svo_file = *g_string_new( str );
        break;
    case PROP_STREAM_IP:
        str = g_value_get_string(value);
        src->stream_ip = *g_string_new( str );
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
        src->depth_stabilization = g_value_get_boolean(value);
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
    case PROP_SENSING_MODE:
        src->sensing_mode = g_value_get_enum(value);
        break;
    case PROP_POS_TRACKING:
        src->pos_tracking = g_value_get_boolean(value);
        break;
    case PROP_CAMERA_STATIC:
        src->camera_static = g_value_get_boolean(value);
        break;
    case PROP_POS_AREA_FILE_PATH:
        str = g_value_get_string(value);
        src->area_file_path = *g_string_new( str );
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
    case PROP_OD_IMAGE_SYNC:
        src->od_image_sync = g_value_get_boolean(value);
        break;
    case PROP_OD_TRACKING:
        src->od_enable_tracking = g_value_get_boolean(value);
        break;
        /*case PROP_OD_MASK:
        src->od_enable_mask_output = g_value_get_boolean(value);
        break;*/
    case PROP_OD_DET_MODEL:
        src->od_detection_model = g_value_get_enum(value);
        break;
    case PROP_OD_CONFIDENCE:
        src->od_det_conf = g_value_get_float(value);
        break;
    case PROP_OD_MAX_RANGE:
        src->od_max_range = g_value_get_float(value);
        break;
    case PROP_OD_BODY_FITTING:
        src->od_body_fitting = g_value_get_boolean(value);
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
    case PROP_LEDSTATUS:
        src->led_status = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

void
gst_zedsrc_get_property (GObject * object, guint property_id,
                         GValue * value, GParamSpec * pspec)
{
    GstZedSrc *src;

    g_return_if_fail (GST_IS_ZED_SRC (object));
    src = GST_ZED_SRC (object);

    switch (property_id) {
    case PROP_CAM_RES:
        g_value_set_enum( value, src->camera_resolution );
        break;
    case PROP_CAM_FPS:
        g_value_set_enum( value, src->camera_fps );
        break;
    case PROP_SDK_VERBOSE:
        g_value_set_boolean( value, src->sdk_verbose );
        break;
    case PROP_CAM_FLIP:
        g_value_set_enum( value, src->camera_image_flip );
        break;
    case PROP_CAM_ID:
        g_value_set_int( value, src->camera_id );
        break;
    case PROP_CAM_SN:
        g_value_set_int64( value, src->camera_id );
        break;
    case PROP_SVO_FILE:
        g_value_set_string( value, src->svo_file.str );
        break;
    case PROP_STREAM_IP:
        g_value_set_string( value, src->stream_ip.str );
        break;
    case PROP_STREAM_PORT:
        g_value_set_int( value, src->stream_port );
        break;
    case PROP_STREAM_TYPE:
        g_value_set_enum( value, src->stream_type );
        break;
    case PROP_DEPTH_MIN:
        g_value_set_float( value, src->depth_min_dist );
        break;
    case PROP_DEPTH_MAX:
        g_value_set_float( value, src->depth_max_dist );
        break;
    case PROP_DEPTH_MODE:
        g_value_set_enum( value, src->depth_mode );
        break;
    case PROP_COORD_SYS:
        g_value_set_enum( value, src->coord_sys );
        break;
    case PROP_DIS_SELF_CALIB:
        g_value_set_boolean( value, src->camera_disable_self_calib );
        break;
        /*case PROP_RIGHT_DEPTH_ENABLE:
        g_value_set_boolean( value, src->enable_right_side_measure);
        break;*/
    case PROP_DEPTH_STAB:
        g_value_set_boolean( value, src->depth_stabilization );
        break;
    case PROP_CONFIDENCE_THRESH:
        g_value_set_int(value, src->confidence_threshold);
        break;
    case PROP_TEXTURE_CONF_THRESH:
        g_value_set_int(value, src->texture_confidence_threshold);
        break;
    case PROP_3D_REF_FRAME:
        g_value_set_enum( value, src->measure3D_reference_frame);
        break;
    case PROP_SENSING_MODE:
        g_value_set_enum( value, src->sensing_mode);
        break;
    case PROP_POS_TRACKING:
        g_value_set_boolean( value, src->pos_tracking );
        break;
    case PROP_CAMERA_STATIC:
        g_value_set_boolean( value, src->camera_static );
        break;
    case PROP_POS_AREA_FILE_PATH:
        g_value_set_string( value, src->area_file_path.str );
        break;
    case PROP_POS_ENABLE_AREA_MEMORY:
        g_value_set_boolean( value, src->enable_area_memory );
        break;
    case PROP_POS_ENABLE_IMU_FUSION:
        g_value_set_boolean( value, src->enable_imu_fusion );
        break;
    case PROP_POS_SET_FLOOR_AS_ORIGIN:
        g_value_set_boolean( value, src->set_floor_as_origin );
        break;
    case PROP_POS_ENABLE_POSE_SMOOTHING:
        g_value_set_boolean( value, src->enable_pose_smoothing );
        break;
    case PROP_POS_INIT_X:
        g_value_set_float( value, src->init_pose_x );
        break;
    case PROP_POS_INIT_Y:
        g_value_set_float( value, src->init_pose_y );
        break;
    case PROP_POS_INIT_Z:
        g_value_set_float( value, src->init_pose_z );
        break;
    case PROP_POS_INIT_ROLL:
        g_value_set_float( value, src->init_orient_roll );
        break;
    case PROP_POS_INIT_PITCH:
        g_value_set_float( value, src->init_orient_pitch );
        break;
    case PROP_POS_INIT_YAW:
        g_value_set_float( value, src->init_orient_yaw );
        break;
    case PROP_OD_ENABLE:
        g_value_set_boolean( value, src->object_detection );
        break;
    case PROP_OD_IMAGE_SYNC:
        g_value_set_boolean( value, src->od_image_sync );
        break;
    case PROP_OD_TRACKING:
        g_value_set_boolean( value, src->od_enable_tracking );
        break;
        /*case PROP_OD_MASK:
        g_value_set_boolean( value, src->od_enable_mask_output );
        break;*/
    case PROP_OD_DET_MODEL:
        g_value_set_enum( value, src->od_detection_model );
        break;
    case PROP_OD_CONFIDENCE:
        g_value_set_float( value, src->od_det_conf );
        break;
    case PROP_OD_MAX_RANGE:
        g_value_set_float( value, src->od_max_range );
        break;
    case PROP_OD_BODY_FITTING:
        g_value_set_boolean( value, src->od_body_fitting );
        break;

    case PROP_BRIGHTNESS:
        g_value_set_int( value, src->brightness);
        break;
    case PROP_CONTRAST:
        g_value_set_int( value, src->contrast);
        break;
    case PROP_HUE:
        g_value_set_int( value, src->hue);
        break;
    case PROP_SATURATION:
        g_value_set_int( value, src->saturation);
        break;
    case PROP_SHARPNESS:
        g_value_set_int( value, src->sharpness);
        break;
    case PROP_GAMMA:
        g_value_set_int( value, src->gamma);
        break;
    case PROP_GAIN:
        g_value_set_int( value, src->gain);
        break;
    case PROP_EXPOSURE:
        g_value_set_int( value, src->exposure);
        break;
    case PROP_AEC_AGC:
        g_value_set_boolean( value, src->aec_agc);
        break;
    case PROP_AEC_AGC_ROI_X:
        g_value_set_int( value, src->aec_agc_roi_x);
        break;
    case PROP_AEC_AGC_ROI_Y:
        g_value_set_int( value, src->aec_agc_roi_y);
        break;
    case PROP_AEC_AGC_ROI_W:
        g_value_set_int( value, src->aec_agc_roi_w);
        break;
    case PROP_AEC_AGC_ROI_H:
        g_value_set_int( value, src->aec_agc_roi_h);
        break;
    case PROP_AEC_AGC_ROI_SIDE:
        g_value_set_enum( value, src->aec_agc_roi_side);
        break;
    case PROP_WHITEBALANCE:
        g_value_set_int( value, src->whitebalance_temperature);
        break;
    case PROP_WHITEBALANCE_AUTO:
        g_value_set_boolean( value, src->whitebalance_temperature_auto);
        break;
    case PROP_LEDSTATUS:
        g_value_set_boolean( value, src->led_status);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

void
gst_zedsrc_dispose (GObject * object)
{
    GstZedSrc *src;

    g_return_if_fail (GST_IS_ZED_SRC (object));
    src = GST_ZED_SRC (object);

    /* clean up as possible.  may be called multiple times */

    G_OBJECT_CLASS (gst_zedsrc_parent_class)->dispose (object);
}

void
gst_zedsrc_finalize (GObject * object)
{
    GstZedSrc *src;

    g_return_if_fail (GST_IS_ZED_SRC (object));
    src = GST_ZED_SRC (object);

    /* clean up object here */
    if (src->caps) {
        gst_caps_unref (src->caps);
        src->caps = NULL;
    }

    G_OBJECT_CLASS (gst_zedsrc_parent_class)->finalize (object);
}

static gboolean gst_zedsrc_calculate_caps(GstZedSrc* src)
{
    guint32 width, height;
    gint fps;
    GstVideoInfo vinfo;
    GstVideoFormat format = GST_VIDEO_FORMAT_BGRA;

    if(src->stream_type==GST_ZEDSRC_DEPTH_16 )
    {
        format = GST_VIDEO_FORMAT_GRAY16_LE;
    }

    sl::CameraInformation cam_info = src->zed.getCameraInformation();

    width = cam_info.camera_configuration.resolution.width;
    height = cam_info.camera_configuration.resolution.height;

    if(src->stream_type==GST_ZEDSRC_LEFT_RIGHT || src->stream_type==GST_ZEDSRC_LEFT_DEPTH)
    {
        height *= 2;
    }

    fps = static_cast<gint>(cam_info.camera_configuration.fps);

    if( format != GST_VIDEO_FORMAT_UNKNOWN ) {
        gst_video_info_init( &vinfo );
        gst_video_info_set_format( &vinfo, format, width, height );
        if (src->caps) {
            gst_caps_unref (src->caps);
        }
        src->out_framesize = (guint) GST_VIDEO_INFO_SIZE (&vinfo);
        vinfo.fps_n = fps;
        vinfo.fps_d = 1;
        src->caps = gst_video_info_to_caps (&vinfo);
    }

    gst_base_src_set_blocksize( GST_BASE_SRC(src), src->out_framesize );
    gst_base_src_set_caps ( GST_BASE_SRC(src), src->caps );
    GST_DEBUG_OBJECT( src, "Created caps %" GST_PTR_FORMAT, src->caps );

    return TRUE;
}

static gboolean gst_zedsrc_start( GstBaseSrc * bsrc )
{   
#if ZED_SDK_MAJOR_VERSION!=3 && ZED_SDK_MINOR_VERSION!=3
    GST_ELEMENT_ERROR (src, LIBRARY, FAILED,
                       ("Wrong ZED SDK version. SDK v3.3.x required "), (NULL));
#endif

    GstZedSrc *src = GST_ZED_SRC (bsrc);
    sl::ERROR_CODE ret;

    GST_DEBUG_OBJECT( src, "start" );

    // ----> Set init parameters
    sl::InitParameters init_params;

    GST_INFO("CAMERA INITIALIZATION PARAMETERS");

    init_params.camera_resolution = static_cast<sl::RESOLUTION>(src->camera_resolution);
    GST_INFO(" * Camera resolution: %s", sl::toString(init_params.camera_resolution).c_str());
    init_params.camera_fps = src->camera_fps;
    GST_INFO(" * Camera FPS: %d", init_params.camera_fps);
    init_params.sdk_verbose = src->sdk_verbose==TRUE;
    GST_INFO(" * SDK verbose: %s", (init_params.sdk_verbose?"TRUE":"FALSE"));
    init_params.camera_image_flip = src->camera_image_flip;
    GST_INFO(" * Camera flipped: %s", sl::toString(static_cast<sl::FLIP_MODE>(init_params.camera_image_flip)).c_str());

    init_params.depth_mode = static_cast<sl::DEPTH_MODE>(src->depth_mode);
    if(src->object_detection && init_params.depth_mode==sl::DEPTH_MODE::NONE)
    {
        init_params.depth_mode=sl::DEPTH_MODE::ULTRA;
        src->depth_mode = static_cast<gint>(init_params.depth_mode);

        GST_WARNING_OBJECT(src, "Object detection requires DEPTH_MODE!=NONE. Depth mode value forced to ULTRA");

        if(!src->pos_tracking)
        {
            src->pos_tracking=TRUE;
            GST_WARNING_OBJECT(src, "Object detection requires Positional Tracking to be active. Positional Tracking automatically activated");
        }
    }
    if(src->pos_tracking && init_params.depth_mode==sl::DEPTH_MODE::NONE)
    {
        init_params.depth_mode=sl::DEPTH_MODE::ULTRA;
        src->depth_mode = static_cast<gint>(init_params.depth_mode);

        GST_WARNING_OBJECT(src, "Positional tracking requires DEPTH_MODE!=NONE. Depth mode value forced to ULTRA");
    }
    if((src->stream_type==GST_ZEDSRC_LEFT_DEPTH || src->stream_type==GST_ZEDSRC_DEPTH_16)
            && init_params.depth_mode==sl::DEPTH_MODE::NONE)
    {
        init_params.depth_mode=sl::DEPTH_MODE::ULTRA;
        src->depth_mode = static_cast<gint>(init_params.depth_mode);
        GST_WARNING_OBJECT(src, "'stream-type' setting requires depth calculation. Depth mode value forced to ULTRA");
    }
    GST_INFO(" * Depth Mode: %s", sl::toString(init_params.depth_mode).c_str());
    init_params.coordinate_units = sl::UNIT::MILLIMETER; // ready for 16bit depth image
    GST_INFO(" * Coordinate units: %s", sl::toString(init_params.coordinate_units).c_str());
    init_params.coordinate_system = static_cast<sl::COORDINATE_SYSTEM>(src->coord_sys);
    GST_INFO(" * Coordinate system: %s", sl::toString(init_params.coordinate_system).c_str());
    init_params.depth_minimum_distance = src->depth_min_dist;
    GST_INFO(" * MIN depth: %g", init_params.depth_minimum_distance);
    init_params.depth_maximum_distance = src->depth_max_dist;
    GST_INFO(" * MAX depth: %g", init_params.depth_maximum_distance);
    init_params.depth_stabilization = src->depth_stabilization;
    GST_INFO(" * Depth Stabilization: %s", (init_params.depth_stabilization?"TRUE":"FALSE"));
    init_params.enable_right_side_measure = false; //src->enable_right_side_measure==TRUE;
    init_params.camera_disable_self_calib = src->camera_disable_self_calib==TRUE;
    GST_INFO(" * Disable self calibration: %s", (init_params.camera_disable_self_calib?"TRUE":"FALSE"));

    if( src->svo_file.len != 0 )
    {
        sl::String svo( static_cast<char*>(src->svo_file.str) );
        init_params.input.setFromSVOFile(svo);
        init_params.svo_real_time_mode = true;

        GST_INFO(" * Input SVO filename: %s", src->svo_file.str);
    }
    else if( src->camera_id != DEFAULT_PROP_CAM_ID )
    {
        init_params.input.setFromCameraID(src->camera_id);

        GST_INFO(" * Input Camera ID: %d", src->camera_id);
    }
    else if( src->camera_sn != DEFAULT_PROP_CAM_SN )
    {
        init_params.input.setFromSerialNumber(src->camera_sn);

        GST_INFO(" * Input Camera SN: %ld", src->camera_sn);
    }
    else if( src->stream_ip.len != 0 )
    {
        sl::String ip( static_cast<char*>(src->stream_ip.str) );
        init_params.input.setFromStream(ip,src->stream_port);

        GST_INFO(" * Input Stream: %s:%d", src->stream_ip.str,src->stream_port );
    }
    else
    {
        GST_INFO(" * Input from default device");
    }
    // <---- Set init parameters

    // ----> Open camera
    ret = src->zed.open( init_params );

    if (ret!=sl::ERROR_CODE::SUCCESS) {
        GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
                           ("Failed to open camera, '%s'", sl::toString(ret).c_str() ), (NULL));
        return FALSE;
    }
    // <---- Open camera

    // ----> Camera Controls
    GST_INFO("CAMERA CONTROLS");
    src->zed.setCameraSettings((sl::VIDEO_SETTINGS::BRIGHTNESS), (src->brightness));
    GST_INFO(" * BRIGHTNESS: %d", src->brightness);
    src->zed.setCameraSettings(sl::VIDEO_SETTINGS::CONTRAST, src->contrast );
    GST_INFO(" * CONTRAST: %d", src->contrast);
    src->zed.setCameraSettings(sl::VIDEO_SETTINGS::HUE, src->hue );
    GST_INFO(" * HUE: %d", src->hue);
    src->zed.setCameraSettings(sl::VIDEO_SETTINGS::SATURATION, src->saturation );
    GST_INFO(" * SATURATION: %d", src->saturation);
    src->zed.setCameraSettings(sl::VIDEO_SETTINGS::SHARPNESS, src->sharpness );
    GST_INFO(" * SHARPNESS: %d", src->sharpness);
    src->zed.setCameraSettings(sl::VIDEO_SETTINGS::GAMMA, src->gamma );
    GST_INFO(" * GAMMA: %d", src->gamma);
    if(src->aec_agc==FALSE) {
        src->zed.setCameraSettings(sl::VIDEO_SETTINGS::AEC_AGC, src->aec_agc );
        GST_INFO(" * AEC_AGC: %s", (src->aec_agc?"TRUE":"FALSE"));
        src->zed.setCameraSettings(sl::VIDEO_SETTINGS::EXPOSURE, src->exposure );
        GST_INFO(" * EXPOSURE: %d", src->exposure);
        src->zed.setCameraSettings(sl::VIDEO_SETTINGS::GAIN, src->gain );
        GST_INFO(" * GAIN: %d", src->gain);
    } else {
        src->zed.setCameraSettings(sl::VIDEO_SETTINGS::AEC_AGC, src->aec_agc );
        GST_INFO(" * AEC_AGC: %s", (src->aec_agc?"TRUE":"FALSE"));

        if( src->aec_agc_roi_x!=-1 &&
                src->aec_agc_roi_y!=-1 &&
                src->aec_agc_roi_w!=-1 &&
                src->aec_agc_roi_h!=-1) {
            sl::Rect roi;
            roi.x=src->aec_agc_roi_x;
            roi.y=src->aec_agc_roi_y;
            roi.width=src->aec_agc_roi_w;
            roi.height=src->aec_agc_roi_h;

            sl::SIDE side =  static_cast<sl::SIDE>(src->aec_agc_roi_side);

            GST_INFO(" * AEC_AGC_ROI: (%d,%d)-%dx%d - Side: %d",
                     src->aec_agc_roi_x, src->aec_agc_roi_y, src->aec_agc_roi_w, src->aec_agc_roi_h,
                     src->aec_agc_roi_side);

            src->zed.setCameraSettings( sl::VIDEO_SETTINGS::AEC_AGC_ROI, roi, side );
        }
    }
    if(src->whitebalance_temperature_auto==FALSE) {
        src->zed.setCameraSettings(sl::VIDEO_SETTINGS::WHITEBALANCE_AUTO, src->whitebalance_temperature_auto );
        GST_INFO(" * WHITEBALANCE_AUTO: %s", (src->whitebalance_temperature_auto?"TRUE":"FALSE"));
        src->whitebalance_temperature /=100;
        src->whitebalance_temperature *=100;
        src->zed.setCameraSettings(sl::VIDEO_SETTINGS::WHITEBALANCE_TEMPERATURE, src->whitebalance_temperature );
        GST_INFO(" * WHITEBALANCE_TEMPERATURE: %d", src->whitebalance_temperature);

    } else {
        src->zed.setCameraSettings(sl::VIDEO_SETTINGS::WHITEBALANCE_AUTO, src->whitebalance_temperature_auto );
        GST_INFO(" * WHITEBALANCE_AUTO: %s", (src->whitebalance_temperature_auto?"TRUE":"FALSE"));
    }
    src->zed.setCameraSettings(sl::VIDEO_SETTINGS::LED_STATUS, src->led_status );
    GST_INFO(" * LED_STATUS: %s", (src->led_status?"ON":"OFF"));
    // <---- Camera Controls

    // ----> Set runtime parameters
    GST_INFO("CAMERA RUNTIME PARAMETERS");
    if( src->depth_mode==static_cast<gint>(sl::DEPTH_MODE::NONE)
            && !src->pos_tracking)
    {
        src->zedRtParams.enable_depth = false;
    }
    else
    {
        src->zedRtParams.enable_depth = true;
    }
    GST_INFO(" * Depth calculation: %s", (src->zedRtParams.enable_depth?"ON":"OFF"));
    src->zedRtParams.confidence_threshold = src->confidence_threshold;
    GST_INFO(" * Depth Confidence threshold: %d", src->zedRtParams.confidence_threshold);
    src->zedRtParams.texture_confidence_threshold = src->texture_confidence_threshold;
    GST_INFO(" * Depth Texture Confidence threshold: %d", src->zedRtParams.texture_confidence_threshold );
    src->zedRtParams.measure3D_reference_frame = static_cast<sl::REFERENCE_FRAME>(src->measure3D_reference_frame);
    GST_INFO(" * 3D Reference Frame: %s",  sl::toString(src->zedRtParams.measure3D_reference_frame).c_str());
    src->zedRtParams.sensing_mode = static_cast<sl::SENSING_MODE>(src->sensing_mode);
    GST_INFO(" * Sensing Mode: %s",  sl::toString(src->zedRtParams.sensing_mode).c_str());
    // <---- Set runtime parameters

    // ----> Positional tracking
    GST_INFO("POSITIONAL TRACKING PARAMETERS");
    GST_INFO(" * Positional tracking status: %s", (src->pos_tracking?"ON":"OFF"));
    if( src->pos_tracking )
    {
        sl::PositionalTrackingParameters pos_trk_params;
        pos_trk_params.set_as_static = (src->camera_static==TRUE);
        GST_INFO(" * Camera static: %s", (pos_trk_params.set_as_static?"TRUE":"FALSE"));
        sl::String area_file_path( static_cast<char*>(src->area_file_path.str) );
        pos_trk_params.area_file_path = area_file_path;
        GST_INFO(" * Area file path: %s", pos_trk_params.area_file_path.c_str());
        pos_trk_params.enable_area_memory = (src->enable_area_memory==TRUE);
        GST_INFO(" * Area memory: %s", (pos_trk_params.enable_area_memory?"TRUE":"FALSE"));
        pos_trk_params.enable_imu_fusion = (src->enable_imu_fusion==TRUE);
        GST_INFO(" * IMU fusion: %s", (pos_trk_params.enable_imu_fusion?"TRUE":"FALSE"));
        pos_trk_params.enable_pose_smoothing = (src->enable_pose_smoothing==TRUE);
        GST_INFO(" * Pose smoothing: %s", (pos_trk_params.enable_pose_smoothing?"TRUE":"FALSE"));
        pos_trk_params.set_floor_as_origin = (src->set_floor_as_origin==TRUE);
        GST_INFO(" * Floor as origin: %s", (pos_trk_params.set_floor_as_origin?"TRUE":"FALSE"));

        sl::Translation init_pos(src->init_pose_x,src->init_pose_y, src->init_pose_z);
        sl::Rotation init_or;
        init_or.setEulerAngles( sl::float3(src->init_orient_roll,src->init_orient_pitch,src->init_orient_yaw), false);
        pos_trk_params.initial_world_transform = sl::Transform(init_or,init_pos);
        GST_INFO(" * Initial world transform: T(%g,%g,%g) OR(%g,%g,%g)",
                 src->init_pose_x,src->init_pose_y, src->init_pose_z,
                 src->init_orient_roll,src->init_orient_pitch,src->init_orient_yaw);


        ret = src->zed.enablePositionalTracking(pos_trk_params);
        if (ret!=sl::ERROR_CODE::SUCCESS) {
            GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
                               ("Failed to start positional tracking, '%s'", sl::toString(ret).c_str() ), (NULL));
            return FALSE;
        }
    }
    // <---- Positional tracking

    // ----> Object Detection
    GST_INFO("OBJECT DETECTION PARAMETERS");
    GST_INFO(" * Object Detection status: %s", (src->object_detection?"ON":"OFF"));
    if( src->object_detection )
    {
        sl::ObjectDetectionParameters od_params;
        od_params.image_sync = (src->od_image_sync==TRUE);
        GST_INFO(" * Image sync: %s", (od_params.image_sync?"TRUE":"FALSE"));
        od_params.enable_tracking = (src->od_enable_tracking==TRUE);
        GST_INFO(" * Object tracking: %s", (od_params.enable_tracking?"TRUE":"FALSE"));
        od_params.enable_mask_output = (src->od_enable_mask_output==TRUE);
        GST_INFO(" * Mask output: %s", (od_params.enable_mask_output?"TRUE":"FALSE"));
        od_params.detection_model = static_cast<sl::DETECTION_MODEL>(src->od_detection_model);
        GST_INFO(" * Detection model: %s", sl::toString(od_params.detection_model).c_str());
        od_params.max_range = src->od_max_range;
        GST_INFO(" * Max range: %g", od_params.max_range);
        od_params.enable_body_fitting = src->od_body_fitting;
        GST_INFO(" * Body fitting: %s", (od_params.enable_body_fitting?"TRUE":"FALSE"));

        ret = src->zed.enableObjectDetection( od_params );
        if (ret!=sl::ERROR_CODE::SUCCESS) {
            GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
                               ("Failed to start Object Detection, '%s'", sl::toString(ret).c_str() ), (NULL));
            return FALSE;
        }
    }
    // <---- Object Detection

    if (!gst_zedsrc_calculate_caps(src) ) {
        return FALSE;
    }

    return TRUE;
}

static gboolean gst_zedsrc_stop (GstBaseSrc * bsrc)
{
    GstZedSrc *src = GST_ZED_SRC (bsrc);

    GST_DEBUG_OBJECT (src, "stop");

    gst_zedsrc_reset( src );

    return TRUE;
}

static GstCaps* gst_zedsrc_get_caps( GstBaseSrc * bsrc, GstCaps * filter )
{
    GstZedSrc *src = GST_ZED_SRC (bsrc);
    GstCaps *caps;

    if (src->caps)
    {
        caps = gst_caps_copy (src->caps);
    }
    else
    {
        caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (src));
    }

    GST_DEBUG_OBJECT (src, "The caps before filtering are %" GST_PTR_FORMAT,
                      caps);

    if (filter && caps)
    {
        GstCaps *tmp = gst_caps_intersect( caps, filter );
        gst_caps_unref (caps);
        caps = tmp;
    }

    GST_DEBUG_OBJECT (src, "The caps after filtering are %" GST_PTR_FORMAT, caps);

    return caps;
}

static gboolean gst_zedsrc_set_caps( GstBaseSrc * bsrc, GstCaps * caps )
{
    GstZedSrc *src = GST_ZED_SRC (bsrc);
    GstVideoInfo vinfo;

    gst_caps_get_structure( caps, 0 );

    GST_DEBUG_OBJECT (src, "The caps being set are %" GST_PTR_FORMAT, caps);

    gst_video_info_from_caps (&vinfo, caps);

    if (GST_VIDEO_INFO_FORMAT (&vinfo) == GST_VIDEO_FORMAT_UNKNOWN)
    {
        goto unsupported_caps;
    }

    return TRUE;

unsupported_caps:
    GST_ERROR_OBJECT (src, "Unsupported caps: %" GST_PTR_FORMAT, caps);
    return FALSE;
}

static gboolean gst_zedsrc_unlock( GstBaseSrc * bsrc )
{
    GstZedSrc *src = GST_ZED_SRC (bsrc);

    GST_LOG_OBJECT (src, "unlock");

    src->stop_requested = TRUE;

    return TRUE;
}

static gboolean gst_zedsrc_unlock_stop( GstBaseSrc * bsrc )
{
    GstZedSrc *src = GST_ZED_SRC (bsrc);

    GST_LOG_OBJECT (src, "unlock_stop");

    src->stop_requested = FALSE;

    return TRUE;
}

static GstFlowReturn gst_zedsrc_fill( GstPushSrc * psrc, GstBuffer * buf )
{
    GstZedSrc *src = GST_ZED_SRC (psrc);
    sl::ERROR_CODE ret;
    GstMapInfo minfo;
    GstClock *clock;
    GstClockTime clock_time;

    static int temp_ugly_buf_index = 0;

    GST_LOG_OBJECT (src, "fill");

    if (!src->is_started) {
        src->acq_start_time =
                gst_clock_get_time(gst_element_get_clock (GST_ELEMENT (src)));

        src->is_started = TRUE;
    }

    // ----> ZED grab
    ret = src->zed.grab(src->zedRtParams);

    if( ret!=sl::ERROR_CODE::SUCCESS )
    {
        GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
                           ("Grabbing failed with error '%s'", sl::toString(ret).c_str()), (NULL));
        return GST_FLOW_ERROR;
    }
    // <---- ZED grab

    // ----> Clock update
    clock = gst_element_get_clock (GST_ELEMENT (src));
    clock_time = gst_clock_get_time (clock);
    gst_object_unref (clock);
    // <---- Clock update

    // Memory mapping
    if( FALSE==gst_buffer_map( buf, &minfo, GST_MAP_WRITE ) )
    {
        GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
                           ("Failed to map buffer for writing" ), (NULL));
        return GST_FLOW_ERROR;
    }

    // ZED Mats
    sl::Mat left_img;
    sl::Mat right_img;
    sl::Mat depth_data;

    // ----> Mats retrieving
    if(src->stream_type== GST_ZEDSRC_ONLY_LEFT)
    {
        ret = src->zed.retrieveImage(left_img, sl::VIEW::LEFT, sl::MEM::CPU );
    }
    else if(src->stream_type== GST_ZEDSRC_ONLY_RIGHT)
    {
        ret = src->zed.retrieveImage(left_img, sl::VIEW::RIGHT, sl::MEM::CPU );
    }
    else if(src->stream_type== GST_ZEDSRC_LEFT_RIGHT)
    {
        ret = src->zed.retrieveImage(left_img, sl::VIEW::LEFT, sl::MEM::CPU );
        ret = src->zed.retrieveImage(right_img, sl::VIEW::RIGHT, sl::MEM::CPU );
    }
    else if(src->stream_type== GST_ZEDSRC_DEPTH_16)
    {
#if(ZED_SDK_MAJOR_VERSION==3 && ZED_SDK_MINOR_VERSION<4)
        ret = src->zed.retrieveMeasure(depth_data, sl::MEASURE::DEPTH, sl::MEM::CPU );
#else
        ret = src->zed.retrieveMeasure(depth_data, sl::MEASURE::DEPTH_U16_MM, sl::MEM::CPU );
#endif
    }
    else if(src->stream_type== GST_ZEDSRC_LEFT_DEPTH)
    {
        ret = src->zed.retrieveImage(left_img, sl::VIEW::LEFT, sl::MEM::CPU );
        ret = src->zed.retrieveMeasure(depth_data, sl::MEASURE::DEPTH, sl::MEM::CPU );
    }

    if( ret!=sl::ERROR_CODE::SUCCESS )
    {
        GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
                           ("Grabbing failed with error '%s'", sl::toString(ret).c_str()), (NULL));
        return GST_FLOW_ERROR;
    }
    // <---- Mats retrieving

    // ----> Memory copy
    if(src->stream_type== GST_ZEDSRC_DEPTH_16)
    {
#if(ZED_SDK_MAJOR_VERSION==3 && ZED_SDK_MINOR_VERSION<4)
        uint16_t* gst_data = (uint16_t*)(&minfo.data[0]);
        unsigned long dataSize = minfo.size;

        sl::float1* depthDataPtr = depth_data.getPtr<sl::float1>();

        for (unsigned long i = 0; i < dataSize/2; i++) {
            *(gst_data++) = static_cast<uint16_t>(*(depthDataPtr++));
        }
#else
        memcpy(minfo.data, depth_data.getPtr<sl::ushort1>(), minfo.size);
#endif

    }
    else if(src->stream_type== GST_ZEDSRC_LEFT_RIGHT)
    {
        // Left RGB data on half top
        memcpy(minfo.data, left_img.getPtr<sl::uchar4>(), minfo.size/2);

        // Right RGB data on half bottom
        memcpy((minfo.data + minfo.size/2), right_img.getPtr<sl::uchar4>(), minfo.size/2);
    }
    else if(src->stream_type== GST_ZEDSRC_LEFT_DEPTH)
    {
        // RGB data on half top
        memcpy(minfo.data, left_img.getPtr<sl::uchar4>(), minfo.size/2);

        // Depth data on half bottom
        uint32_t* gst_data = (uint32_t*)(minfo.data + minfo.size/2);

        sl::float1* depthDataPtr = depth_data.getPtr<sl::float1>();

        for (unsigned long i = 0; i < minfo.size/8; i++) {
            *(gst_data++) = static_cast<uint32_t>(*(depthDataPtr++));
        }
    }
    else
    {
        memcpy(minfo.data, left_img.getPtr<sl::uchar4>(), minfo.size);
    }
    // <---- Memory copy

    // ----> Info metadata
    sl::CameraInformation cam_info = src->zed.getCameraInformation();
    ZedInfo info;
    info.cam_model = (gint) cam_info.camera_model;
    info.stream_type = src->stream_type;
    info.grab_single_frame_width = cam_info.camera_configuration.resolution.width;
    info.grab_single_frame_height = cam_info.camera_configuration.resolution.height;
    if(info.grab_single_frame_height==752 || info.grab_single_frame_height==1440 || info.grab_single_frame_height==2160 || info.grab_single_frame_height==2484)
    {
        info.grab_single_frame_height/=2; // Only half buffer size if the stream is composite
    }
    // <---- Info metadata

    // ----> Positional Tracking metadata
    ZedPose pose;
    if(src->pos_tracking)
    {
        sl::Pose cam_pose;
        sl::POSITIONAL_TRACKING_STATE state = src->zed.getPosition( cam_pose );

        sl::Translation pos = cam_pose.getTranslation();
        pose.pose_avail = true;
        pose.pos_tracking_state = static_cast<int>(state);
        pose.pos[0] = pos(0);
        pose.pos[1] = pos(1);
        pose.pos[2] = pos(2);

        sl::Orientation orient = cam_pose.getOrientation();
        sl::float3 euler = orient.getRotationMatrix().getEulerAngles();
        pose.orient[0] = euler[0];
        pose.orient[1] = euler[1];
        pose.orient[2] = euler[2];
    }
    else
    {
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
    ZedSensors sens;
    if( src->zed.getCameraInformation().camera_model != sl::MODEL::ZED )
    {
        sens.sens_avail = TRUE;
        sens.imu.imu_avail = TRUE;

        sl::SensorsData sens_data;
        src->zed.getSensorsData( sens_data, sl::TIME_REFERENCE::IMAGE );

        sens.imu.acc[0] = sens_data.imu.linear_acceleration.x;
        sens.imu.acc[1] = sens_data.imu.linear_acceleration.y;
        sens.imu.acc[2] = sens_data.imu.linear_acceleration.z;
        sens.imu.gyro[0] = sens_data.imu.angular_velocity.x;
        sens.imu.gyro[1] = sens_data.imu.angular_velocity.y;
        sens.imu.gyro[2] = sens_data.imu.angular_velocity.z;

        if( src->zed.getCameraInformation().camera_model != sl::MODEL::ZED_M )
        {
            sens.mag.mag_avail = TRUE;
            sens.mag.mag[0] = sens_data.magnetometer.magnetic_field_calibrated.x;
            sens.mag.mag[1] = sens_data.magnetometer.magnetic_field_calibrated.y;
            sens.mag.mag[2] = sens_data.magnetometer.magnetic_field_calibrated.z;
            sens.env.env_avail = TRUE;

            float temp;
            sens_data.temperature.get(sl::SensorsData::TemperatureData::SENSOR_LOCATION::BAROMETER, temp);
            sens.env.temp = temp;
            sens.env.press = sens_data.barometer.pressure*1e-2;

            float tempL,tempR;
            sens_data.temperature.get(sl::SensorsData::TemperatureData::SENSOR_LOCATION::ONBOARD_LEFT, tempL);
            sens_data.temperature.get(sl::SensorsData::TemperatureData::SENSOR_LOCATION::ONBOARD_RIGHT, tempR);
            sens.temp.temp_avail = TRUE;
            sens.temp.temp_cam_left = tempL;
            sens.temp.temp_cam_right = tempR;
        }
        else
        {
            sens.mag.mag_avail = FALSE;
            sens.env.env_avail = FALSE;
            sens.temp.temp_avail = FALSE;
        }
    }
    else
    {
        sens.sens_avail = FALSE;
        sens.imu.imu_avail = FALSE;
        sens.mag.mag_avail = FALSE;
        sens.env.env_avail = FALSE;
        sens.temp.temp_avail = FALSE;
    }
    // <---- Sensors metadata metadata

    // ----> Object detection metadata
    ZedObjectData obj_data[256];
    guint8 obj_count=0;
    if( src->object_detection )
    {
        GST_TRACE_OBJECT( src, "Object Detection enabled" );

        sl::ObjectDetectionRuntimeParameters rt_params;
        rt_params.detection_confidence_threshold = src->od_det_conf;

        sl::Objects det_objs;
        sl::ERROR_CODE ret = src->zed.retrieveObjects( det_objs, rt_params );

        if( ret == sl::ERROR_CODE::SUCCESS )
        {
            if( det_objs.is_new )
            {
                GST_TRACE_OBJECT( src, "OD new data" );

                obj_count = det_objs.object_list.size();

                GST_TRACE_OBJECT( src, "Number of detected objects: %d", obj_count );

                uint8_t idx = 0;
                for (auto i = det_objs.object_list.begin(); i != det_objs.object_list.end(); ++i)
                {
                    sl::ObjectData obj = *i;

                    obj_data[idx].id = obj.id;
                    GST_TRACE_OBJECT( src, " * [%d] Object id: %d", idx, obj.id );

                    obj_data[idx].label = static_cast<OBJECT_CLASS>(obj.label);
                    GST_TRACE_OBJECT( src, " * [%d] Label: %s", idx, sl::toString(obj.label).c_str() );

                    obj_data[idx].tracking_state = static_cast<OBJECT_TRACKING_STATE>(obj.tracking_state);
                    GST_TRACE_OBJECT( src, " * [%d] Tracking state: %s", idx, sl::toString(obj.tracking_state).c_str() );

                    obj_data[idx].action_state = static_cast<OBJECT_ACTION_STATE>(obj.action_state);
                    GST_TRACE_OBJECT( src, " * [%d] Action state: %s", idx, sl::toString(obj.action_state).c_str() );

                    obj_data[idx].confidence = obj.confidence;
                    GST_TRACE_OBJECT( src, " * [%d] Object confidence: %g", idx, obj.confidence );

                    memcpy( obj_data[idx].position, (void*)obj.position.ptr(), 3*sizeof(float) );
                    GST_TRACE_OBJECT( src, " * [%d] Copied position", idx );
                    memcpy( obj_data[idx].position_covariance, (void*)obj.position_covariance, 6*sizeof(float) );
                    GST_TRACE_OBJECT( src, " * [%d] Copied covariance", idx );
                    memcpy( obj_data[idx].velocity, (void*)obj.velocity.ptr(), 3*sizeof(float) );
                    GST_TRACE_OBJECT( src, " * [%d] Copied velocity", idx );

                    if(obj.bounding_box_2d.size()>0)
                    {
                        memcpy( (uint8_t*)obj_data[idx].bounding_box_2d, (uint8_t*)obj.bounding_box_2d.data(),
                                obj.bounding_box_2d.size()*2*sizeof(unsigned int) );
                        GST_TRACE_OBJECT( src, " * [%d] Copied bbox 2D - %lu", idx, 8*sizeof(unsigned int) );
                    }
                    else
                    {
                        GST_TRACE_OBJECT( src, " * [%d] bounding_box_2d empty", idx );
                    }
                    /*for( int i=0; i<4; i++ )
                    {
                        GST_TRACE_OBJECT( src, "\t* [%d] x_cp: %u, y_cp: %u", i, obj_data[idx].bounding_box_2d[i][0], obj_data[idx].bounding_box_2d[i][1] );
                        GST_TRACE_OBJECT( src, "\t* [%d] x_or: %u, y_or: %u", i, obj.bounding_box_2d[i].x, obj.bounding_box_2d[i].y );
                    }*/

                    if(obj.bounding_box.size()>0)
                    {
                        memcpy( obj_data[idx].bounding_box_3d, (void*)obj.bounding_box.data(), 24*sizeof(float) );
                        GST_TRACE_OBJECT( src, " * [%d] Copied bbox 3D - %lu", idx, 24*sizeof(float));
                    }
                    else
                    {
                        GST_TRACE_OBJECT( src, " * [%d] bounding_box empty", idx );
                    }

                    memcpy( obj_data[idx].dimensions, (void*)obj.dimensions.ptr(), 3*sizeof(float) );
                    GST_TRACE_OBJECT( src, " * [%d] Copied dimensions", idx );

                    if( src->od_detection_model==GST_ZEDSRC_OD_HUMAN_BODY_FAST ||
                            src->od_detection_model==GST_ZEDSRC_OD_HUMAN_BODY_ACCURATE )
                    {
                        obj_data[idx].skeletons_avail = TRUE;

                        if(obj.keypoint_2d.size()>0)
                        {
                            memcpy( obj_data[idx].keypoint_2d, (void*)obj.keypoint_2d.data(), 36*sizeof(float) );
                            GST_TRACE_OBJECT( src, " * [%d] Copied skeleton 2d - %lu", idx, obj.keypoint_2d.size());
                        }
                        else
                        {
                            GST_TRACE_OBJECT( src, " * [%d] keypoint_2d empty", idx );
                        }
                        if(obj.keypoint.size()>0)
                        {
                            memcpy( obj_data[idx].keypoint_3d, (void*)obj.keypoint.data(), 54*sizeof(float) );
                            GST_TRACE_OBJECT( src, " * [%d] Copied skeleton 3d - %lu", idx, obj.keypoint.size());
                        }
                        else
                        {
                            GST_TRACE_OBJECT( src, " * [%d] keypoint empty", idx );
                        }

                        if(obj.head_bounding_box_2d.size()>0)
                        {
                            memcpy( obj_data[idx].head_bounding_box_2d, (void*)obj.head_bounding_box_2d.data(), 8*sizeof(unsigned int) );
                            GST_TRACE_OBJECT( src, " * [%d] Copied head bbox 2d - %lu", idx, obj.head_bounding_box_2d.size());
                        }
                        else
                        {
                            GST_TRACE_OBJECT( src, " * [%d] head_bounding_box_2d empty", idx );
                        }
                        if(obj.head_bounding_box.size()>0)
                        {
                            memcpy( obj_data[idx].head_bounding_box_3d, (void*)obj.head_bounding_box.data(), 24*sizeof(float) );
                            GST_TRACE_OBJECT( src, " * [%d] Copied head bbox 3d - %lu", idx, obj.head_bounding_box.size());
                        }
                        else
                        {
                            GST_TRACE_OBJECT( src, " * [%d] head_bounding_box empty", idx );
                        }
                        memcpy( obj_data[idx].head_position, (void*)obj.head_position.ptr(), 3*sizeof(float) );
                        GST_TRACE_OBJECT( src, " * [%d] Copied head position", idx );
                    }
                    else
                    {
                        obj_data[idx].skeletons_avail = FALSE;
                        GST_TRACE_OBJECT( src, " * [%d] No Skeletons", idx );
                    }

                    idx++;
                }
            }
            else
            {
                obj_count=0;
            }
        }
        else
        {
            GST_DEBUG_OBJECT(src, "Object detection problem: %s", sl::toString(ret).c_str());
        }
    }
    // <---- Object detection metadata

    gst_buffer_add_zed_src_meta( buf, info, pose, sens, src->object_detection, obj_count, obj_data);

    // ----> Timestamp meta-data
    GST_BUFFER_TIMESTAMP(buf) = GST_CLOCK_DIFF (gst_element_get_base_time (GST_ELEMENT (src)),
                                                clock_time);
    GST_BUFFER_DTS(buf) = GST_BUFFER_TIMESTAMP(buf);
    GST_BUFFER_OFFSET(buf) = temp_ugly_buf_index++;
    // <---- Timestamp meta-data

    // Buffer release
    gst_buffer_unmap( buf, &minfo );
    // gst_buffer_unref(buf); // NOTE: do not uncomment to not crash

    if (src->stop_requested) {
        return GST_FLOW_FLUSHING;
    }

    return GST_FLOW_OK;
}


static gboolean plugin_init (GstPlugin * plugin)
{
    GST_DEBUG_CATEGORY_INIT( gst_zedsrc_debug, "zedsrc", 0,
                             "debug category for zedsrc element");
    gst_element_register( plugin, "zedsrc", GST_RANK_NONE,
                          gst_zedsrc_get_type());

    return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
                   GST_VERSION_MINOR,
                   zedsrc,
                   "Zed camera source",
                   plugin_init,
                   GST_PACKAGE_VERSION,
                   GST_PACKAGE_LICENSE,
                   GST_PACKAGE_NAME,
                   GST_PACKAGE_ORIGIN)
