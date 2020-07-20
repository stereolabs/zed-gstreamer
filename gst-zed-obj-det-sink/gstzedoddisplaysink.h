#ifndef _GST_ZED_OD_DISPLAY_SINK_H
#define _GST_ZED_OD_DISPLAY_SINK_H

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

#include <unistd.h>
#include <string>
#include <string.h>
#include <fstream>

#include <opencv2/opencv.hpp>

#include <atomic>

G_BEGIN_DECLS

#define GST_TYPE_ZED_OD_DISPLAY_SINK  (gst_zedoddisplaysink_get_type())
#define GST_OD_DISPLAY_SINK(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ZED_OD_DISPLAY_SINK,GstZedOdDisplaySink))
#define GST_OD_DISPLAY_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ZED_OD_DISPLAY_SINK,GstZedOdDisplaySinkClass))
#define GST_IS_OD_DISPLAY_SINK(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ZED_OD_DISPLAY_SINK))
#define GST_IS_OD_DISPLAY_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ZED_OD_DISPLAY_SINK))
#define GST_OD_DISPLAY_SINK_CAST(obj) ((GstZedOdDisplaySink *)(obj))

typedef struct _GstZedOdDisplaySink GstZedOdDisplaySink;
typedef struct _GstZedOdDisplaySinkClass GstZedOdDisplaySinkClass;

struct _GstZedOdDisplaySink
{
    GstBaseSink parent;

    GstPad* sinkpad;

    guint img_left_w;
    guint img_left_h;

    std::string ocv_wnd_name;
    std::atomic<cv::Mat*> atomic_frame;

    // Properties
    gboolean display3d;
};

struct _GstZedOdDisplaySinkClass {
  GstBaseSinkClass parent_class;
};

G_GNUC_INTERNAL GType gst_zedoddisplaySink_get_type (void);

G_END_DECLS

#endif // #ifndef _GST_ZED_OD_DISPLAY_SINK_H
