// /////////////////////////////////////////////////////////////////////////

//
// Copyright (c) 2026, STEREOLABS.
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

// Waits of the ZED sources in create() (a camera restart, a free output buffer), ended at once by
// unlock(). Include it after GST_CAT_DEFAULT is defined, so that its logs use the element category.

#pragma once

#include <cuda_runtime.h>
#include <gst/gst.h>
#include <sl/Camera.hpp>

// GstBaseSrc calls unlock() to make create() return GST_FLOW_FLUSHING as soon as possible (stop,
// flush, EOS event, PLAYING to PAUSED), then unlock_stop(). create() reads unlock_count once when
// it starts: unlock() may be followed by unlock_stop() before create() looks again (EOS event), and
// the change of unlock_count still tells it that it was unlocked.
typedef struct {
    GMutex lock;                  // Protects the fields below and what create() waits for
    GCond cond;                   // Signaled by unlock() and when what create() waits for changes
    gboolean stop_requested;      // Set by unlock(), cleared by unlock_stop()
    guint unlock_count;           // Incremented by unlock()
    gboolean camera_restarting;   // Streaming thread only: grab() reports a camera restart
} GstZedWait;

static inline void gst_zed_wait_init(GstZedWait *wait) {
    g_mutex_init(&wait->lock);
    g_cond_init(&wait->cond);
    wait->stop_requested = FALSE;
    wait->unlock_count = 0;
    wait->camera_restarting = FALSE;
}

static inline void gst_zed_wait_clear(GstZedWait *wait) {
    g_cond_clear(&wait->cond);
    g_mutex_clear(&wait->lock);
}

// From GstBaseSrc::unlock()
static inline void gst_zed_wait_unlock(GstZedWait *wait) {
    g_mutex_lock(&wait->lock);
    wait->stop_requested = TRUE;
    wait->unlock_count++;
    g_cond_broadcast(&wait->cond);
    g_mutex_unlock(&wait->lock);
}

// From GstBaseSrc::unlock_stop()
static inline void gst_zed_wait_unlock_stop(GstZedWait *wait) {
    g_mutex_lock(&wait->lock);
    wait->stop_requested = FALSE;
    g_mutex_unlock(&wait->lock);
}

// Read once when create() starts
static inline guint gst_zed_wait_get_unlock_count(GstZedWait *wait) {
    g_mutex_lock(&wait->lock);
    const guint unlock_count = wait->unlock_count;
    g_mutex_unlock(&wait->lock);
    return unlock_count;
}

// Must be called with wait->lock held
static inline gboolean gst_zed_wait_is_unlocked_locked(GstZedWait *wait, guint unlock_count) {
    return wait->stop_requested || wait->unlock_count != unlock_count;
}

static inline gboolean gst_zed_wait_is_unlocked(GstZedWait *wait, guint unlock_count) {
    g_mutex_lock(&wait->lock);
    const gboolean unlocked = gst_zed_wait_is_unlocked_locked(wait, unlock_count);
    g_mutex_unlock(&wait->lock);
    return unlocked;
}

// Waits for timeout_us, or less if the element is unlocked. Returns FALSE if it is unlocked.
static inline gboolean gst_zed_wait_for(GstZedWait *wait, guint unlock_count, gint64 timeout_us) {
    const gint64 end_time = g_get_monotonic_time() + timeout_us;
    g_mutex_lock(&wait->lock);
    while (!gst_zed_wait_is_unlocked_locked(wait, unlock_count) &&
           g_cond_wait_until(&wait->cond, &wait->lock, end_time)) {
    }
    const gboolean unlocked = gst_zed_wait_is_unlocked_locked(wait, unlock_count);
    g_mutex_unlock(&wait->lock);
    return !unlocked;
}

// What grab() and retrieveImage() return while the camera restarts (ZED X reboot, USB reconnection
// with async-grab-camera-recovery, recovery of a GMSL capture shared by several cameras, which can
// report CUDA_ERROR for a while): there is no new image yet
static inline gboolean gst_zed_is_restarting(sl::ERROR_CODE ret) {
    return ret == sl::ERROR_CODE::CAMERA_REBOOTING || ret == sl::ERROR_CODE::CUDA_ERROR;
}

// Grabs the next frame with grab_fn. grab() itself cannot be interrupted: it returns within a
// frame period while the camera delivers images, and at once while the camera restarts. A restart
// is waited for up to timeout_s seconds (camera-recovery-timeout-sec), 0 stops the stream at once.
template <typename GrabFn>
static inline GstFlowReturn gst_zed_grab(GstElement *element, GstZedWait *wait, guint unlock_count,
                                         gfloat timeout_s, GrabFn grab_fn) {
    gint64 restart_start = 0;
    for (;;) {
        const sl::ERROR_CODE ret = grab_fn();

        if (gst_zed_wait_is_unlocked(wait, unlock_count)) {
            return GST_FLOW_FLUSHING;
        }

        if (gst_zed_is_restarting(ret)) {
            if (timeout_s <= 0.0f) {
                GST_ELEMENT_ERROR(element, RESOURCE, READ, ("The camera is restarting"),
                                  ("grab: %s, camera-recovery-timeout-sec is 0",
                                   sl::toString(ret).c_str()));
                return GST_FLOW_ERROR;
            }
            const gint64 now = g_get_monotonic_time();
            if (!wait->camera_restarting) {
                wait->camera_restarting = TRUE;
                GST_ELEMENT_WARNING(element, RESOURCE, READ,
                                    ("The camera is restarting, the stream resumes when it is back"),
                                    ("grab: %s", sl::toString(ret).c_str()));
            }
            if (restart_start == 0) {
                restart_start = now;
            } else if (now - restart_start > (gint64) (timeout_s * G_TIME_SPAN_SECOND)) {
                GST_ELEMENT_ERROR(element, RESOURCE, READ,
                                  ("The camera did not come back within %g seconds", timeout_s),
                                  ("grab: %s", sl::toString(ret).c_str()));
                return GST_FLOW_ERROR;
            }
            // Do not let a CUDA error of the restart fail the next calls of this thread
            cudaGetLastError();
            if (!gst_zed_wait_for(wait, unlock_count, 50 * G_TIME_SPAN_MILLISECOND)) {
                return GST_FLOW_FLUSHING;
            }
            continue;
        }

        if (ret == sl::ERROR_CODE::END_OF_SVOFILE_REACHED) {
            GST_INFO_OBJECT(element, "End of SVO file");
            return GST_FLOW_EOS;
        }

        if (ret > sl::ERROR_CODE::SUCCESS) {
            GST_ELEMENT_ERROR(element, RESOURCE, FAILED,
                              ("Grabbing failed with error: '%s' - %s", sl::toString(ret).c_str(),
                               sl::toVerbose(ret).c_str()),
                              (NULL));
            return GST_FLOW_ERROR;
        }

        // SUCCESS, or a warning (negative code) that comes with a new image
        if (wait->camera_restarting) {
            wait->camera_restarting = FALSE;
            GST_ELEMENT_INFO(element, RESOURCE, READ, ("The camera is back, the stream resumes"),
                             (NULL));
        }
        return GST_FLOW_OK;
    }
}
