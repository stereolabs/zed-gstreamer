#ifndef GSTZED_H_
#define GSTZED_H_

#include <gst/base/gstpushsrc.h>

#include "sl/Camera.hpp"

G_BEGIN_DECLS

#define GST_TYPE_ZED            (gst_zed_get_type())
#define GST_ZED(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ZED,GstZed))
#define GST_ZED_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ZED,GstZedClass))
#define GST_IS_ZED(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ZED))
#define GST_IS_ZED_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ZED))

typedef struct _GstZed GstZed;
typedef struct _GstZedClass GstZedClass;

typedef enum
{
    RGB_LEFT = 0,
    RGB_RIGHT = 1,
    DEPTH = 2,
    LAST=100
} DataType;

struct _GstZed
{
  GstPushSrc parent;

  gint dropped_frame_count;
  gboolean acq_started;

  /* properties */
  sl::Camera* zed;

  sl::RESOLUTION resolution;
  int frame_rate;
  DataType type;
};

struct _GstZedClass
{
  GstPushSrcClass parent_class;
};

GType gst_zed_get_type (void);

G_END_DECLS

#endif //#ifndef GSTZED_H_
