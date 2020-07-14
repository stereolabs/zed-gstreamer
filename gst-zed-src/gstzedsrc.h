#ifndef _GST_ZED_SRC_H_
#define _GST_ZED_SRC_H_

#include <gst/base/gstpushsrc.h>

#include "sl/Camera.hpp"

G_BEGIN_DECLS

#define GST_TYPE_ZED_SRC   (gst_zedsrc_get_type())
#define GST_ZED_SRC(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ZED_SRC,GstZedSrc))
#define GST_ZED_SRC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ZED_SRC,GstZedSrcClass))
#define GST_IS_ZED_SRC(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ZED_SRC))
#define GST_IS_ZED_SRC_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ZED_SRC))

typedef struct _GstZedSrc GstZedSrc;
typedef struct _GstZedSrcClass GstZedSrcClass;

struct _GstZedSrc
{
    GstPushSrc base_zedsrc;

    // ZED camera object
    sl::Camera zed;

    gboolean is_started; // grab started flag

    // ----> Properties
    gint camera_resolution;     // Camera resolution [enum]
    gint camera_fps;            // Camera FPS [enum]
    gboolean sdk_verbose;
    gboolean camera_image_flip;
    gint camera_id;
    gint64 camera_sn;
    GString svo_file;
    GString stream_ip;
    gint stream_port;
    gint stream_type;

    gfloat depth_min_dist;
    gfloat depth_max_dist;
    gboolean camera_disable_self_calib;
    gboolean depth_stabilization;
    gboolean pos_tracking;
    gboolean camera_static;
    gint coord_sys;
    // <---- Properties


    GstClockTime acq_start_time;
    guint32 last_frame_count;
    guint32 total_dropped_frames;

    GstCaps *caps;
    guint out_framesize;

    gboolean stop_requested;
};

struct _GstZedSrcClass
{
    GstPushSrcClass base_zedsrc_class;
};

G_GNUC_INTERNAL GType gst_zedsrc_get_type (void);

G_END_DECLS

#endif
