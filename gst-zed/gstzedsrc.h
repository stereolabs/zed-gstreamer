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

    /*AP_HANDLE apbase;*/
    sl::Camera zed;
    gboolean is_started;

    /* properties */
    gint camera_index;
    //gchar *config_file;
    //gchar *config_preset;
    //gchar *xsdat_file;

    GstClockTime acq_start_time;
    guint32 last_frame_count;
    guint32 total_dropped_frames;

    GstCaps *caps;
    //gint raw_framesize;
    guint out_framesize;
    //gboolean convert_to_rgb;
    guint8 *buffer;

    gboolean stop_requested;
};

struct _GstZedSrcClass
{
    GstPushSrcClass base_zedsrc_class;
};

GType gst_zedsrc_get_type (void);

G_END_DECLS

#endif
