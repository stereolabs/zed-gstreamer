#ifndef GSTZEDMETA_H
#define GSTZEDMETA_H

#include <gst/gst.h>

typedef struct _GstZedSrcMeta GstZedSrcMeta;

struct _GstZedSrcMeta {
    GstMeta meta;

    gint cam_model;
    gint stream_type;

    gdouble pos_x;
    gdouble pos_y;
    gdouble pos_z;
    gdouble or_roll;
    gdouble or_pitch;
    gdouble or_yaw;

};

GType gst_zed_src_meta_api_get_type (void);
#define GST_ZED_SRC_META_API_TYPE (gst_zed_src_meta_api_get_type())
#define gst_buffer_get_zed_src_meta(b) \
((GstZedSrcMeta*)gst_buffer_get_meta((b),GST_ZED_SRC_META_API_TYPE))

/* implementation */

const GstMetaInfo* gst_zed_src_meta_get_info (void);
#define GST_ZED_SRC_META_INFO (gst_zed_src_meta_get_info())

GstZedSrcMeta* gst_buffer_add_zed_src_meta( GstBuffer* buffer,
                                            gint cam_model,
                                            gint stream_type,
                                            gdouble pos_x,
                                            gdouble pos_y,
                                            gdouble pos_z,
                                            gdouble or_roll,
                                            gdouble or_pitch,
                                            gdouble or_yaw );

#endif
