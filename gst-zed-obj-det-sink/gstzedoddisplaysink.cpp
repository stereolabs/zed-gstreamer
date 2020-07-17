#include "gstzedoddisplaysink.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/gstbuffer.h>
#include <gst/gstcaps.h>
#include <gst/gstformat.h>

#include "gst-zed-meta/gstzedmeta.h"

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
                                                                    GST_PAD_SINK,
                                                                    GST_PAD_ALWAYS,
                                                                    GST_STATIC_CAPS( ("video/x-raw, " // Double stream
                                                                                      "format = (string) { BGRA }, "
                                                                                      "width = (int) { 672, 1280, 1920, 2208 } , "
                                                                                      "height =  (int) { 752, 1440, 2160, 2484 } , "
                                                                                      "framerate =  (fraction) { 15, 30, 60, 100 }") ) );

GST_DEBUG_CATEGORY_STATIC (gst_zedoddisplaysink_debug);
#define GST_CAT_DEFAULT gst_zedoddisplaysink_debug

#define DEFAULT_PROP_DISPLAY_3D     TRUE

enum
{
    PROP_0,
    PROP_DISPLAY3D,
    PROP_LAST
};

static void gst_zedoddisplaysink_dispose(GObject * object);

static void gst_zedoddisplaysink_set_property (GObject * object, guint prop_id,
                                               const GValue * value, GParamSpec * pspec);
static void gst_zedoddisplaysink_get_property (GObject * object, guint prop_id,
                                               GValue * value, GParamSpec * pspec);

static gboolean gst_zedoddisplaysink_start (GstBaseSink * sink);
static gboolean gst_zedoddisplaysink_stop (GstBaseSink * sink);

static gboolean gst_zedoddisplaysink_event (GstBaseSink * sink, GstEvent * event);
static GstFlowReturn gst_zedoddisplaysink_render (GstBaseSink * sink, GstBuffer * buffer);

G_DEFINE_TYPE( GstZedOdDisplaySink, gst_zedoddisplaysink, GST_TYPE_BASE_SINK );

static void gst_zedoddisplaysink_class_init (GstZedOdDisplaySinkClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
    GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);

    gobject_class->dispose = gst_zedoddisplaysink_dispose;

    gobject_class->set_property = gst_zedoddisplaysink_set_property;
    gobject_class->get_property = gst_zedoddisplaysink_get_property;

    g_object_class_install_property( gobject_class, PROP_DISPLAY3D,
                                     g_param_spec_boolean("display-3d", "Display results in 3D  ",
                                                          "Creates a 3D OpenGL View to display bounding boxes and skeletons",
                                                          DEFAULT_PROP_DISPLAY_3D,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata (gstelement_class,
                                           "ZED Object Detection Display Sink",
                                           "Sink/Video", "Display results of ZED Object Detection module",
                                           "Stereolabs <support@stereolabs.com>");
    gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);

    gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_zedoddisplaysink_start);
    gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_zedoddisplaysink_stop);
    gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_zedoddisplaysink_render);
    gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_zedoddisplaysink_event);
}

static void gst_zedoddisplaysink_init(GstZedOdDisplaySink * displaysink)
{
    GST_TRACE_OBJECT( displaysink, "Init" );

    displaysink->display3d = DEFAULT_PROP_DISPLAY_3D;
    displaysink->ocv_wnd_name = "Detections";

    gst_base_sink_set_sync(GST_BASE_SINK(displaysink), FALSE);
}

static void gst_zedoddisplaysink_dispose (GObject * object)
{
    GstZedOdDisplaySink* sink = GST_OD_DISPLAY_SINK(object);

    cv::destroyAllWindows();

    GST_TRACE_OBJECT( sink, "Dispose" );

    G_OBJECT_CLASS(gst_zedoddisplaysink_parent_class)->dispose(object);
}

void gst_zedoddisplaysink_set_property (GObject * object, guint prop_id,
                                        const GValue * value, GParamSpec * pspec)
{
    GstZedOdDisplaySink* sink = GST_OD_DISPLAY_SINK(object);

    GST_TRACE_OBJECT( sink, "Set property" );

    switch (prop_id) {
    case PROP_DISPLAY3D:
        sink->display3d = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

void gst_zedoddisplaysink_get_property (GObject * object, guint prop_id,
                                        GValue * value, GParamSpec * pspec)
{
    GstZedOdDisplaySink* sink = GST_OD_DISPLAY_SINK(object);

    GST_TRACE_OBJECT( sink, "Get property" );

    switch (prop_id) {
    case PROP_DISPLAY3D:
        g_value_set_boolean (value, sink->display3d);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

gboolean gst_zedoddisplaysink_start(GstBaseSink* sink)
{
    GstZedOdDisplaySink* displaysink = GST_OD_DISPLAY_SINK(sink);

    GST_TRACE_OBJECT( displaysink, "Start" );

    cv::namedWindow(displaysink->ocv_wnd_name, cv::WINDOW_AUTOSIZE);

    return TRUE;
}

gboolean gst_zedoddisplaysink_stop (GstBaseSink * sink)
{
    GstZedOdDisplaySink* displaysink = GST_OD_DISPLAY_SINK(sink);

    GST_TRACE_OBJECT( displaysink, "Stop" );

    return TRUE;
}

static gboolean parse_sink_caps( GstZedOdDisplaySink* sink, GstCaps* sink_caps )
{
    GstVideoInfo vinfo_in;

    GST_DEBUG_OBJECT( sink, "Sink caps %" GST_PTR_FORMAT, sink_caps);

    gst_video_info_from_caps(&vinfo_in, sink_caps);
    sink->img_left_w = vinfo_in.width;
    sink->img_left_h = vinfo_in.height/2; // The pad gets only composite stream inputs

    return TRUE;
}

gboolean gst_zedoddisplaysink_event (GstBaseSink * sink, GstEvent * event)
{
    GstEventType type;
    gboolean ret;
    GstZedOdDisplaySink* displaysink = GST_OD_DISPLAY_SINK(sink);

    type = GST_EVENT_TYPE (event);

    GST_TRACE_OBJECT( displaysink, "Event " );

    switch (type) {
    case GST_EVENT_EOS:
        break;
    case GST_EVENT_CAPS:
    {
        GST_DEBUG_OBJECT( displaysink, "Event CAPS" );
        GstCaps* caps;

        gst_event_parse_caps(event, &caps);

        ret = parse_sink_caps( displaysink, caps );

        break;
    }
    default:
        break;
    }

    return GST_BASE_SINK_CLASS(gst_zedoddisplaysink_parent_class)->event (sink, event);
}

void draw_objects( cv::Mat& image, guint8 count, ZedObjectData* objs )
{
    for( int i=0; i<count; i++ )
    {
        cv::Scalar color = cv::Scalar(i*232+232, i*176+176, i*59+59);

        GST_TRACE( "Object: %d", i );
        GST_TRACE( " * Id: %d", (int)objs[i].label );
        GST_TRACE( " * Pos: %g,%g,%g", objs[i].position[0],objs[i].position[1],objs[i].position[2] );

        // ----> Bounding box
        cv::Point tl;
        tl.x = objs[i].bounding_box_2d[0][0];
        tl.y = objs[i].bounding_box_2d[0][1];
        //GST_TRACE( "Rect tl: %d,%d", tl.x,tl.y );
        cv::Point br;
        br.x = objs[i].bounding_box_2d[2][0];
        br.y = objs[i].bounding_box_2d[2][1];
        //GST_TRACE( "Rect br: %d,%d", br.x,br.y );
        cv::rectangle( image, tl, br, color, 2 );
        // <---- Bounding box
    }
}

GstFlowReturn gst_zedoddisplaysink_render( GstBaseSink * sink, GstBuffer* buf )
{
    GstZedOdDisplaySink* displaysink = GST_OD_DISPLAY_SINK(sink);

    GstMapInfo map_in;

    GST_TRACE_OBJECT( displaysink, "Render" );

    if(gst_buffer_map(buf, &map_in, GST_MAP_READ))
    {
        // Get left image (upper half memory buffer)
        cv::Mat ocv_left = cv::Mat( displaysink->img_left_h, displaysink->img_left_w, CV_8UC4, map_in.data ).clone();

        // Metadata
        GstZedSrcMeta* meta = (GstZedSrcMeta*)gst_buffer_get_meta( buf, GST_ZED_SRC_META_API_TYPE );

        GST_TRACE_OBJECT( displaysink, "Cam. Model: %d",meta->info.cam_model );
        GST_TRACE_OBJECT( displaysink, "Stream type: %d",meta->info.stream_type );

        if(meta->od_enabled)
        {
            GST_TRACE_OBJECT( displaysink, "Detected %d objects",meta->obj_count );
            // Draw 2D detections
            draw_objects( ocv_left, meta->obj_count, meta->objects );
        }

        // rendering
        cv::imshow( displaysink->ocv_wnd_name, ocv_left );

        // Release incoming buffer
        gst_buffer_unmap( buf, &map_in );
    }

    return GST_FLOW_OK;
}

static gboolean plugin_init (GstPlugin * plugin)
{
    GST_DEBUG_CATEGORY_INIT( gst_zedoddisplaysink_debug, "zedoddisplaysink", 0,
                             "debug category for zedoddisplaysink element");
    gst_element_register( plugin, "zedoddisplaysink", GST_RANK_NONE,
                          gst_zedoddisplaysink_get_type());

    return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
                   GST_VERSION_MINOR,
                   zedoddisplaysink,
                   "ZED Object Detection Display sink",
                   plugin_init,
                   GST_PACKAGE_VERSION,
                   GST_PACKAGE_LICENSE,
                   GST_PACKAGE_NAME,
                   GST_PACKAGE_ORIGIN)
