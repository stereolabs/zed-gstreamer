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

#pragma once

#include <cuda_runtime.h>
#include <gst/base/gstbasesrc.h>
#include <gst/gst.h>
#include <sl/Camera.hpp>

///
/// \brief Shared recovery-aware grab loop for ZED GStreamer source plugins.
///
/// During multi-camera Argus recovery, grab() returns CAMERA_REBOOTING (-1)
/// for 10-30s while the ProviderGuardian coordinates provider destruction
/// and recreation.  This helper retries instead of killing the pipeline.
///
/// \param element      GstElement pointer (for GST_*_OBJECT logging macros)
/// \param basesrc      GstBaseSrc pointer (for flushing check)
/// \param grab_fn      Callable returning sl::ERROR_CODE (e.g., grab())
/// \param max_wait_sec Maximum seconds to wait before declaring timeout
/// \param[out] waited  Set to the number of seconds spent waiting (0 if no recovery)
///
/// \return The final sl::ERROR_CODE from grab_fn.
///         On timeout: the last recovery error code (caller should post GST_ELEMENT_ERROR).
///         On flushing: sl::ERROR_CODE::FAILURE with *waited set to -1 as sentinel.
///
/// Usage:
/// \code
///   int waited = 0;
///   ret = zed_gst_grab_with_recovery(GST_ELEMENT(src), GST_BASE_SRC(src),
///       [&]() { return src->zed.grab(rtParams); }, 60, &waited);
///   if (waited == -1) { /* pipeline flushing */ }
///   if (waited > 0) GST_INFO_OBJECT(src, "recovered after %ds", waited);
/// \endcode
///
template <typename GrabFn>
static inline sl::ERROR_CODE zed_gst_grab_with_recovery(GstElement *element, GstBaseSrc *basesrc,
                                                        GrabFn grab_fn, int max_wait_sec,
                                                        int *waited) {
    *waited = 0;

    while (true) {
        sl::ERROR_CODE ret = grab_fn();

        if (ret != sl::ERROR_CODE::CAMERA_REBOOTING && ret != sl::ERROR_CODE::CUDA_ERROR) {
            return ret;
        }

        // Recovery path.
        if (*waited == 0)
            GST_WARNING_OBJECT(element, "Camera recovering (error: %s), waiting...",
                               sl::toString(ret).c_str());

        if (++(*waited) > max_wait_sec) {
            GST_ERROR_OBJECT(element, "Camera recovery timeout after %ds (last error: %s)",
                             max_wait_sec, sl::toString(ret).c_str());
            return ret;   // caller posts GST_ELEMENT_ERROR
        }

        // Sleep 1s in 100ms chunks — check for pipeline flushing so that
        // gst_element_set_state(NULL) isn't blocked.
        for (int ms = 0; ms < 1000; ms += 100) {
            if (GST_PAD_IS_FLUSHING(GST_BASE_SRC_PAD(basesrc))) {
                *waited = -1;   // sentinel: flushing
                return sl::ERROR_CODE::FAILURE;
            }
            g_usleep(100000);
        }
        {
            cudaError_t cu = cudaGetLastError();
            if (cu != cudaSuccess)
                GST_DEBUG_OBJECT(element, "Cleared CUDA error %d during recovery", (int) cu);
        }
    }
}
