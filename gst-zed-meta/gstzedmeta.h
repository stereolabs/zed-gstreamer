#ifndef GSTZEDMETA_H
#define GSTZEDMETA_H

#include <gst/gst.h>

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
};

struct _ZedPose {
    gboolean pose_avail;
    gdouble pos[3];
    gdouble orient[3];
};

struct _ZedImu {
    gboolean imu_avail;
    gdouble acc[3];
    gdouble gyro[3];
    gdouble temp;
};

struct _ZedMag {
    gboolean mag_avail;
    gdouble mag[3];
};

struct _ZedEnv {
    gboolean env_avail;
    gdouble press;
    gdouble temp;
};

struct _ZedCamTemp {
    gboolean temp_avail;
    gdouble temp_cam_left;
    gdouble temp_cam_right;
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

    gfloat bounding_box_2d[4][2];

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

    gfloat dimensions; // 3D object dimensions: width, height, length

    gfloat keypoint_2d[18][2]; // Negative coordinates -> point not valid
    gfloat keypoint_3d[18][3]; // Nan coordinates -> point not valid

    gfloat head_bounding_box_2d[4][2];
    gfloat head_bounding_box_3d[4][2];
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
GType gst_zed_src_meta_api_get_type (void);
#define GST_ZED_SRC_META_API_TYPE (gst_zed_src_meta_api_get_type())
#define gst_buffer_get_zed_src_meta(b) \
((GstZedSrcMeta*)gst_buffer_get_meta((b),GST_ZED_SRC_META_API_TYPE))

/* implementation */

GST_EXPORT
const GstMetaInfo* gst_zed_src_meta_get_info (void);
#define GST_ZED_SRC_META_INFO (gst_zed_src_meta_get_info())

GST_EXPORT
GstZedSrcMeta* gst_buffer_add_zed_src_meta( GstBuffer* buffer,
                                            ZedInfo &info,
                                            ZedPose &pose,
                                            ZedSensors& sens);

#endif
