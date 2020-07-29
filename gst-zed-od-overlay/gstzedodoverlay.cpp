/*
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) YEAR AUTHOR_NAME AUTHOR_EMAIL
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
 * gst-launch -v -m fakesrc ! zedodoverlay ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#include <opencv2/opencv.hpp>
#include <gst-zed-meta/gstzedmeta.h>

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/controller/controller.h>
#include <gst/video/video.h>

#include "gstzedodoverlay.h"

GST_DEBUG_CATEGORY_STATIC (gst_zed_od_overlay_debug);
#define GST_CAT_DEFAULT gst_zed_od_overlay_debug

static void draw_objects( cv::Mat& image, guint8 obj_count, ZedObjectData* objs, gfloat scaleW, gfloat scaleH );
gboolean gst_zedoddisplaysink_event (GstBaseTransform * base, GstEvent * event);

/* Filter signals and args */
enum
{
    /* FILL ME */
    LAST_SIGNAL
};

enum
{
    PROP_0
};

/* the capabilities of the inputs and outputs.
 *
 * FIXME:describe the real formats here.
 */

static GstStaticPadTemplate sink_template =
        GST_STATIC_PAD_TEMPLATE (
            "sink",
            GST_PAD_SINK,
            GST_PAD_ALWAYS,
            GST_STATIC_CAPS(
                ("video/x-raw, " // Double stream
                 "format = (string) { BGRA }, "
                 "width = (int) { 672, 1280, 1920, 2208 } , "
                 "height =  (int) { 752, 1440, 2160, 2484 } , "
                 "framerate =  (fraction) { 15, 30, 60, 100 }"
                 ";"
                 "video/x-raw, " // Single stream
                 "format = (string) { BGRA }, "
                 "width = (int) { 672, 1280, 1920, 2208 } , "
                 "height =  (int) { 376, 720, 1080, 1242 } , "
                 "framerate =  (fraction) { 15, 30, 60, 100 }")  ) );

static GstStaticPadTemplate src_template =
        GST_STATIC_PAD_TEMPLATE (
            "src",
            GST_PAD_SRC,
            GST_PAD_ALWAYS,
            GST_STATIC_CAPS(
                ("video/x-raw, " // Double stream
                 "format = (string) { BGRA }, "
                 "width = (int) { 672, 1280, 1920, 2208 } , "
                 "height =  (int) { 752, 1440, 2160, 2484 } , "
                 "framerate =  (fraction) { 15, 30, 60, 100 }"
                 ";"
                 "video/x-raw, " // Single stream
                 "format = (string) { BGRA }, "
                 "width = (int) { 672, 1280, 1920, 2208 } , "
                 "height =  (int) { 376, 720, 1080, 1242 } , "
                 "framerate =  (fraction) { 15, 30, 60, 100 }")  ) );

#define gst_zed_od_overlay_parent_class parent_class
G_DEFINE_TYPE (GstZedOdOverlay, gst_zed_od_overlay, GST_TYPE_BASE_TRANSFORM);

static void gst_zed_od_overlay_set_property (GObject * object, guint prop_id,
                                             const GValue * value, GParamSpec * pspec);
static void gst_zed_od_overlay_get_property (GObject * object, guint prop_id,
                                             GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_zed_od_overlay_transform_ip (GstBaseTransform * base,
                                                      GstBuffer * outbuf);

/* GObject vmethod implementations */

/* initialize the plugin's class */
static void
gst_zed_od_overlay_class_init (GstZedOdOverlayClass * klass)
{
    GObjectClass *gobject_class;
    GstElementClass *gstelement_class;

    gobject_class = (GObjectClass *) klass;
    gstelement_class = (GstElementClass *) klass;

    gobject_class->set_property = gst_zed_od_overlay_set_property;
    gobject_class->get_property = gst_zed_od_overlay_get_property;

    gst_element_class_set_details_simple (gstelement_class,
                                          "ZedOdOverlay",
                                          "Generic/Filter",
                                          "Draws the results of ZED Object Detection module",
                                          "Stereolabs <support@stereolabs.com>");

    gst_element_class_add_pad_template (gstelement_class,
                                        gst_static_pad_template_get (&src_template));
    gst_element_class_add_pad_template (gstelement_class,
                                        gst_static_pad_template_get (&sink_template));

    GST_BASE_TRANSFORM_CLASS (klass)->transform_ip =
            GST_DEBUG_FUNCPTR (gst_zed_od_overlay_transform_ip);

    GST_BASE_TRANSFORM_CLASS (klass)->sink_event = GST_DEBUG_FUNCPTR (gst_zedoddisplaysink_event);

    //debug category for filtering log messages
    GST_DEBUG_CATEGORY_INIT (gst_zed_od_overlay_debug, "zedodoverlay", 0, "Zed Object Detection Overlay");
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_zed_od_overlay_init (GstZedOdOverlay *filter)
{
    filter->img_left_w=0;
    filter->img_left_h=0;
}

static void
gst_zed_od_overlay_set_property (GObject * object, guint prop_id,
                                 const GValue * value, GParamSpec * pspec)
{
    GstZedOdOverlay *filter = GST_ZED_OD_OVERLAY (object);

    switch (prop_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gst_zed_od_overlay_get_property (GObject * object, guint prop_id,
                                 GValue * value, GParamSpec * pspec)
{
    GstZedOdOverlay *filter = GST_ZED_OD_OVERLAY (object);

    switch (prop_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

/* GstBaseTransform vmethod implementations */
gboolean gst_zedoddisplaysink_event (GstBaseTransform * base, GstEvent * event)
{
    GstEventType type;
    GstZedOdOverlay *filter = GST_ZED_OD_OVERLAY (base);

    type = GST_EVENT_TYPE (event);

    GST_TRACE_OBJECT( filter, "Event %d [%d]", type, GST_EVENT_CAPS  );

    switch (type) {
    case GST_EVENT_EOS:
        break;
    case GST_EVENT_CAPS:
    {
        GST_DEBUG_OBJECT( filter, "Event CAPS" );
        GstCaps* caps;

        gst_event_parse_caps(event, &caps);

        GstVideoInfo vinfo_in;
        gst_video_info_from_caps(&vinfo_in, caps);
        filter->img_left_w = vinfo_in.width;
        filter->img_left_h = vinfo_in.height;
        if(vinfo_in.height==752 || vinfo_in.height==1440 || vinfo_in.height==2160 || vinfo_in.height==2484)
        {
            filter->img_left_h/=2; // Only half buffer size if the stream is composite
        }

        break;
    }
    default:
        break;
    }

    return GST_BASE_TRANSFORM_CLASS(parent_class)->sink_event(base, event);
}

/* this function does the actual processing
 */
static GstFlowReturn
gst_zed_od_overlay_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
    GstZedOdOverlay *filter = GST_ZED_OD_OVERLAY (base);
    GST_TRACE_OBJECT( filter, "transform_ip" );

    GstMapInfo map_buf;

    if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (outbuf)))
        gst_object_sync_values (GST_OBJECT (filter), GST_BUFFER_TIMESTAMP (outbuf));

    if(FALSE==gst_buffer_map(outbuf, &map_buf, GstMapFlags(GST_MAP_READ|GST_MAP_WRITE)))
    {
        GST_WARNING_OBJECT( filter, "Could not map buffer for write/read" );
        return GST_FLOW_OK;
    }

    // Get left image (upper half memory buffer)
    cv::Mat ocv_left = cv::Mat( filter->img_left_h, filter->img_left_w, CV_8UC4, map_buf.data );

    // Metadata
    GstZedSrcMeta* meta = (GstZedSrcMeta*)gst_buffer_get_meta( outbuf, GST_ZED_SRC_META_API_TYPE );

    if( meta==NULL ) // Metadata not found
    {
        GST_ELEMENT_ERROR (filter, RESOURCE, FAILED,
                           ("No ZED metadata [GstZedSrcMeta] found in the stream'" ), (NULL));
        return GST_FLOW_ERROR;
    }

    GST_TRACE_OBJECT( filter, "Cam. Model: %d",meta->info.cam_model );
    GST_TRACE_OBJECT( filter, "Stream type: %d",meta->info.stream_type );
    GST_TRACE_OBJECT( filter, "Grab frame Size: %d x %d",meta->info.grab_frame_width,meta->info.grab_frame_height );

    gboolean rescaled = FALSE;
    gfloat scaleW = 1.0f;
    gfloat scaleH = 1.0f;
    if(meta->info.grab_frame_width != filter->img_left_w ||
            meta->info.grab_frame_height != filter->img_left_h)
    {
        rescaled = TRUE;
        scaleW = ((gfloat)filter->img_left_w)/meta->info.grab_frame_width;
        scaleH = ((gfloat)filter->img_left_h)/meta->info.grab_frame_height;
    }

    if(meta->od_enabled)
    {
        GST_TRACE_OBJECT( filter, "Detected %d objects",meta->obj_count );
        // Draw 2D detections
        draw_objects( ocv_left, meta->obj_count, meta->objects, scaleW, scaleH );
    }


    GST_TRACE ("Buffer unmap" );
    gst_buffer_unmap( outbuf, &map_buf );

    return GST_FLOW_OK;
}


/* entry point to initialize the plug-in
         * initialize the plug-in itself
         * register the element factories and other features
         */
static gboolean
plugin_init (GstPlugin * plugin)
{
    return gst_element_register (plugin, "zedodoverlay", GST_RANK_NONE,
                                 GST_TYPE_ZED_OD_OVERLAY);
}

/* gstreamer looks for this structure to register plugins
         *
         * FIXME:exchange the string 'Template plugin' with you plugin description
         */
GST_PLUGIN_DEFINE (
        GST_VERSION_MAJOR,
        GST_VERSION_MINOR,
        zedodoverlay,
        "ZED Object Detection Overlay",
        plugin_init,
        GST_PACKAGE_VERSION,
        GST_PACKAGE_LICENSE,
        GST_PACKAGE_NAME,
        GST_PACKAGE_ORIGIN
        )

static void draw_objects( cv::Mat& image, guint8 obj_count, ZedObjectData* objs, gfloat scaleW, gfloat scaleH )
{
    for( int i=0; i<obj_count; i++ )
    {
        cv::Scalar color = cv::Scalar::all(125);
        if(objs[i].id>=0)
        {
            color = cv::Scalar((objs[i].id*232+232)%255, (objs[i].id*176+176)%255, (objs[i].id*59+59)%255);
        }

        cv::Rect roi_render(0, 0, image.size().width, image.size().height);

        GST_TRACE( "Object: %d", i );
        GST_TRACE( " * Id: %d", (int)objs[i].label );
        GST_TRACE( " * Pos: %g,%g,%g", objs[i].position[0],objs[i].position[1],objs[i].position[2] );

        if(objs[i].skeletons_avail==FALSE)
        {
            GST_TRACE( "Scale: %g, %g",scaleW,scaleH );

            // ----> Bounding box

            cv::Point2f tl;
            tl.x = objs[i].bounding_box_2d[0][0]*scaleW;
            tl.y = objs[i].bounding_box_2d[0][1]*scaleH;
            cv::Point2f br;
            br.x = objs[i].bounding_box_2d[2][0]*scaleW;
            br.y = objs[i].bounding_box_2d[2][1]*scaleH;
            cv::rectangle( image, tl, br, color, 3 );

            // <---- Bounding box

            // ----> Text info
            int baseline=0;
            int font_face = cv::FONT_HERSHEY_COMPLEX_SMALL;
            double font_scale = 0.75;

            std::stringstream txt_info;
            txt_info << "Id: " << objs[i].id << " - " << ((objs[i].label==OBJECT_CLASS::PERSON)?"PERSON":"VEHICLE");

            cv::Size txt_size = cv::getTextSize( txt_info.str(), font_face, font_scale, 1, &baseline );

            int offset = 5;

            cv::Point txt_tl;
            cv::Point txt_br;
            cv::Point txt_pos;

            txt_tl.x = tl.x;
            txt_tl.y = tl.y - (txt_size.height+2*offset);

            txt_br.x = tl.x + (txt_size.width+2*offset);
            txt_br.y = tl.y;

            txt_pos.x = txt_tl.x + offset;
            txt_pos.y = txt_br.y - offset;

            if( !roi_render.contains(txt_tl) )
            {
                txt_tl.y = tl.y + (txt_size.height+2*baseline);
                txt_pos.y = txt_tl.y - offset;
            }

            cv::rectangle( image, txt_tl, txt_br, color, 1 );
            cv::putText( image, txt_info.str(), txt_pos, font_face, font_scale, color, 1, cv::LINE_AA );

            if( !std::isnan(objs[i].position[0]) && !std::isnan(objs[i].position[1]) && !std::isnan(objs[i].position[2]) )
            {
                float dist = sqrtf( objs[i].position[0]*objs[i].position[0] +
                        objs[i].position[1]*objs[i].position[1] +
                        objs[i].position[2]*objs[i].position[2]);
                char text[64];
                sprintf(text, "%.2fm", abs(dist / 1000.0f));
                putText( image, text, cv::Point2i(tl.x+(br.x-tl.x)/2 - 20, tl.y+(br.y-tl.y)/2 - 12),
                         cv::FONT_HERSHEY_COMPLEX_SMALL, 0.75, color, 1 );
            }
            // <---- Text info
        }
        else
        {
            GST_TRACE( "Scale: %g, %g",scaleW,scaleH );
            // ----> Skeletons
            {
                // ----> Bones
                for (const auto& parts : skeleton::BODY_BONES)
                {
                    if( objs[i].keypoint_2d[skeleton::getIdx(parts.first)][0]>=0 &&
                            objs[i].keypoint_2d[skeleton::getIdx(parts.first)][1]>=0 &&
                            objs[i].keypoint_2d[skeleton::getIdx(parts.second)][0]>=0 &&
                            objs[i].keypoint_2d[skeleton::getIdx(parts.second)][1]>=0 )
                    {
                        cv::Point2f kp_a;
                        kp_a.x = objs[i].keypoint_2d[skeleton::getIdx(parts.first)][0]*scaleW;
                        kp_a.y = objs[i].keypoint_2d[skeleton::getIdx(parts.first)][1]*scaleH;
                        GST_TRACE( "kp_a: %g, %g",kp_a.x,kp_a.y );

                        cv::Point2f kp_b;
                        kp_b.x = objs[i].keypoint_2d[skeleton::getIdx(parts.second)][0]*scaleW;
                        kp_b.y = objs[i].keypoint_2d[skeleton::getIdx(parts.second)][1]*scaleH;
                        GST_TRACE( "kp_b: %g, %g",kp_b.x,kp_b.y );

                        if (roi_render.contains(kp_a) && roi_render.contains(kp_b))
                            cv::line(image, kp_a, kp_b, color, 1, cv::LINE_AA);
                    }
                }
                // <---- Bones
                // ----> Joints
                for(int j=0; j<18; j++)
                {
                    if( objs[i].keypoint_2d[j][0]>=0 &&
                            objs[i].keypoint_2d[j][1]>=0 )
                    {
                        cv::Point2f cv_kp;
                        cv_kp.x = objs[i].keypoint_2d[j][0]*scaleW;
                        cv_kp.y = objs[i].keypoint_2d[j][1]*scaleH;
                        if (roi_render.contains(cv_kp))
                        {
                            cv::circle(image, cv_kp, 3, color+cv::Scalar(50,50,50), -1, cv::LINE_AA);
                        }
                    }
                }
                // <---- Joints
            }
            // <---- Skeletons
        }
    }
}
