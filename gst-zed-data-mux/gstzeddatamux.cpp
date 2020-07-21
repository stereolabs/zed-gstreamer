/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) YEAR AUTHOR_NAME AUTHOR_EMAIL
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-plugin
 *
 * FIXME:Describe plugin here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! plugin ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/gstbuffer.h>
#include <gst/gstcaps.h>

#include "gstzeddatamux.h"
#include "gst-zed-meta/gstzedmeta.h"

GST_DEBUG_CATEGORY_STATIC (gst_zeddatamux_debug);
#define GST_CAT_DEFAULT gst_zeddatamux_debug

/* Filter signals and args */
enum
{
    /* FILL ME */
    LAST_SIGNAL
};

enum
{
    PROP_0,
    PROP_IS_DEPTH,
    PROP_STREAM_DATA
};

#define DEFAULT_PROP_IS_DEPTH       TRUE
#define DEFAULT_PROP_STREAM_DATA   FALSE


/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_video_factory = GST_STATIC_PAD_TEMPLATE ("sink_video",
                                                                          GST_PAD_SINK,
                                                                          GST_PAD_ALWAYS,
                                                                          GST_STATIC_CAPS( ("video/x-raw, "
                                                                                            "format = (string) { BGRA, GRAY16_LE  }, "
                                                                                            "width = (int) { 672, 1280, 1920, 2208 } , "
                                                                                            "height =  (int) { 376, 720, 1080, 1242 } , "
                                                                                            "framerate =  (fraction)  { 15, 30, 60, 100 }"
                                                                                            ";"
                                                                                            "video/x-raw, " // Double stream
                                                                                            "format = (string) { BGRA, GRAY16_LE }, "
                                                                                            "width = (int) { 672, 1280, 1920, 2208 } , "
                                                                                            "height =  (int) { 752, 1440, 2160, 2484 } , "
                                                                                            "framerate =  (fraction) { 15, 30, 60, 100 }") ) );

static GstStaticPadTemplate sink_data_factory = GST_STATIC_PAD_TEMPLATE ("sink_data",
                                                                         GST_PAD_SINK,
                                                                         GST_PAD_ALWAYS,
                                                                         GST_STATIC_CAPS ("application/data"));


static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
                                                                   GST_PAD_SINK,
                                                                   GST_PAD_ALWAYS,
                                                                   GST_STATIC_CAPS( ("video/x-raw, "
                                                                                     "format = (string) { BGRA, GRAY16_LE  }, "
                                                                                     "width = (int) { 672, 1280, 1920, 2208 } , "
                                                                                     "height =  (int) { 376, 720, 1080, 1242 } , "
                                                                                     "framerate =  (fraction)  { 15, 30, 60, 100 }"
                                                                                     ";"
                                                                                     "video/x-raw, " // Double stream
                                                                                     "format = (string) { BGRA, GRAY16_LE }, "
                                                                                     "width = (int) { 672, 1280, 1920, 2208 } , "
                                                                                     "height =  (int) { 752, 1440, 2160, 2484 } , "
                                                                                     "framerate =  (fraction) { 15, 30, 60, 100 }") ) );



/* class initialization */
G_DEFINE_TYPE(GstZedDataMux, gst_zeddatamux, GST_TYPE_ELEMENT);

static void gst_zeddatamux_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_zeddatamux_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_zeddatamux_finalize  (GObject * object);
static GstStateChangeReturn gst_zeddatamux_change_state (GstElement * element, GstStateChange transition);
static GstFlowReturn  gst_zeddatamux_collected (GstCollectPads * pads, GstZedDataMux * self);
static void gst_zeddatamux_reset(GstZedDataMux * self);
static gboolean gst_zeddatamux_sink_event (GstCollectPads * pads, GstCollectData * cdata, GstEvent * event, GstZedDataMux * self);
static gboolean gst_zeddatamux_sink_query (GstCollectPads * pads, GstCollectData * cdata, GstQuery * query, GstZedDataMux * self);
static gboolean gst_zeddatamux_set_caps (GstZedDataMux * self, GstPad * pad, GstCaps * caps);
static GstCaps* gst_zeddatamux_get_caps (GstZedDataMux * self, GstPad * pad, GstCaps * filter);
static GstCaps* gst_zeddatamux_query_pad_caps (GstPad * pad, GstPad * skip, GstCaps * filter);


/* initialize the plugin's class */
static void
gst_zeddatamux_class_init (GstZedDataMuxClass * klass)
{
    GST_TRACE ("class_init" );

    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

    GST_DEBUG_OBJECT( gobject_class, "Class Init" );

    gobject_class->finalize = gst_zeddatamux_finalize;
    gobject_class->set_property = gst_zeddatamux_set_property;
    gobject_class->get_property = gst_zeddatamux_get_property;

    gst_element_class_set_static_metadata (gstelement_class,
                                           "ZED Stream Data Muxer",
                                           "Muxer/Video",
                                           "Stereolabs ZED Data Muxer",
                                           "Stereolabs <support@stereolabs.com>");

    gst_element_class_add_pad_template (gstelement_class,
                                        gst_static_pad_template_get (&src_factory));

    gst_element_class_add_pad_template (gstelement_class,
                                        gst_static_pad_template_get (&sink_video_factory));

    gst_element_class_add_pad_template (gstelement_class,
                                        gst_static_pad_template_get (&sink_data_factory));

    gstelement_class->change_state =
            GST_DEBUG_FUNCPTR (gst_zeddatamux_change_state);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void gst_zeddatamux_init (GstZedDataMux *self)
{
    GST_TRACE ("init" );

    GST_DEBUG_OBJECT( self, "Filter Init" );

    self->sinkvideopad = gst_pad_new_from_static_template (&sink_video_factory, "sink_video");
    gst_element_add_pad (GST_ELEMENT (self), self->sinkvideopad);

    self->sinkdatapad = gst_pad_new_from_static_template( &sink_data_factory, "sink_data" );
    gst_element_add_pad (GST_ELEMENT (self), self->sinkdatapad);

    self->srcpad = gst_pad_new_from_static_template( &src_factory, "src" );
    gst_element_add_pad(GST_ELEMENT (self), self->srcpad);

    self->collect = gst_collect_pads_new ();
    gst_collect_pads_set_function (self->collect, (GstCollectPadsFunction) gst_zeddatamux_collected, self);
    gst_collect_pads_set_event_function (self->collect, (GstCollectPadsEventFunction) gst_zeddatamux_sink_event, self);
    gst_collect_pads_set_query_function (self->collect, (GstCollectPadsQueryFunction) gst_zeddatamux_sink_query, self);

    self->caps= nullptr;
}

static gboolean gst_zeddatamux_sink_query (GstCollectPads * pads, GstCollectData * cdata,
                                           GstQuery * query, GstZedDataMux * self)
{
    GST_TRACE ("sink_query" );

    gboolean ret = TRUE;

    switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
        GstCaps *filter, *caps;
        gst_query_parse_caps (query, &filter);
        caps = gst_zeddatamux_get_caps (self, cdata->pad, filter);
        gst_query_set_caps_result (query, caps);
        gst_caps_unref (caps);
        break;
    }
    default:
        ret = gst_collect_pads_query_default (pads, cdata, query, FALSE);
        break;
    }
    return ret;
}

static void gst_zeddatamux_finalize  (GObject * object)
{
    GST_TRACE ("finalize" );

    GstZedDataMux *self = GST_ZEDDATAMUX (object);
    //GstZedDataMuxClass *klass = GST_ZEDDATAMUX_CLASS (object);

    if (self->collect)
        gst_object_unref (self->collect);
    self->collect = NULL;

    //G_OBJECT_CLASS (g_type_class_peek_parent (klass))->finalize (object);
}

static void gst_zeddatamux_reset(GstZedDataMux * self)
{
    GST_TRACE ("reset" );

    GstEvent **p_ev;

    gst_caps_replace (&self->caps, NULL);
    p_ev = &self->segment_event;
    gst_event_replace (p_ev, NULL);

    gst_video_info_init (&self->info);
}

static gboolean gst_zeddatamux_set_caps (GstZedDataMux * self, GstPad * pad, GstCaps * caps)
{
    GST_TRACE ("set_caps" );

    gboolean ret = TRUE;

    if (!self->caps) {
        gst_caps_replace (&self->caps, caps);

        ret = gst_pad_set_caps (self->srcpad, caps);

        if (ret)
        {
            GstVideoInfo info;

            gst_video_info_init (&info);
            if (!gst_video_info_from_caps (&self->info, caps))
            {
                ret = FALSE;
            }
        }
    }
    else if (!gst_caps_is_equal (caps, self->caps))
    {
        GstCaps *upstream_caps;

        upstream_caps = gst_pad_peer_query_caps (pad, NULL);
        if (gst_caps_can_intersect (self->caps, upstream_caps))
            gst_pad_push_event (pad, gst_event_new_reconfigure ());
        gst_caps_unref (upstream_caps);
        ret = FALSE;
    }

    return ret;
}

static GstCaps* gst_zeddatamux_query_pad_caps (GstPad * pad, GstPad * skip, GstCaps * filter)
{
    GST_TRACE ("query_pad_caps" );

    GstCaps *caps;

    if (pad == skip)
        return filter;

    caps = gst_pad_peer_query_caps (pad, filter);

    if (caps)
        gst_caps_unref (filter);
    else
        caps = filter;

    return caps;
}

static GstCaps* gst_zeddatamux_get_caps (GstZedDataMux * self, GstPad * pad, GstCaps * filter)
{
    GST_TRACE ("get_caps" );

    GstCaps *caps = NULL;

    if (self->caps) {
        caps = gst_caps_ref (self->caps);
    } else {
        GstCaps *tmp;

        caps = gst_pad_get_pad_template_caps (self->srcpad);
        if (filter) {
            tmp = caps;
            caps = gst_caps_intersect_full (tmp, filter, GST_CAPS_INTERSECT_FIRST);
            gst_caps_unref (tmp);
        }

        caps = gst_zeddatamux_query_pad_caps (self->srcpad, pad, caps);
        caps = gst_zeddatamux_query_pad_caps (self->sinkvideopad, pad, caps);
        caps = gst_zeddatamux_query_pad_caps (self->sinkdatapad, pad, caps);
    }

    return caps;
}

static gboolean gst_zeddatamux_sink_event (GstCollectPads * pads, GstCollectData * cdata, GstEvent * event, GstZedDataMux * self)
{
    GST_TRACE ("sink_event" );

    gboolean ret = TRUE;

    switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
        GstCaps *caps;
        gst_event_parse_caps (event, &caps);
        ret = gst_zeddatamux_set_caps (self, cdata->pad, caps);
        gst_event_unref (event);
        break;
    }
    default:
        ret = gst_collect_pads_event_default (pads, cdata, event, FALSE);
        break;
    }
    return ret;
}


static GstStateChangeReturn gst_zeddatamux_change_state (GstElement * element, GstStateChange transition)
{
    GST_TRACE ("change_state" );

    GstZedDataMux *self = GST_ZEDDATAMUX (element);
    GstStateChangeReturn ret;

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
        break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        gst_collect_pads_start (self->collect);
        break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
        break;
    default:
        break;
    }

    /* Stop before calling the parent's state change function as
       * GstCollectPads might take locks and we would deadlock in that
       * case
       */
    if (transition == GST_STATE_CHANGE_PAUSED_TO_READY)
        gst_collect_pads_stop (self->collect);

    ret = GST_ELEMENT_CLASS (gst_zeddatamux_parent_class)->change_state (element, transition);

    switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        gst_zeddatamux_reset (self);
        break;
    case GST_STATE_CHANGE_READY_TO_NULL:
        break;
    default:
        break;
    }

    return ret;
}

static void
gst_zeddatamux_set_property (GObject * object, guint prop_id,
                             const GValue * value, GParamSpec * pspec)
{
    GST_TRACE ("set_property" );

    GstZedDataMux *filter = GST_ZEDDATAMUX (object);

    GST_DEBUG_OBJECT( filter, "Set property" );

    switch (prop_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gst_zeddatamux_get_property (GObject * object, guint prop_id,
                             GValue * value, GParamSpec * pspec)
{
    GST_TRACE ("get_property" );

    GstZedDataMux *filter = GST_ZEDDATAMUX (object);

    GST_DEBUG_OBJECT( filter, "Get property" );

    switch (prop_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static GstFlowReturn  gst_zeddatamux_collected (GstCollectPads * pads, GstZedDataMux * self)
{
    GST_TRACE ("collected" );

    GstZedDataMuxClass *klass = GST_ZEDDATAMUX_CLASS (self);

    GstBuffer *inbuf0 = NULL, *inbuf1 = NULL;
    GstBuffer *outbuf = NULL;

    GstFlowReturn ret = GST_FLOW_OK;
    GSList *l;

    GstClockTime timestamp;
    gdouble time;

    GstMapInfo outmap, inmap0, inmap1;


}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean plugin_init (GstPlugin * plugin)
{
    GST_TRACE ("plugin_init" );

    /* debug category for fltering log messages
     *
     * exchange the string 'Template plugin' with your description
     */
    GST_DEBUG_CATEGORY_INIT( gst_zeddatamux_debug, "zeddatamux",
                             0, "debug category for zeddatamux element");

    gst_element_register( plugin, "zeddatamux", GST_RANK_NONE,
                          gst_zeddatamux_get_type());

    return TRUE;
}

/* gstreamer looks for this structure to register plugins
 *
 * exchange the string 'Template plugin' with your plugin description
 */
GST_PLUGIN_DEFINE( GST_VERSION_MAJOR,
                   GST_VERSION_MINOR,
                   zeddatamux,
                   "ZED stream data muxer",
                   plugin_init,
                   GST_PACKAGE_VERSION,
                   GST_PACKAGE_LICENSE,
                   GST_PACKAGE_NAME,
                   GST_PACKAGE_ORIGIN)
