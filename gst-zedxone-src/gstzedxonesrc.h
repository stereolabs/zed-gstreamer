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

#ifndef _GST_ZED_X_ONE_SRC_H_
#define _GST_ZED_X_ONE_SRC_H_

#include <gst/base/gstpushsrc.h>

#include "lib/include/ArgusCapture.hpp"

G_BEGIN_DECLS

#define GST_TYPE_ZED_X_ONE_SRC (gst_zedxonesrc_get_type())
#define GST_ZED_X_ONE_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ZED_X_ONE_SRC, GstZedXOneSrc))
#define GST_ZED_X_ONE_SRC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ZED_X_ONE_SRC, GstZedXOneSrcClass))
#define GST_IS_ZED_X_ONE_SRC(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ZED_X_ONE_SRC))
#define GST_IS_ZED_X_ONE_SRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ZED_X_ONE_SRC))

typedef struct _GstZedXOneSrc GstZedXOneSrc;
typedef struct _GstZedXOneSrcClass GstZedXOneSrcClass;

struct _GstZedXOneSrc {
    GstPushSrc base_zedxonesrc;

    // ZED X One camera object
    std::unique_ptr<oc::ArgusBayerCapture> zed;

    gboolean is_started;   // grab started flag

    // ----> Properties
    gint camera_resolution;   // Camera resolution [enum]
    gint camera_fps;          // Camera FPS
    gint verbose_level;
    guint16 cam_timeout_msec;
    gint camera_id;
    //gint camera_sn;
    gboolean swap_rb; //swap for RGB(A) or BGR(A) output

    gint brightness;
    gint contrast;
    gint hue;
    gint saturation;
    gint sharpness;
    gint gamma;
    gint gain;
    gint exposure;
    gboolean aec_agc;
    gint aec_agc_roi_x;
    gint aec_agc_roi_y;
    gint aec_agc_roi_w;
    gint aec_agc_roi_h;
    gint aec_agc_roi_side;
    gint whitebalance_temperature;
    gboolean whitebalance_temperature_auto;
    // <---- Properties

    GstClockTime acq_start_time;
    guint32 last_frame_count;
    guint32 total_dropped_frames;

    GstCaps *caps;
    guint out_framesize;

    gboolean stop_requested;
};

struct _GstZedXOneSrcClass {
    GstPushSrcClass base_zedxonesrc_class;
};

G_GNUC_INTERNAL GType gst_zedxonesrc_get_type(void);

G_END_DECLS

#endif // _GST_ZED_X_ONE_SRC_H_
