
#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>
#include "gstzedsrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_zed_debug);
#define GST_CAT_DEFAULT gst_zed_debug

/* prototypes */
static void gst_zed_set_property (GObject * object, guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_zed_get_property (GObject * object, guint property_id, GValue * value, GParamSpec * pspec);
static void gst_zed_dispose (GObject * object);

static gboolean gst_zed_start (GstBaseSrc * src);
static gboolean gst_zed_stop (GstBaseSrc * src);
static GstCaps *gst_zed_get_caps (GstBaseSrc * bsrc, GstCaps * filter);
static gboolean gst_zed_set_caps (GstBaseSrc * bsrc, GstCaps * caps);

static GstFlowReturn gst_zed_fill (GstPushSrc * src, GstBuffer * buf);

/* class initialization */
G_DEFINE_TYPE (GstZed, gst_zed, GST_TYPE_PUSH_SRC);

static GstStaticPadTemplate gst_zed_src_template =
        GST_STATIC_PAD_TEMPLATE ("src",
                                 GST_PAD_SRC,
                                 GST_PAD_ALWAYS,
                                 GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
                                                  ("{ GRAY16_LE, BGRx }"))
                                 );

static void
gst_zed_class_init(GstZedClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
    GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
    GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);

    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "zedsrc", 0,
                             "Zed source");

    gobject_class->set_property = gst_zed_set_property;
    gobject_class->get_property = gst_zed_get_property;
    gobject_class->dispose = gst_zed_dispose;

    /* Install GObject properties */

    // TODO Add camera parameters

    gst_element_class_add_pad_template (gstelement_class,
                                        gst_static_pad_template_get (&gst_zed_src_template));

    gst_element_class_set_static_metadata (gstelement_class,
                                           "Stereolabs Zed Video Source", "Source/Video",
                                           "Stereolabs Zed video source",
                                           "Stereolabs <support@stereolabs.com>");

    gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_zed_start);
    gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_zed_stop);
    gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_zed_get_caps);
    gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_zed_set_caps);

    gstpushsrc_class->fill = GST_DEBUG_FUNCPTR (gst_zed_fill);
}

static void
gst_zed_init (GstZed* zed)
{
    /* set source as live (no preroll) */
    gst_base_src_set_live (GST_BASE_SRC (zed), TRUE);

    /* override default of BYTES to operate in time mode */
    gst_base_src_set_format (GST_BASE_SRC (zed), GST_FORMAT_TIME);

    /* initialize member variables */
    zed->zed = nullptr;
    zed->resolution = sl::RESOLUTION::HD720;
    zed->frame_rate = 30;
    zed->type = RGB_LEFT;
}

void
gst_zed_set_property (GObject * object, guint property_id,
                      const GValue * value, GParamSpec * pspec)
{
    GstZed* zed;

    g_return_if_fail (GST_IS_ZED (object));
    zed = GST_ZED (object);

    switch (property_id) {

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

void
gst_zed_get_property (GObject * object, guint property_id,
                      GValue * value, GParamSpec * pspec)
{
    GstZed* zed;

    g_return_if_fail (GST_IS_ZED (object));
    zed = GST_ZED (object);

    switch (property_id) {

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

void gst_zed_dispose (GObject * object)
{
    GstZed* zed;

    g_return_if_fail (GST_IS_ZED (object));
    zed = GST_ZED (object);


    /* clean up as possible.  may be called multiple times */

    /* Close the MultiCam driver */

    G_OBJECT_CLASS (gst_zed_parent_class)->dispose (object);
}

static gboolean gst_zed_start(GstBaseSrc * bsrc)
{
    GstZed *zed_src = GST_ZED(bsrc);

    GST_DEBUG_OBJECT (zed_src, "start");

    if(!zed_src->zed)
    {
        sl::InitParameters params;
        params.camera_resolution = zed_src->resolution;
        params.camera_fps = zed_src->frame_rate;
        params.coordinate_units = sl::UNIT::MILLIMETER;

        zed_src->zed = new sl::Camera();
        if( sl::ERROR_CODE::SUCCESS != zed_src->zed->open( params ) )
        {
            GST_DEBUG_OBJECT (zed_src, "Could not open ZED");
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
gst_zed_stop (GstBaseSrc * src)
{
    GstZed *zed = GST_ZED(src);

    GST_DEBUG_OBJECT (zed, "stop");

    return TRUE;
}

static GstCaps *
gst_zed_get_camera_caps(GstZed * src)
{
    GstZed *zed_src = GST_ZED(src);

    GstVideoFormat videoFormat;
    GstCaps *caps;
    GstVideoInfo vinfo;
    gint32 width, height;

    sl::CameraInformation info = zed_src->zed->getCameraInformation();

    if(zed_src->type==DEPTH)
    {
        videoFormat = GST_VIDEO_FORMAT_GRAY16_LE;
    }
    else
    {
        videoFormat = GST_VIDEO_FORMAT_BGRA;
    }

    width = info.camera_configuration.resolution.width;
    height = info.camera_configuration.resolution.height;

    gst_video_info_init (&vinfo);
    gst_video_info_set_format (&vinfo, videoFormat, width, height);
    vinfo.fps_n = (gint)info.camera_configuration.fps;
    vinfo.fps_d = 1;
    caps = gst_video_info_to_caps (&vinfo);

    if (caps == NULL) {
        GST_ELEMENT_ERROR (src, STREAM, TOO_LAZY,
                           (("Failed to generate caps from video format.")), (NULL));
        return NULL;
    }

    return caps;
}

static GstCaps *
gst_zed_get_caps(GstBaseSrc * bsrc, GstCaps * filter)
{
    GstZed *zed = GST_ZED(bsrc);
    GstCaps *caps;

    if(zed->zed == nullptr)
      caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (zed));
    else
      caps = gst_zed_get_camera_caps (zed);

    if (filter && caps) {
        GstCaps *tmp = gst_caps_intersect (caps, filter);
        gst_caps_unref (caps);
        caps = tmp;
    }

    return caps;
}

static gboolean
gst_zed_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
    GstVideoInfo vinfo;

    GST_DEBUG_OBJECT (bsrc, "set_caps with caps=%" GST_PTR_FORMAT, caps);

    gst_video_info_from_caps (&vinfo, caps);

    /* TODO: check stride alignment */
    gst_base_src_set_blocksize (bsrc, GST_VIDEO_INFO_SIZE (&vinfo));

    return TRUE;
}

GstFlowReturn
gst_zed_fill (GstPushSrc * src, GstBuffer * buf)
{
    GstZed *zed = GST_ZED(src);


    /* Start acquisition */

    /* Wait for next surface (frame) */

    /* Get pointer to image data and other info */

    /* Copy image to buffer from surface */

    /* TODO: set buffer timestamp based on MC_TimeStamp_us */


    /* Done processing surface, release control */

    return GST_FLOW_OK;
}
