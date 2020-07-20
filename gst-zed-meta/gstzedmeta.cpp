
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

    emeta->info.cam_model = 0;
    emeta->info.stream_type = 0;
    emeta->info.grab_frame_width = 0;
    emeta->info.grab_frame_height = 0;

    emeta->pose.pose_avail = FALSE;
    emeta->pose.pos[0] = 0.0;
    emeta->pose.pos[1] = 0.0;
    emeta->pose.pos[2] = 0.0;
    emeta->pose.orient[0] = 0.0;
    emeta->pose.orient[1] = 0.0;
    emeta->pose.orient[2] = 0.0;
}

static gboolean gst_zed_src_meta_transform( GstBuffer* transbuf, GstMeta * meta,
                                            GstBuffer* buffer, GQuark type, gpointer data)
{
    GST_TRACE("Transform");

    GstZedSrcMeta* emeta = (GstZedSrcMeta*) meta;

    /* we always copy no matter what transform */
    gst_buffer_add_zed_src_meta( transbuf,
                                 emeta->info,
                                 emeta->pose,
                                 emeta->sens,
                                 emeta->od_enabled,
                                 emeta->obj_count,
                                 emeta->objects );

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
        g_once_init_leave( &meta_info, mi );

    }

    return meta_info;
}

GstZedSrcMeta* gst_buffer_add_zed_src_meta( GstBuffer* buffer,
                                            ZedInfo &info,
                                            ZedPose &pose,
                                            ZedSensors& sens,
                                            gboolean od_enabled,
                                            guint8 obj_count,
                                            ZedObjectData* objects)
{
    GstZedSrcMeta* meta;

    g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

    meta = (GstZedSrcMeta *) gst_buffer_add_meta( buffer,
                                                  GST_ZED_SRC_META_INFO, NULL);

    memcpy( &meta->info, &info, sizeof(ZedInfo));
    memcpy( &meta->pose, &pose, sizeof(ZedPose));
    memcpy( &meta->sens, &sens, sizeof(ZedSensors));

    meta->od_enabled = od_enabled;
    meta->obj_count = obj_count;

    memcpy( &meta->objects, objects, obj_count*sizeof(ZedObjectData));

    return meta;
}
