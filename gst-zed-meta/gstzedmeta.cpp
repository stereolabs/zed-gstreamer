
#include "gstzedmeta.h"
#include <gst/gstbuffer.h>



GType gst_zed_src_meta_api_get_type()
{
    static volatile GType type;
    static const gchar *tags[] = { "ZED", "source", "demux", NULL };

    if (g_once_init_enter (&type)) {
        GType _type = gst_meta_api_type_register( "GstZedSrcMetaAPI", tags );

        g_once_init_leave (&type, _type);
    }

    return type;
}

static gboolean gst_zed_src_meta_init(GstMeta * meta, gpointer params, GstBuffer* buffer)
{
    GstZedSrcMeta* emeta = (GstZedSrcMeta*) meta;

    emeta->cam_model = 0;
    emeta->stream_type = 0;

    emeta->pose.pose_avail = false;
    emeta->pose.pos[0] = 0.1;
    emeta->pose.pos[1] = 0.2;
    emeta->pose.pos[2] = 0.3;
    emeta->pose.orient[0] = 0.4;
    emeta->pose.orient[1] = 0.5;
    emeta->pose.orient[2] = 0.6;
}

static gboolean gst_zed_src_meta_transform( GstBuffer* transbuf, GstMeta * meta,
                                            GstBuffer* buffer, GQuark type, gpointer data)
{
    GstZedSrcMeta* emeta = (GstZedSrcMeta*) meta;

    /* we always copy no matter what transform */
    gst_buffer_add_zed_src_meta(transbuf, emeta->cam_model, emeta->stream_type,
                                emeta->pose,
                                emeta->sens);

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

GstZedSrcMeta* gst_buffer_add_zed_src_meta(GstBuffer* buffer,
                                           gint cam_model,
                                           gint stream_type,
                                           ZedPose& pose,
                                           ZedSensors &sens)
{
    GstZedSrcMeta* meta;

    g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

    meta = (GstZedSrcMeta *) gst_buffer_add_meta (buffer,
                                                  GST_ZED_SRC_META_INFO, NULL);

    meta->cam_model = cam_model;
    meta->stream_type = stream_type;

    memcpy( &meta->pose, &pose, sizeof(ZedPose));
    memcpy( &meta->sens, &sens, sizeof(ZedSensors));

    return meta;
}
