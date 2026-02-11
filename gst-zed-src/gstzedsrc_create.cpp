static GstFlowReturn gst_zedsrc_create(GstPushSrc *psrc, GstBuffer **outbuf) {
    GstZedSrc *src = GST_ZED_SRC(psrc);

    // Use resolved_stream_type which accounts for AUTO negotiation
    gint stream_type = src->resolved_stream_type;

    // For non-NVMM modes, fall back to the default fill() path
    if (stream_type != GST_ZEDSRC_RAW_NV12 && stream_type != GST_ZEDSRC_RAW_NV12_STEREO) {
        return GST_PUSH_SRC_CLASS(gst_zedsrc_parent_class)->create(psrc, outbuf);
    }

    GST_TRACE_OBJECT(src, "gst_zedsrc_create (NVMM zero-copy)");

    sl::ERROR_CODE ret;
    GstFlowReturn flow_ret = GST_FLOW_OK;
    GstClock *clock = nullptr;
    GstClockTime clock_time = GST_CLOCK_TIME_NONE;
    CUcontext zctx;

    // Acquisition start time
    if (!src->is_started) {
        GstClock *start_clock = gst_element_get_clock(GST_ELEMENT(src));
        if (start_clock) {
            src->acq_start_time = gst_clock_get_time(start_clock);
            gst_object_unref(start_clock);
        }
        src->is_started = TRUE;
    }

    // Runtime parameters - Unified path
    sl::RuntimeParameters zedRtParams;
    gst_zedsrc_setup_runtime_parameters(src, zedRtParams);

    // Grab frame
    if (cuCtxPushCurrent_v2(src->zed.getCUDAContext()) != CUDA_SUCCESS) {
        GST_ERROR_OBJECT(src, "Failed to push CUDA context");
        return GST_FLOW_ERROR;
    }

    ret = src->zed.grab(zedRtParams);
    if (ret == sl::ERROR_CODE::END_OF_SVOFILE_REACHED) {
        GST_INFO_OBJECT(src, "End of SVO file");
        cuCtxPopCurrent_v2(NULL);
        return GST_FLOW_EOS;
    } else if (ret != sl::ERROR_CODE::SUCCESS) {
        GST_ERROR_OBJECT(src, "grab() failed: %s", sl::toString(ret).c_str());
        cuCtxPopCurrent_v2(NULL);
        return GST_FLOW_ERROR;
    }

    // Get clock for timestamp
    clock = gst_element_get_clock(GST_ELEMENT(src));
    if (clock) {
        clock_time = gst_clock_get_time(clock);
        gst_object_unref(clock);
    }

    // Retrieve RawBuffer - allocate on heap for GstBuffer lifecycle
    sl::RawBuffer *raw_buffer = new sl::RawBuffer();
    ret = src->zed.retrieveImage(*raw_buffer);
    if (ret != sl::ERROR_CODE::SUCCESS) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED,
                          ("Failed to retrieve RawBuffer: '%s'", sl::toString(ret).c_str()),
                          (NULL));
        delete raw_buffer;
        cuCtxPopCurrent_v2(NULL);
        return GST_FLOW_ERROR;
    }

    // Get NvBufSurface
    NvBufSurface *nvbuf = static_cast<NvBufSurface *>(raw_buffer->getRawBuffer());
    if (!nvbuf) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("RawBuffer returned null NvBufSurface"), (NULL));
        delete raw_buffer;
        cuCtxPopCurrent_v2(NULL);
        return GST_FLOW_ERROR;
    }

    if (nvbuf->numFilled == 0) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("NvBufSurface has no filled surfaces"), (NULL));
        delete raw_buffer;
        cuCtxPopCurrent_v2(NULL);
        return GST_FLOW_ERROR;
    }

    // Log buffer info
    NvBufSurfaceParams *params = &nvbuf->surfaceList[0];
    GST_DEBUG_OBJECT(src, "NvBufSurface: %p, FD: %ld, size: %d, memType: %d, format: %d", nvbuf,
                     params->bufferDesc, params->dataSize, nvbuf->memType, params->colorFormat);

    // Create GstBuffer wrapping the NvBufSurface pointer directly
    // This is the NVIDIA convention: the buffer's memory data IS the NvBufSurface*
    // DeepStream and other NVIDIA elements expect this format
    GstBuffer *buf =
        gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY,   // Memory is read-only
                                    nvbuf,                      // Data pointer is the NvBufSurface*
                                    sizeof(NvBufSurface),       // Max size
                                    0,                          // Offset
                                    sizeof(NvBufSurface),       // Size
                                    raw_buffer,                 // User data for destroy callback
                                    raw_buffer_destroy_notify   // Called when buffer is unreffed
        );

    // Attach Unified Metadata
    gst_zedsrc_attach_metadata(src, buf, clock_time);

    cuCtxPopCurrent_v2(NULL);

    if (src->stop_requested) {
        gst_buffer_unref(buf);
        return GST_FLOW_FLUSHING;
    }

    *outbuf = buf;
    return GST_FLOW_OK;
}