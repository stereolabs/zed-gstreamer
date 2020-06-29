#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>

#include "gstzedsrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_zedsrc_debug);
#define GST_CAT_DEFAULT gst_zedsrc_debug

/* prototypes */
static void gst_zedsrc_set_property (GObject * object,
                                     guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_zedsrc_get_property (GObject * object,
                                     guint property_id, GValue * value, GParamSpec * pspec);
static void gst_zedsrc_dispose (GObject * object);
static void gst_zedsrc_finalize (GObject * object);

static gboolean gst_zedsrc_start (GstBaseSrc * src);
static gboolean gst_zedsrc_stop (GstBaseSrc * src);
static GstCaps *gst_zedsrc_get_caps (GstBaseSrc * src, GstCaps * filter);
static gboolean gst_zedsrc_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_zedsrc_unlock (GstBaseSrc * src);
static gboolean gst_zedsrc_unlock_stop (GstBaseSrc * src);

static GstFlowReturn gst_zedsrc_fill (GstPushSrc * src, GstBuffer * buf);

enum
{
    PROP_0,
    PROP_CAM_RES,
    PROP_CAM_FPS,
    PROP_SDK_VERBOSE,
    PROP_CAM_FLIP,
    PROP_CAM_ID,
    PROP_CAM_SN,
    PROP_SVO_FILE,
    PROP_STREAM_IP,
    PROP_STREAM_PORT,
    N_PROPERTIES
};

typedef enum {
    GST_ZEDSRC_100FPS = 100,
    GST_ZEDSRC_60FPS = 60,
    GST_ZEDSRC_30FPS = 30,
    GST_ZEDSRC_15FPS = 15
} GstZedSrcFPS;

#define DEFAULT_PROP_CAM_RES        static_cast<gint>(sl::RESOLUTION::HD1080)
#define DEFAULT_PROP_CAM_FPS        GST_ZEDSRC_30FPS
#define DEFAULT_PROP_SDK_VERBOSE    FALSE
#define DEFAULT_PROP_CAM_FLIP       FALSE
#define DEFAULT_PROP_CAM_ID         -1
#define DEFAULT_PROP_CAM_SN         -1
#define DEFAULT_PROP_SVO_FILE       ""
#define DEFAULT_PROP_STREAM_IP      ""
#define DEFAULT_PROP_STREAM_PORT    30000

#define GST_TYPE_ZED_RESOL (gst_zedtsrc_resol_get_type ())
static GType
gst_zedtsrc_resol_get_type (void)
{
    static GType zedsrc_resol_type = 0;

    if (!zedsrc_resol_type) {
        static GEnumValue pattern_types[] = {
            { static_cast<gint>(sl::RESOLUTION::VGA),    "672x376",     "VGA" },
            { static_cast<gint>(sl::RESOLUTION::HD720),  "1280x720",    "HD720"  },
            { static_cast<gint>(sl::RESOLUTION::HD1080), "1920x1080",   "HD1080" },
            { static_cast<gint>(sl::RESOLUTION::HD2K),   "2208x1242",   "HD2K" },
            { 0, NULL, NULL },
        };

        zedsrc_resol_type =
                g_enum_register_static ("GstZedsrcResolution",
                                        pattern_types);
    }

    return zedsrc_resol_type;
}

#define GST_TYPE_ZED_FPS (gst_zedtsrc_fps_get_type ())
static GType
gst_zedtsrc_fps_get_type (void)
{
    static GType zedsrc_fps_type = 0;

    if (!zedsrc_fps_type) {
        static GEnumValue pattern_types[] = {
            { GST_ZEDSRC_100FPS,    "only VGA resolution",                    "100 FPS" },
            { GST_ZEDSRC_60FPS,     "only VGA and HD720 resolutions",         "60  FPS" },
            { GST_ZEDSRC_30FPS,     "only VGA, HD720 and HD1080 resolutions", "30  FPS" },
            { GST_ZEDSRC_15FPS,     "all resolutions",                        "15  FPS" },
            { 0, NULL, NULL },
        };

        zedsrc_fps_type =
                g_enum_register_static ("GstZedSrcFPS",
                                        pattern_types);
    }

    return zedsrc_fps_type;
}


/* pad templates */

static GstStaticPadTemplate gst_zedsrc_src_template =
        GST_STATIC_PAD_TEMPLATE ("src",
                                 GST_PAD_SRC,
                                 GST_PAD_ALWAYS,
                                 GST_STATIC_CAPS( "video/x-raw, "
                                                  "format = (string) { BGRA }, "
                                                  "width =  (int) [ 1, 2208 ] , "
                                                  "height =  (int) [ 1, 1242 ] , "
                                                  "framerate =  (fraction) [ 15, 100 ]") );

/* class initialization */

G_DEFINE_TYPE (GstZedSrc, gst_zedsrc, GST_TYPE_PUSH_SRC);

static void
gst_zedsrc_class_init (GstZedSrcClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
    GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
    GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);

    gobject_class->set_property = gst_zedsrc_set_property;
    gobject_class->get_property = gst_zedsrc_get_property;
    gobject_class->dispose = gst_zedsrc_dispose;
    gobject_class->finalize = gst_zedsrc_finalize;

    gst_element_class_add_pad_template (gstelement_class,
                                        gst_static_pad_template_get (&gst_zedsrc_src_template));

    gst_element_class_set_static_metadata (gstelement_class,
                                           "ZED Video Source", "Source/Video",
                                           "Stereolabs ZED video source", "Stereolabs <support@stereolabs.com>");

    gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_zedsrc_start);
    gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_zedsrc_stop);
    gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_zedsrc_get_caps);
    gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_zedsrc_set_caps);
    gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_zedsrc_unlock);
    gstbasesrc_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_zedsrc_unlock_stop);

    gstpushsrc_class->fill = GST_DEBUG_FUNCPTR (gst_zedsrc_fill);

    /* Install GObject properties */
    g_object_class_install_property( gobject_class, PROP_CAM_RES,
                                     g_param_spec_enum("resolution", "Camera Resolution",
                                                       "Camera Resolution", GST_TYPE_ZED_RESOL, DEFAULT_PROP_CAM_RES,
                                                       (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_CAM_FPS,
                                     g_param_spec_enum("framerate", "Camera frame rate",
                                                       "Camera frame rate", GST_TYPE_ZED_FPS, DEFAULT_PROP_CAM_FPS,
                                                       (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_SDK_VERBOSE,
                                     g_param_spec_boolean("verbose", "ZED SDK Verbose",
                                                          "ZED SDK Verbose", DEFAULT_PROP_SDK_VERBOSE,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_CAM_FLIP,
                                     g_param_spec_boolean("flip", "Camera flip",
                                                          "Flip images and depth info if camera is flipped",
                                                          DEFAULT_PROP_CAM_FLIP,
                                                          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_CAM_ID,
                                     g_param_spec_int("camera-id", "Camera ID",
                                                      "Camera ID",-1,255,-1,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_CAM_SN,
                                     g_param_spec_int64("camera-sn", "Camera Serial Number",
                                                        "Camera Serial Number",-1,G_MAXINT64,
                                                        DEFAULT_PROP_CAM_SN,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_SVO_FILE,
                                     g_param_spec_string("svo-file-path", "SVO file",
                                                         "Input from SVO file",
                                                         DEFAULT_PROP_SVO_FILE,
                                                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_STREAM_IP,
                                     g_param_spec_string("stream-ip-addr", "Stream IP",
                                                         "Input from remote source",
                                                         DEFAULT_PROP_SVO_FILE,
                                                         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property( gobject_class, PROP_STREAM_PORT,
                                     g_param_spec_int("stream-port", "Stream IP",
                                                      "Input from remote source",1,G_MAXINT16,
                                                      DEFAULT_PROP_STREAM_PORT,
                                                      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));


}

static void gst_zedsrc_reset (GstZedSrc * src)
{
    if(src->zed.isOpened())
    {
        src->zed.close();
    }

    src->out_framesize = 0;
    src->is_started = FALSE;

    src->last_frame_count = 0;
    src->total_dropped_frames = 0;

    if (src->caps) {
        gst_caps_unref (src->caps);
        src->caps = NULL;
    }
}

static void gst_zedsrc_init (GstZedSrc * src)
{
    /* set source as live (no preroll) */
    gst_base_src_set_live (GST_BASE_SRC (src), TRUE);

    /* override default of BYTES to operate in time mode */
    gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);

    /* initialize member variables */
    src->camera_resolution = DEFAULT_PROP_CAM_RES;
    src->camera_fps = DEFAULT_PROP_CAM_FPS;
    src->sdk_verbose = DEFAULT_PROP_SDK_VERBOSE;
    src->camera_image_flip = DEFAULT_PROP_CAM_FLIP;
    src->camera_id = DEFAULT_PROP_CAM_ID;
    src->camera_sn = DEFAULT_PROP_CAM_SN;
    src->svo_file = *g_string_new( DEFAULT_PROP_SVO_FILE );
    src->stream_ip = *g_string_new( DEFAULT_PROP_STREAM_IP );;
    src->stream_port = DEFAULT_PROP_STREAM_PORT;

    src->stop_requested = FALSE;
    src->caps = NULL;

    gst_zedsrc_reset (src);
}

void
gst_zedsrc_set_property (GObject * object, guint property_id,
                         const GValue * value, GParamSpec * pspec)
{
    GstZedSrc *src;

    src = GST_ZED_SRC(object);

    switch (property_id) {
    case PROP_CAM_RES:
        src->camera_resolution = g_value_get_enum(value);
        break;
    case PROP_CAM_FPS:
        src->camera_fps = g_value_get_enum(value);
        break;
    case PROP_SDK_VERBOSE:
        src->sdk_verbose = g_value_get_boolean(value);
        break;
    case PROP_CAM_FLIP:
        src->camera_image_flip = g_value_get_boolean(value);
        break;
    case PROP_CAM_ID:
        src->camera_id = g_value_get_int(value);
        break;
    case PROP_CAM_SN:
        src->camera_sn = g_value_get_int64(value);
        break;
    case PROP_SVO_FILE:
    {
        const gchar* str = g_value_get_string(value);
        src->svo_file = *g_string_new( str );
    }
        break;
    case PROP_STREAM_IP:
    {
        const gchar* str = g_value_get_string(value);
        src->stream_ip = *g_string_new( str );
    }
        break;
    case PROP_STREAM_PORT:
        src->stream_port = g_value_get_int(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

void
gst_zedsrc_get_property (GObject * object, guint property_id,
                         GValue * value, GParamSpec * pspec)
{
    GstZedSrc *src;

    g_return_if_fail (GST_IS_ZED_SRC (object));
    src = GST_ZED_SRC (object);

    switch (property_id) {
    case PROP_CAM_RES:
        g_value_set_enum( value, src->camera_resolution );
        break;
    case PROP_CAM_FPS:
        g_value_set_enum( value, src->camera_fps );
        break;
    case PROP_SDK_VERBOSE:
        g_value_set_boolean( value, src->sdk_verbose );
        break;
    case PROP_CAM_FLIP:
        g_value_set_boolean( value, src->camera_image_flip );
        break;
    case PROP_CAM_ID:
        g_value_set_int( value, src->camera_id );
        break;
    case PROP_CAM_SN:
        g_value_set_int64( value, src->camera_id );
        break;
    case PROP_SVO_FILE:
        g_value_set_string( value, src->svo_file.str );
        break;
    case PROP_STREAM_IP:
        g_value_set_string( value, src->stream_ip.str );
        break;
    case PROP_STREAM_PORT:
        g_value_set_int( value, src->stream_port );
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

void
gst_zedsrc_dispose (GObject * object)
{
    GstZedSrc *src;

    g_return_if_fail (GST_IS_ZED_SRC (object));
    src = GST_ZED_SRC (object);

    /* clean up as possible.  may be called multiple times */

    G_OBJECT_CLASS (gst_zedsrc_parent_class)->dispose (object);
}

void
gst_zedsrc_finalize (GObject * object)
{
    GstZedSrc *src;

    g_return_if_fail (GST_IS_ZED_SRC (object));
    src = GST_ZED_SRC (object);

    /* clean up object here */
    if (src->caps) {
        gst_caps_unref (src->caps);
        src->caps = NULL;
    }

    G_OBJECT_CLASS (gst_zedsrc_parent_class)->finalize (object);
}

static gboolean gst_zedsrc_calculate_caps(GstZedSrc* src)
{
    guint32 width, height;
    gint fps;
    GstVideoInfo vinfo;
    GstVideoFormat format = GST_VIDEO_FORMAT_BGRA;

    // ----> CAPS for color stream
    width = src->zed.getCameraInformation().camera_configuration.resolution.width;
    height = src->zed.getCameraInformation().camera_configuration.resolution.height;
    fps = static_cast<gint>(src->zed.getCameraInformation().camera_configuration.fps);

    if( format != GST_VIDEO_FORMAT_UNKNOWN ) {
        gst_video_info_init( &vinfo );
        gst_video_info_set_format( &vinfo, format, width, height );
        if (src->caps) {
            gst_caps_unref (src->caps);
        }
        src->out_framesize = (guint) GST_VIDEO_INFO_SIZE (&vinfo);
        vinfo.fps_n = fps;
        vinfo.fps_d = 1;
        src->caps = gst_video_info_to_caps (&vinfo);
    }
    // <---- CAPS for color stream

    gst_base_src_set_blocksize( GST_BASE_SRC(src), src->out_framesize );
    gst_base_src_set_caps ( GST_BASE_SRC(src), src->caps );
    GST_DEBUG_OBJECT( src, "Created caps %" GST_PTR_FORMAT, src->caps );

    return TRUE;
}

static gboolean gst_zedsrc_start( GstBaseSrc * bsrc )
{
    GstZedSrc *src = GST_ZED_SRC (bsrc);
    sl::ERROR_CODE ret;

    GST_DEBUG_OBJECT (src, "start");


    sl::InitParameters init_params;
    init_params.camera_resolution = static_cast<sl::RESOLUTION>(src->camera_resolution);
    init_params.camera_fps = src->camera_fps;
    init_params.sdk_verbose = src->sdk_verbose==TRUE;
    init_params.camera_image_flip = src->camera_image_flip;

    if( src->svo_file.len != 0 )
    {
        sl::String svo( static_cast<char*>(src->svo_file.str) );
        init_params.input.setFromSVOFile(svo);
    }
    else if( src->camera_id != DEFAULT_PROP_CAM_ID )
    {
        init_params.input.setFromCameraID(src->camera_id);
    }
    else if( src->camera_sn != DEFAULT_PROP_CAM_SN )
    {
        init_params.input.setFromSerialNumber(src->camera_sn);
    }
    else if( src->stream_ip.len != 0 )
    {
        sl::String ip( static_cast<char*>(src->stream_ip.str) );
        init_params.input.setFromStream(ip,src->stream_port);
    }


    // TODO Set parameters

    ret = src->zed.open( init_params );

    if (ret!=sl::ERROR_CODE::SUCCESS) {
        GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
                           ("Failed to open camera, '%s'", sl::toString(ret).c_str() ), (NULL));
        return FALSE;
    }

    if (!gst_zedsrc_calculate_caps(src) ) {
        return FALSE;
    }

    return TRUE;
}

static gboolean
gst_zedsrc_stop (GstBaseSrc * bsrc)
{
    GstZedSrc *src = GST_ZED_SRC (bsrc);

    GST_DEBUG_OBJECT (src, "stop");

    // TODO stop any started modules

    gst_zedsrc_reset( src );

    return TRUE;
}

static GstCaps* gst_zedsrc_get_caps( GstBaseSrc * bsrc, GstCaps * filter )
{
    GstZedSrc *src = GST_ZED_SRC (bsrc);
    GstCaps *caps;

    if (src->caps)
    {
        caps = gst_caps_copy (src->caps);
    }
    else
    {
        caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (src));
    }

    GST_DEBUG_OBJECT (src, "The caps before filtering are %" GST_PTR_FORMAT,
                      caps);

    if (filter && caps)
    {
        GstCaps *tmp = gst_caps_intersect( caps, filter );
        gst_caps_unref (caps);
        caps = tmp;
    }

    GST_DEBUG_OBJECT (src, "The caps after filtering are %" GST_PTR_FORMAT, caps);

    return caps;
}

static gboolean gst_zedsrc_set_caps( GstBaseSrc * bsrc, GstCaps * caps )
{
    GstZedSrc *src = GST_ZED_SRC (bsrc);
    GstVideoInfo vinfo;

    gst_caps_get_structure( caps, 0 );

    GST_DEBUG_OBJECT (src, "The caps being set are %" GST_PTR_FORMAT, caps);

    gst_video_info_from_caps (&vinfo, caps);

    if (GST_VIDEO_INFO_FORMAT (&vinfo) == GST_VIDEO_FORMAT_UNKNOWN)
    {
        goto unsupported_caps;
    }

    return TRUE;

unsupported_caps:
    GST_ERROR_OBJECT (src, "Unsupported caps: %" GST_PTR_FORMAT, caps);
    return FALSE;
}

static gboolean gst_zedsrc_unlock (GstBaseSrc * bsrc)
{
    GstZedSrc *src = GST_ZED_SRC (bsrc);

    GST_LOG_OBJECT (src, "unlock");

    src->stop_requested = TRUE;

    return TRUE;
}

static gboolean gst_zedsrc_unlock_stop (GstBaseSrc * bsrc)
{
    GstZedSrc *src = GST_ZED_SRC (bsrc);

    GST_LOG_OBJECT (src, "unlock_stop");

    src->stop_requested = FALSE;

    return TRUE;
}

static GstFlowReturn gst_zedsrc_fill( GstPushSrc * psrc, GstBuffer * buf )
{
    GstZedSrc *src = GST_ZED_SRC (psrc);
    sl::ERROR_CODE ret;
    GstMapInfo minfo;
    GstClock *clock;
    GstClockTime clock_time;

    static int temp_ugly_buf_index = 0;

    GST_LOG_OBJECT (src, "fill");

    if (!src->is_started) {
        /* TODO: check timestamps on buffers vs start time */
        src->acq_start_time =
                gst_clock_get_time (gst_element_get_clock (GST_ELEMENT (src)));

        src->is_started = TRUE;
    }

    // ----> ZED grab
    ret = src->zed.grab(); // TODO set runtime parameters

    if( ret!=sl::ERROR_CODE::SUCCESS )
    {
        GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
                           ("Grabbing failed with error '%s'", sl::toString(ret).c_str()), (NULL));
        return GST_FLOW_ERROR;
    }
    // <---- ZED grab

    // ----> Clock update
    clock = gst_element_get_clock (GST_ELEMENT (src));
    clock_time = gst_clock_get_time (clock);
    gst_object_unref (clock);
    // <---- Clock update

    // Memory mapping
    gst_buffer_map( buf, &minfo, GST_MAP_WRITE );

    // ----> RGB frame: LEFT
    sl::Mat rgb;
    ret = src->zed.retrieveImage(rgb, sl::VIEW::LEFT, sl::MEM::CPU );
    if( ret!=sl::ERROR_CODE::SUCCESS )
    {
        GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
                           ("Grabbing failed with error '%s'", sl::toString(ret).c_str()), (NULL));
        return GST_FLOW_ERROR;
    }
    // <---- RGB frame: LEFT

    // Memory copy
    memcpy(minfo.data, rgb.getPtr<sl::uchar4>(), (int) minfo.size);

    gst_buffer_unmap( buf, &minfo );

    GST_BUFFER_TIMESTAMP (buf) =
            GST_CLOCK_DIFF (gst_element_get_base_time (GST_ELEMENT (src)),
                            clock_time);
    GST_BUFFER_OFFSET (buf) = temp_ugly_buf_index++;

    if (src->stop_requested) {
        return GST_FLOW_FLUSHING;
    }

    return GST_FLOW_OK;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
    GST_DEBUG_CATEGORY_INIT (gst_zedsrc_debug, "zedsrc", 0,
                             "debug category for zedsrc element");
    gst_element_register (plugin, "zedsrc", GST_RANK_NONE,
                          gst_zedsrc_get_type ());

    return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
                   GST_VERSION_MINOR,
                   zed,
                   "Zed camera source",
                   plugin_init, GST_PACKAGE_VERSION, GST_PACKAGE_LICENSE, GST_PACKAGE_NAME,
                   GST_PACKAGE_ORIGIN)
