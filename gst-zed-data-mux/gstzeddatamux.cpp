// /////////////////////////////////////////////////////////////////////////

//
// Copyright (c) 2024, STEREOLABS.
//
// All rights reserved.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// /////////////////////////////////////////////////////////////////////////

#include <memory>

#include <gst/gst.h>
#include <gst/gstbuffer.h>
#include <gst/gstcaps.h>
#include <gst/video/video.h>

#include "gst-zed-meta/gstzedmeta.h"
#include "gstzeddatamux.h"

GST_DEBUG_CATEGORY_STATIC(gst_zeddatamux_debug);
#define GST_CAT_DEFAULT gst_zeddatamux_debug

/* Filter signals and args */
enum {
    /* FILL ME */
    LAST_SIGNAL
};

enum {
    PROP_0,
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw, format=(string){ BGRA, GRAY16_LE }, "
                    "width=(int)[1,MAX], height=(int)[1,MAX], "
                    "framerate=(fraction)[0/1,MAX]"));
static GstStaticPadTemplate sink_video_factory = GST_STATIC_PAD_TEMPLATE(
    "sink_video", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw, format=(string){ BGRA, GRAY16_LE }, "
                    "width=(int)[1,MAX], height=(int)[1,MAX], "
                    "framerate=(fraction)[0/1,MAX]"));
static GstStaticPadTemplate sink_data_factory = GST_STATIC_PAD_TEMPLATE(
    "sink_data", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("application/data"));

// How many buffers of one input can wait for the matching buffer of the other input. Beyond that,
// the oldest one is dropped.
// The data usually arrives first (it is pushed first by `zeddemux` and its branch is light), so the
// data side covers a video branch up to 16 frames behind, a default `queue` holding 1 to 8 frames.
// A data buffer is about 450 KB. A pending video frame is a full image, and its branch is rarely
// ahead, hence a shorter limit.
#define MAX_PENDING_DATA 16
#define MAX_PENDING_VIDEO 4

/* class initialization */
G_DEFINE_TYPE(GstZedDataMux, gst_zeddatamux, GST_TYPE_ELEMENT);

static void gst_zeddatamux_set_property(GObject *object, guint prop_id, const GValue *value,
                                        GParamSpec *pspec);
static void gst_zeddatamux_get_property(GObject *object, guint prop_id, GValue *value,
                                        GParamSpec *pspec);
static GstStateChangeReturn gst_zeddatamux_change_state(GstElement *element,
                                                        GstStateChange transition);

static gboolean gst_zeddatamux_sink_data_event(GstPad *pad, GstObject *parent, GstEvent *event);
static GstFlowReturn gst_zeddatamux_chain_data(GstPad *pad, GstObject *parent, GstBuffer *buf);
static gboolean gst_zeddatamux_sink_video_event(GstPad *pad, GstObject *parent, GstEvent *event);
static GstFlowReturn gst_zeddatamux_chain_video(GstPad *pad, GstObject *parent, GstBuffer *buf);

static void clear_pending(GQueue *queue) {
    GstBuffer *buf;
    while ((buf = (GstBuffer *) g_queue_pop_head(queue)) != nullptr) {
        gst_buffer_unref(buf);
    }
}

// Forgets the pending buffers and the end of the stream of both inputs
static void gst_zeddatamux_reset(GstZedDataMux *filter) {
    g_mutex_lock(&filter->lock);
    clear_pending(&filter->pending_video);
    clear_pending(&filter->pending_data);
    filter->video_eos = FALSE;
    filter->data_eos = FALSE;
    filter->overflow_warned = FALSE;
    g_mutex_unlock(&filter->lock);
}

/* GObject vmethod implementations */

// The pending buffers are private copies owned by the element.
static void gst_zeddatamux_finalize(GObject *object) {
    GstZedDataMux *filter = GST_ZEDDATAMUX(object);
    clear_pending(&filter->pending_video);
    clear_pending(&filter->pending_data);
    g_mutex_clear(&filter->lock);
    if (filter->caps) {
        gst_caps_unref(filter->caps);
    }
    G_OBJECT_CLASS(gst_zeddatamux_parent_class)->finalize(object);
}

/* initialize the plugin's class */
static void gst_zeddatamux_class_init(GstZedDataMuxClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);

    GST_DEBUG_OBJECT(gobject_class, "Class Init");

    gobject_class->finalize = gst_zeddatamux_finalize;
    gobject_class->set_property = gst_zeddatamux_set_property;
    gobject_class->get_property = gst_zeddatamux_get_property;

    gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_zeddatamux_change_state);

    gst_element_class_set_static_metadata(gstelement_class, "ZED Data Video Muxer", "Muxer/Video",
                                          "Stereolabs ZED Data Video Muxer",
                                          "Stereolabs <support@stereolabs.com>");

    gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&src_factory));

    gst_element_class_add_pad_template(gstelement_class,
                                       gst_static_pad_template_get(&sink_data_factory));

    gst_element_class_add_pad_template(gstelement_class,
                                       gst_static_pad_template_get(&sink_video_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void gst_zeddatamux_init(GstZedDataMux *filter) {
    GST_DEBUG_OBJECT(filter, "Filter Init");

    filter->sinkpad_data = gst_pad_new_from_static_template(&sink_data_factory, "sink_data");
    gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad_data);

    filter->sinkpad_video = gst_pad_new_from_static_template(&sink_video_factory, "sink_video");
    gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad_video);

    filter->srcpad = gst_pad_new_from_static_template(&src_factory, "src");
    gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);

    gst_pad_set_event_function(filter->sinkpad_data,
                               GST_DEBUG_FUNCPTR(gst_zeddatamux_sink_data_event));
    gst_pad_set_event_function(filter->sinkpad_video,
                               GST_DEBUG_FUNCPTR(gst_zeddatamux_sink_video_event));

    gst_pad_set_chain_function(filter->sinkpad_video,
                               GST_DEBUG_FUNCPTR(gst_zeddatamux_chain_video));
    gst_pad_set_chain_function(filter->sinkpad_data, GST_DEBUG_FUNCPTR(gst_zeddatamux_chain_data));

    filter->caps = nullptr;

    g_mutex_init(&filter->lock);
    g_queue_init(&filter->pending_video);
    g_queue_init(&filter->pending_data);
    filter->video_eos = FALSE;
    filter->data_eos = FALSE;
    filter->overflow_warned = FALSE;
}

static void gst_zeddatamux_set_property(GObject *object, guint prop_id, const GValue *value,
                                        GParamSpec *pspec) {
    GstZedDataMux *filter = GST_ZEDDATAMUX(object);

    GST_DEBUG_OBJECT(filter, "Set property");

    switch (prop_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_zeddatamux_get_property(GObject *object, guint prop_id, GValue *value,
                                        GParamSpec *pspec) {
    GstZedDataMux *filter = GST_ZEDDATAMUX(object);

    GST_DEBUG_OBJECT(filter, "Get property");

    switch (prop_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/* GstElement vmethod implementations */

static gboolean set_out_caps(GstZedDataMux *filter, GstCaps *sink_caps) {
    GstVideoInfo vinfo_in;
    GstVideoInfo vinfo_out;

    GST_DEBUG_OBJECT(filter, "Sink caps %" GST_PTR_FORMAT, sink_caps);

    // ----> Caps source
    if (filter->caps) {
        gst_caps_unref(filter->caps);
    }

    gst_video_info_from_caps(&vinfo_in, sink_caps);

    gst_video_info_init(&vinfo_out);
    gst_video_info_set_format(&vinfo_out, vinfo_in.finfo->format, vinfo_in.width, vinfo_in.height);
    vinfo_out.fps_d = vinfo_in.fps_d;
    vinfo_out.fps_n = vinfo_in.fps_n;
    filter->caps = gst_video_info_to_caps(&vinfo_out);

    GST_DEBUG_OBJECT(filter, "Created video caps %" GST_PTR_FORMAT, filter->caps);
    if (gst_pad_set_caps(filter->srcpad, filter->caps) == FALSE) {
        return false;
    }
    // <---- Caps source

    return TRUE;
}

static GstStateChangeReturn gst_zeddatamux_change_state(GstElement *element,
                                                        GstStateChange transition) {
    GstZedDataMux *filter = GST_ZEDDATAMUX(element);

    const GstStateChangeReturn ret =
        GST_ELEMENT_CLASS(gst_zeddatamux_parent_class)->change_state(element, transition);

    if (transition == GST_STATE_CHANGE_PAUSED_TO_READY) {
        // Streaming has stopped: the next run starts again from small timestamps
        gst_zeddatamux_reset(filter);
    }

    return ret;
}

// The output ends once both inputs have ended, so that the buffers still coming on the input that
// is behind are muxed with the pending buffers of the input that ended first
static gboolean handle_eos(GstZedDataMux *filter, GstEvent *event, gboolean video) {
    g_mutex_lock(&filter->lock);
    if (video) {
        filter->video_eos = TRUE;
    } else {
        filter->data_eos = TRUE;
    }
    const gboolean both = filter->video_eos && filter->data_eos;
    if (both) {
        // Nothing can match them anymore
        clear_pending(&filter->pending_video);
        clear_pending(&filter->pending_data);
    }
    g_mutex_unlock(&filter->lock);

    if (!both) {
        GST_DEBUG_OBJECT(filter, "End of the %s input, waiting for the other one",
                         video ? "video" : "data");
        gst_event_unref(event);
        return TRUE;
    }
    return gst_pad_push_event(filter->srcpad, event);
}

// After a flush, the input starts again from new timestamps
static void handle_flush_stop(GstZedDataMux *filter, gboolean video) {
    g_mutex_lock(&filter->lock);
    if (video) {
        clear_pending(&filter->pending_video);
        filter->video_eos = FALSE;
    } else {
        clear_pending(&filter->pending_data);
        filter->data_eos = FALSE;
    }
    g_mutex_unlock(&filter->lock);
}

/* this function handles video sink events */
static gboolean gst_zeddatamux_sink_video_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    GstZedDataMux *filter;
    gboolean ret;

    filter = GST_ZEDDATAMUX(parent);

    GST_LOG_OBJECT(filter, "Received %s event: %" GST_PTR_FORMAT, GST_EVENT_TYPE_NAME(event),
                   event);

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
        GST_DEBUG_OBJECT(filter, "Event CAPS");
        GstCaps *caps;

        gst_event_parse_caps(event, &caps);
        /* do something with the caps */

        ret = set_out_caps(filter, caps);

        /* and forward */
        ret = gst_pad_event_default(pad, parent, event);
        break;
    }
    case GST_EVENT_EOS:
        ret = handle_eos(filter, event, TRUE);
        break;
    case GST_EVENT_FLUSH_STOP:
        handle_flush_stop(filter, TRUE);
        ret = gst_pad_event_default(pad, parent, event);
        break;
    default:
        ret = gst_pad_event_default(pad, parent, event);
        break;
    }

    return ret;
}

/* this function handles data sink events */
static gboolean gst_zeddatamux_sink_data_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    GstZedDataMux *filter;
    gboolean ret;

    filter = GST_ZEDDATAMUX(parent);

    GST_LOG_OBJECT(filter, "Received %s event: %" GST_PTR_FORMAT, GST_EVENT_TYPE_NAME(event),
                   event);

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
        GST_DEBUG_OBJECT(filter, "Event CAPS");
        GstCaps *caps;

        gst_event_parse_caps(event, &caps);
        /* do something with the caps */

        /* and forward */
        ret = gst_pad_event_default(pad, parent, event);
        break;
    }
    case GST_EVENT_EOS:
        ret = handle_eos(filter, event, FALSE);
        break;
    case GST_EVENT_FLUSH_STOP:
        handle_flush_stop(filter, FALSE);
        ret = gst_pad_event_default(pad, parent, event);
        break;
    default:
        ret = gst_pad_event_default(pad, parent, event);
        break;
    }

    return ret;
}

// A private copy of the content of `buf`, with its timestamp and offset. The input buffer itself is
// never kept, as it may belong to the pool of an upstream element.
static GstBuffer *private_copy(GstBuffer *buf, const GstMapInfo &map) {
    GstBuffer *copy = gst_buffer_new_allocate(NULL, map.size, NULL);
    if (copy) {
        gst_buffer_fill(copy, 0, map.data, map.size);
        GST_BUFFER_PTS(copy) = GST_BUFFER_PTS(buf);
        GST_BUFFER_OFFSET(copy) = GST_BUFFER_OFFSET(buf);
    }
    return copy;
}

// Takes the pending buffer of `queue` that has the timestamp `ts`, if any. The older ones are
// dropped: the other input is already past their timestamp. Called with the lock held.
static GstBuffer *take_pending(GstZedDataMux *filter, GQueue *queue, GstClockTime ts) {
    GstBuffer *head;
    while ((head = (GstBuffer *) g_queue_peek_head(queue)) != nullptr &&
           GST_BUFFER_PTS(head) < ts) {
        GST_DEBUG_OBJECT(filter,
                         "Nothing to mux with the %s buffer at %" GST_TIME_FORMAT ", dropped",
                         queue == &filter->pending_video ? "video" : "data",
                         GST_TIME_ARGS(GST_BUFFER_PTS(head)));
        gst_buffer_unref((GstBuffer *) g_queue_pop_head(queue));
    }
    if (head && GST_BUFFER_PTS(head) == ts) {
        return (GstBuffer *) g_queue_pop_head(queue);
    }
    return nullptr;
}

// Returns the pending buffer of the other input that has the timestamp of `copy`, the caller then
// keeps `copy`. Without one, returns nullptr and takes `copy`: it waits for the other input, or is
// dropped when the other input is already past its timestamp.
static GstBuffer *match_or_keep(GstZedDataMux *filter, GstBuffer *copy, gboolean video) {
    GQueue *own = video ? &filter->pending_video : &filter->pending_data;
    GQueue *other = video ? &filter->pending_data : &filter->pending_video;
    const guint max_pending = video ? MAX_PENDING_VIDEO : MAX_PENDING_DATA;
    const GstClockTime ts = GST_BUFFER_PTS(copy);

    g_mutex_lock(&filter->lock);
    GstBuffer *match = take_pending(filter, other, ts);
    if (!match) {
        if (!g_queue_is_empty(other)) {
            GST_DEBUG_OBJECT(filter,
                             "Nothing to mux with the %s buffer at %" GST_TIME_FORMAT ", dropped",
                             video ? "video" : "data", GST_TIME_ARGS(ts));
            gst_buffer_unref(copy);
        } else {
            if (g_queue_get_length(own) >= max_pending) {
                GstBuffer *oldest = (GstBuffer *) g_queue_pop_head(own);
                if (!filter->overflow_warned) {
                    GST_WARNING_OBJECT(filter,
                                       "The %s branch is more than %u frames behind the %s "
                                       "branch: the frames beyond are dropped",
                                       video ? "data" : "video", max_pending,
                                       video ? "video" : "data");
                    filter->overflow_warned = TRUE;
                }
                GST_DEBUG_OBJECT(filter,
                                 "Too many %s buffers waiting, the one at %" GST_TIME_FORMAT
                                 " is dropped",
                                 video ? "video" : "data", GST_TIME_ARGS(GST_BUFFER_PTS(oldest)));
                gst_buffer_unref(oldest);
            }
            g_queue_push_tail(own, copy);
        }
    }
    g_mutex_unlock(&filter->lock);

    return match;
}

// Attaches the metadata carried by `data` to `video` and pushes it. Takes both buffers.
static GstFlowReturn push_muxed(GstZedDataMux *filter, GstBuffer *video, GstBuffer *data) {
    GstMapInfo map;
    if (!gst_buffer_map(data, &map, GST_MAP_READ)) {
        gst_buffer_unref(data);
        gst_buffer_unref(video);
        GST_ELEMENT_ERROR(filter, RESOURCE, FAILED, ("Failed to map buffer for reading"), (NULL));
        return GST_FLOW_ERROR;
    }
    gst_buffer_copy_zed_src_meta(video, (GstZedSrcMeta *) map.data);
    gst_buffer_unmap(data, &map);
    gst_buffer_unref(data);

    GST_BUFFER_DTS(video) = GST_BUFFER_PTS(video);

    GST_TRACE_OBJECT(filter, "Out buffer push");
    const GstFlowReturn ret = gst_pad_push(filter->srcpad, video);
    if (ret != GST_FLOW_OK) {
        GST_DEBUG_OBJECT(filter, "Error pushing out buffer: %s", gst_flow_get_name(ret));
    }
    return ret;
}

// The flow returned when the buffer waits or is dropped, so nothing is pushed: like a push, it
// tells upstream when the output does not take buffers anymore, so that the stream can stop
static GstFlowReturn idle_flow(GstZedDataMux *filter) {
    const GstFlowReturn ret = gst_pad_get_last_flow_return(filter->srcpad);
    return (ret == GST_FLOW_EOS || ret == GST_FLOW_NOT_LINKED) ? ret : GST_FLOW_OK;
}

/* chain function for data sink
 * this function does the actual processing
 */
static GstFlowReturn gst_zeddatamux_chain_data(GstPad *pad, GstObject *parent, GstBuffer *buf) {
    // Chain functions own their input, including on error or when a frame is discarded.
    std::unique_ptr<GstBuffer, decltype(&gst_buffer_unref)> input(buf, gst_buffer_unref);
    GstZedDataMux *filter = GST_ZEDDATAMUX(parent);

    GST_LOG_OBJECT(filter, "Data timestamp %" GST_TIME_FORMAT, GST_TIME_ARGS(GST_BUFFER_PTS(buf)));

    GstMapInfo map_in;
    if (!gst_buffer_map(buf, &map_in, GST_MAP_READ)) {
        GST_ELEMENT_ERROR(filter, RESOURCE, FAILED, ("Failed to map buffer for reading"), (NULL));
        return GST_FLOW_ERROR;
    }
    if (map_in.size < sizeof(GstZedSrcMeta)) {
        gst_buffer_unmap(buf, &map_in);
        GST_ELEMENT_ERROR(filter, STREAM, FORMAT, ("Truncated ZED metadata buffer"), (NULL));
        return GST_FLOW_ERROR;
    }
    GstBuffer *data = private_copy(buf, map_in);
    gst_buffer_unmap(buf, &map_in);
    if (!data) {
        return GST_FLOW_ERROR;
    }

    GstBuffer *video = match_or_keep(filter, data, FALSE);
    if (!video) {
        return idle_flow(filter);
    }
    return push_muxed(filter, video, data);
}

/* chain function for video sink
 * this function does the actual processing
 */
static GstFlowReturn gst_zeddatamux_chain_video(GstPad *pad, GstObject *parent, GstBuffer *buf) {
    // Chain functions own their input, including on error or when a frame is discarded.
    std::unique_ptr<GstBuffer, decltype(&gst_buffer_unref)> input(buf, gst_buffer_unref);
    GstZedDataMux *filter = GST_ZEDDATAMUX(parent);

    GST_LOG_OBJECT(filter, "Video timestamp %" GST_TIME_FORMAT, GST_TIME_ARGS(GST_BUFFER_PTS(buf)));

    GstMapInfo map_in;
    if (!gst_buffer_map(buf, &map_in, GST_MAP_READ)) {
        GST_ELEMENT_ERROR(filter, RESOURCE, FAILED, ("Failed to map buffer for reading"), (NULL));
        return GST_FLOW_ERROR;
    }
    // The output is this copy once its metadata arrives
    GstBuffer *video = private_copy(buf, map_in);
    gst_buffer_unmap(buf, &map_in);
    if (!video) {
        return GST_FLOW_ERROR;
    }

    GstBuffer *data = match_or_keep(filter, video, TRUE);
    if (!data) {
        return idle_flow(filter);
    }
    return push_muxed(filter, video, data);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean plugin_init(GstPlugin *plugin) {
    /* debug category for filtering log messages */
    GST_DEBUG_CATEGORY_INIT(gst_zeddatamux_debug, "zeddatamux", 0,
                            "debug category for zeddatamux element");

    gst_element_register(plugin, "zeddatamux", GST_RANK_NONE, gst_zeddatamux_get_type());

    return TRUE;
}

/* gstreamer looks for this structure to register plugins */
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, zeddatamux, "ZED data video muxer",
                  plugin_init, GST_PACKAGE_VERSION, GST_PACKAGE_LICENSE, GST_PACKAGE_NAME,
                  GST_PACKAGE_ORIGIN)
