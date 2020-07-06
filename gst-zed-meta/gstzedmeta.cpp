
#include "gstzedmeta.h"
#include <gst/gstbuffer.h>

const GstMetaInfo* gst_zed_src_meta_get_info (void);
#define GST_ZED_SRC_META_INFO (gst_zed_src_meta_get_info())

GType gst_zed_src_meta_api_get_type()
{
    static volatile GType type;
    static const gchar *tags[] = { "ZED", "bar", NULL };

    if (g_once_init_enter (&type)) {
        GType _type = gst_meta_api_type_register ("GstZedSrcMetaAPI", tags);

        g_once_init_leave (&type, _type);
    }

    return type;
}

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

static gboolean gst_zed_src_meta_init(GstMeta * meta, gpointer params, GstBuffer* buffer)
{
    GstZedSrcMeta* emeta = (GstZedSrcMeta*) meta;

    emeta->cam_model = 0;
    emeta->stream_type = 0;

    emeta->pos_x = 0.0;
    emeta->pos_y = 0.0;
    emeta->pos_z = 0.0;
    emeta->or_roll = 0.0;
    emeta->or_pitch = 0.0;
    emeta->or_yaw = 0.0;
}

static gboolean gst_zed_src_meta_transform( GstBuffer* transbuf, GstMeta * meta,
                                            GstBuffer* buffer, GQuark type, gpointer data)
{
    GstZedSrcMeta* emeta = (GstZedSrcMeta*) meta;

    /* we always copy no matter what transform */
    gst_buffer_add_zed_src_meta(transbuf, emeta->cam_model, emeta->stream_type,
                                emeta->pos_x, emeta->pos_y, emeta->pos_z,
                                emeta->or_roll, emeta->or_pitch, emeta->or_yaw );

    return TRUE;
}

static void gst_zed_src_meta_free(GstMeta * meta, GstBuffer * buffer)
{
    GstZedSrcMeta* emeta = (GstZedSrcMeta*) meta;

    // TODO eventually free dynamic data
}

const GstMetaInfo* gst_zed_src_meta_get_info (void)
{
    static const GstMetaInfo *meta_info = NULL;

    if (g_once_init_enter (&meta_info)) {
        const GstMetaInfo *mi = gst_meta_register(GST_ZED_SRC_META_API_TYPE,
                                                  "GstZedSrcMeta",
                                                  sizeof(GstZedSrcMeta),
                                                  gst_zed_src_meta_init,
                                                  gst_zed_src_meta_free,
                                                  gst_zed_src_meta_transform);
        g_once_init_leave (&meta_info, mi);

    }

    return meta_info;
}

GstZedSrcMeta* gst_buffer_add_zed_src_meta( GstBuffer* buffer,
                                                           gint cam_model,
                                                           gint stream_type,
                                                           gdouble pos_x,
                                                           gdouble pos_y,
                                                           gdouble pos_z,
                                                           gdouble or_roll,
                                                           gdouble or_pitch,
                                                           gdouble or_yaw )
{
    GstZedSrcMeta* meta;

    g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

    meta = (GstZedSrcMeta *) gst_buffer_add_meta (buffer,
    GST_ZED_SRC_META_INFO, NULL);

    meta->cam_model = cam_model;
    meta->stream_type = stream_type;

    meta->pos_x = pos_x;
    meta->pos_y = pos_y;
    meta->pos_z = pos_z;

    meta->or_roll = or_roll;
    meta->or_pitch = or_pitch;
    meta->or_yaw = or_yaw;

    return meta;
}
