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

// Waits of the ZED sources in create() (a free output buffer), ended at once by unlock(). Include
// it after GST_CAT_DEFAULT is defined, so that its logs use the element category.

#pragma once

#include <gst/gst.h>

// GstBaseSrc calls unlock() to make create() return GST_FLOW_FLUSHING as soon as possible (stop,
// flush, EOS event, PLAYING to PAUSED), then unlock_stop(). create() reads unlock_count once when
// it starts: unlock() may be followed by unlock_stop() before create() looks again (EOS event), and
// the change of unlock_count still tells it that it was unlocked.
typedef struct {
    GMutex lock;                  // Protects the fields below and what create() waits for
    GCond cond;                   // Signaled by unlock() and when what create() waits for changes
    gboolean stop_requested;      // Set by unlock(), cleared by unlock_stop()
    guint unlock_count;           // Incremented by unlock()
} GstZedWait;

static inline void gst_zed_wait_init(GstZedWait *wait) {
    g_mutex_init(&wait->lock);
    g_cond_init(&wait->cond);
    wait->stop_requested = FALSE;
    wait->unlock_count = 0;
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
