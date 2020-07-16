#ifndef _GST_ZED_OD_DISPLAY_SINK_H
#define _GST_ZED_OD_DISPLAY_SINK_H

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

#include <unistd.h>
#include <string>
#include <string.h>
#include <fstream>

G_BEGIN_DECLS

#define GST_TYPE_ZED_DATA_CSV_SINK  (gst_zeddatacsvsink_get_type())
#define GST_DATA_CSV_SINK(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ZED_DATA_CSV_SINK,GstZedDataCsvSink))
#define GST_DATA_CSV_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ZED_DATA_CSV_SINK,GstZedDataCsvSinkClass))
#define GST_IS_DATA_CSV_SINK(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ZED_DATA_CSV_SINK))
#define GST_IS_DATA_CSV_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ZED_DATA_CSV_SINK))
#define GST_DATA_CSV_SINK_CAST(obj) ((GstZedDataCsvSink *)(obj))

typedef struct _GstZedDataCsvSink GstZedDataCsvSink;
typedef struct _GstZedDataCsvSinkClass GstZedDataCsvSinkClass;

struct _GstZedDataCsvSink
{
    GstBaseSink parent;

    std::ofstream* out_file_ptr;

    // Properties
    GString filename;
    gboolean append;
};

struct _GstZedDataCsvSinkClass {
  GstBaseSinkClass parent_class;
};

G_GNUC_INTERNAL GType gst_zeddatacsvsink_get_type (void);

G_END_DECLS

#endif // #ifndef _GST_ZED_OD_DISPLAY_SINK_H
