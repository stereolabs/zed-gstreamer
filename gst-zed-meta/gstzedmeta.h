#ifndef GSTZEDMETA_H
#define GSTZEDMETA_H

#include <gst/gst.h>
#include <vector>

G_BEGIN_DECLS

typedef struct _GstZedSrcMeta GstZedSrcMeta;
typedef struct _ZedInfo ZedInfo;
typedef struct _ZedPose ZedPose;
typedef struct _ZedSensors ZedSensors;
typedef struct _ZedImu ZedImu;
typedef struct _ZedMag ZedMag;
typedef struct _ZedEnv ZedEnv;
typedef struct _ZedCamTemp ZedCamTemp;
typedef struct _ZedObjectData ZedObjectData;

struct _ZedInfo {
    gint cam_model;
    gint stream_type;
    guint grab_single_frame_width;
    guint grab_single_frame_height;
};

struct _ZedPose {
    gboolean pose_avail;
    gint pos_tracking_state;
    gfloat pos[3];
    gfloat orient[3];
};

struct _ZedImu {
    gboolean imu_avail;
    gfloat acc[3];
    gfloat gyro[3];
    gfloat temp;
};

struct _ZedMag {
    gboolean mag_avail;
    gfloat mag[3];
};

struct _ZedEnv {
    gboolean env_avail;
    gfloat press;
    gfloat temp;
};

struct _ZedCamTemp {
    gboolean temp_avail;
    gfloat temp_cam_left;
    gfloat temp_cam_right;
};

struct _ZedSensors {
    gboolean sens_avail;
    ZedImu imu;
    ZedMag mag;
    ZedEnv env;
    ZedCamTemp temp;
};

enum class OBJECT_CLASS {
    PERSON = 0, /**< For people detection */
    VEHICLE = 1, /**< For vehicles detection. It can be cars, trucks, buses, motorcycles etc */
    LAST
};

enum class OBJECT_TRACKING_STATE {
    OFF, /**< The tracking is not yet initialized, the object ID is not usable */
    OK, /**< The object is tracked */
    SEARCHING, /**< The object couldn't be detected in the image and is potentially occluded, the trajectory is estimated */
    TERMINATE, /**< This is the last searching state of the track, the track will be deleted in the next retreiveObject */
    LAST
};

enum class OBJECT_ACTION_STATE {
    IDLE = 0, /**< The object is staying static. */
    MOVING = 1, /**< The object is moving. */
    LAST
};

struct _ZedObjectData {
    gint id;

    OBJECT_CLASS label;
    OBJECT_TRACKING_STATE tracking_state;
    OBJECT_ACTION_STATE action_state;

    gfloat confidence;

    gfloat position[3];
    gfloat position_covariance[6];

    gfloat velocity[3];

    unsigned int bounding_box_2d[4][2];

    // TODO mask?

    /* 3D bounding box of the person represented as eight 3D points
          1 ------ 2
         /        /|
        0 ------ 3 |
        | Object | 6
        |        |/
        4 ------ 7
    */
    gfloat bounding_box_3d[8][3];

    gfloat dimensions[3]; // 3D object dimensions: width, height, length

    gboolean skeletons_avail;

    gfloat keypoint_2d[18][2]; // Negative coordinates -> point not valid
    gfloat keypoint_3d[18][3]; // Nan coordinates -> point not valid

    gfloat head_bounding_box_2d[4][2];
    gfloat head_bounding_box_3d[8][3];
    gfloat head_position[3];
};

struct _GstZedSrcMeta {
    GstMeta meta;

    ZedInfo info;

    ZedPose pose;
    ZedSensors sens;

    gboolean od_enabled;
    guint8 obj_count;
    ZedObjectData objects[256];
};

namespace skeleton {

enum class BODY_PARTS {
    NOSE = 0,
    NECK = 1,
    RIGHT_SHOULDER = 2,
    RIGHT_ELBOW= 3,
    RIGHT_WRIST = 4,
    LEFT_SHOULDER = 5,
    LEFT_ELBOW = 6,
    LEFT_WRIST = 7,
    RIGHT_HIP = 8,
    RIGHT_KNEE = 9,
    RIGHT_ANKLE = 10,
    LEFT_HIP = 11,
    LEFT_KNEE = 12,
    LEFT_ANKLE = 13,
    RIGHT_EYE = 14,
    LEFT_EYE = 15,
    RIGHT_EAR = 16,
    LEFT_EAR = 17,
    LAST = 18
};

inline int getIdx(BODY_PARTS part) {
    return static_cast<int>(part);
}

static const std::vector<std::pair< BODY_PARTS, BODY_PARTS>> BODY_BONES{
    {BODY_PARTS::NOSE, BODY_PARTS::NECK},
    {BODY_PARTS::NECK, BODY_PARTS::RIGHT_SHOULDER},
    {BODY_PARTS::RIGHT_SHOULDER, BODY_PARTS::RIGHT_ELBOW},
    {BODY_PARTS::RIGHT_ELBOW, BODY_PARTS::RIGHT_WRIST},
    {BODY_PARTS::NECK, BODY_PARTS::LEFT_SHOULDER},
    {BODY_PARTS::LEFT_SHOULDER, BODY_PARTS::LEFT_ELBOW},
    {BODY_PARTS::LEFT_ELBOW, BODY_PARTS::LEFT_WRIST},
    {BODY_PARTS::RIGHT_SHOULDER, BODY_PARTS::RIGHT_HIP},
    {BODY_PARTS::RIGHT_HIP, BODY_PARTS::RIGHT_KNEE},
    {BODY_PARTS::RIGHT_KNEE, BODY_PARTS::RIGHT_ANKLE},
    {BODY_PARTS::LEFT_SHOULDER, BODY_PARTS::LEFT_HIP},
    {BODY_PARTS::LEFT_HIP, BODY_PARTS::LEFT_KNEE},
    {BODY_PARTS::LEFT_KNEE, BODY_PARTS::LEFT_ANKLE},
    {BODY_PARTS::RIGHT_SHOULDER, BODY_PARTS::LEFT_SHOULDER},
    {BODY_PARTS::RIGHT_HIP, BODY_PARTS::LEFT_HIP},
    {BODY_PARTS::NOSE, BODY_PARTS::RIGHT_EYE},
    {BODY_PARTS::RIGHT_EYE, BODY_PARTS::RIGHT_EAR},
    {BODY_PARTS::NOSE, BODY_PARTS::LEFT_EYE},
    {BODY_PARTS::LEFT_EYE, BODY_PARTS::LEFT_EAR}
};
}

GST_EXPORT
GType gst_zed_src_meta_api_get_type (void);
#define GST_ZED_SRC_META_API_TYPE (gst_zed_src_meta_api_get_type())
#define GST_ZED_SRC_META_INFO  (gst_zed_src_meta_get_info())

#define gst_buffer_get_zed_src_meta(b) ((GstZedSrcMeta*)gst_buffer_get_meta((b),GST_ZED_SRC_META_API_TYPE))

/* implementation */

GST_EXPORT
const GstMetaInfo* gst_zed_src_meta_get_info (void);
#define GST_ZED_SRC_META_INFO (gst_zed_src_meta_get_info())

GST_EXPORT
GstZedSrcMeta* gst_buffer_add_zed_src_meta( GstBuffer* buffer,
                                            ZedInfo &info,
                                            ZedPose &pose,
                                            ZedSensors& sens,
                                            gboolean od_enabled,
                                            guint8 obj_count,
                                            ZedObjectData* objects);

G_END_DECLS

#endif
