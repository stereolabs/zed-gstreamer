// /////////////////////////////////////////////////////////////////////////

//
// Copyright (c) 2020, STEREOLABS.
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

    // ZED camera object
    sl::Camera zed;

    gboolean is_started; // grab started flag

    // ----> Properties
    gint camera_resolution;     // Camera resolution [enum]
    gint camera_fps;            // Camera FPS [enum]
    gboolean sdk_verbose;
    gint camera_image_flip;
    gint camera_id;
    gint64 camera_sn;
    GString svo_file;
    GString stream_ip;
    gint stream_port;
    gint stream_type;

    gfloat depth_min_dist;
    gfloat depth_max_dist;
    gboolean camera_disable_self_calib;
    gboolean depth_stabilization;
    //gboolean enable_right_side_measure;
    gboolean pos_tracking;
    gboolean camera_static;
    gint coord_sys;

    gboolean object_detection;
    gboolean od_image_sync;
    gboolean od_enable_tracking;
    gboolean od_enable_mask_output;
    gint od_detection_model;
    gfloat od_det_conf;
    // <---- Properties


    GstClockTime acq_start_time;
    guint32 last_frame_count;
    guint32 total_dropped_frames;

    GstCaps *caps;
    guint out_framesize;

    gboolean stop_requested;
};

struct _GstZedSrcClass
{
    GstPushSrcClass base_zedsrc_class;
};

G_GNUC_INTERNAL GType gst_zedsrc_get_type (void);

G_END_DECLS

#endif
