#ifndef GSTZEDMETA_H
#define GSTZEDMETA_H

#include <gst/gst.h>

typedef struct _GstZedSrcMeta GstZedSrcMeta;
typedef struct _ZedPose ZedPose;
typedef struct _ZedSensors ZedSensors;
typedef struct _ZedImu ZedImu;
typedef struct _ZedMag ZedMag;
typedef struct _ZedEnv ZedEnv;
typedef struct _ZedCamTemp ZedCamTemp;

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

struct _GstZedSrcMeta {
    GstMeta meta;

    gint cam_model;
    gint stream_type;

    ZedPose pose;
    ZedSensors sens;
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
GstZedSrcMeta* gst_buffer_add_zed_src_meta(GstBuffer* buffer,
                                            gint cam_model,
                                            gint stream_type,
                                            ZedPose &pose,
                                            ZedSensors& sens);

#endif
