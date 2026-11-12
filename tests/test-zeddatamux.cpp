#define PACKAGE "regression-test"
#define GST_PACKAGE_VERSION "test"
#define GST_PACKAGE_LICENSE "LGPL"
#define GST_PACKAGE_NAME "regression-test"
#define GST_PACKAGE_ORIGIN "local"
#include "gst-zed-data-mux/gstzeddatamux.cpp"

static int freed;
static int received;
static gsize expected_size;
static GstFlowReturn downstream_result;
static void released(gpointer, GstMiniObject *) { ++freed; }

static GstFlowReturn consume(GstPad *, GstObject *, GstBuffer *buf) {
    ++received;
    g_assert_cmpuint(gst_buffer_get_size(buf), ==, expected_size);
    auto *meta = (GstZedSrcMeta *) gst_buffer_get_meta(buf, GST_ZED_SRC_META_API_TYPE);
    g_assert_nonnull(meta);
    g_assert_cmpuint(meta->image_timestamp_ns, ==, 123);
    g_assert_cmpuint(meta->center_of_exposure_timestamp_ns, ==, 456);
    GstMapInfo map;
    g_assert_true(gst_buffer_map(buf, &map, GST_MAP_READ));
    g_assert_cmpuint(map.data[map.size - 1], ==, 0xab);
    gst_buffer_unmap(buf, &map);
    gst_buffer_unref(buf); // The mux must not access this buffer after push returns.
    return downstream_result;
}

static GstBuffer *input(gboolean data, gsize size, GstClockTime pts) {
    GstBuffer *buf = gst_buffer_new_allocate(NULL, data ? sizeof(GstZedSrcMeta) : size, NULL);
    gst_buffer_memset(buf, 0, data ? 0 : 0xab, gst_buffer_get_size(buf));
    GST_BUFFER_PTS(buf) = pts;
    if (data) {
        GstMapInfo map;
        g_assert_true(gst_buffer_map(buf, &map, GST_MAP_WRITE));
        auto *meta = (GstZedSrcMeta *) map.data;
        meta->image_timestamp_ns = 123;
        meta->center_of_exposure_timestamp_ns = 456;
        gst_buffer_unmap(buf, &map);
    }
    gst_mini_object_weak_ref(GST_MINI_OBJECT(buf), released, NULL);
    return buf;
}

static void run_case(gboolean data_first, GstFlowReturn result) {
    freed = received = 0;
    downstream_result = result;
    auto *mux = GST_ZEDDATAMUX(g_object_new(gst_zeddatamux_get_type(), NULL));
    auto *sink = gst_pad_new("consumer", GST_PAD_SINK);
    gst_pad_set_chain_function(sink, consume);
    gst_pad_set_active(sink, TRUE);
    gst_pad_set_active(mux->srcpad, TRUE);
    g_assert_cmpint(gst_pad_link(mux->srcpad, sink), ==, GST_PAD_LINK_OK);
    gst_pad_push_event(mux->srcpad, gst_event_new_stream_start("test"));
    GstSegment segment;
    gst_segment_init(&segment, GST_FORMAT_TIME);
    gst_pad_push_event(mux->srcpad, gst_event_new_segment(&segment));
    // Replace an unmatched frame with a larger one, then match the new frame.
    g_assert_cmpint(gst_zeddatamux_chain_video(mux->sinkpad_video, GST_OBJECT(mux),
                                              input(FALSE, 16, 1)), ==, GST_FLOW_OK);
    g_assert_cmpint(freed, ==, 1); // Incoming video is copied, never retained.
    GstBuffer *cached_video = mux->last_video_buf;
    g_assert_nonnull(cached_video);
    expected_size = 4096;
    auto *video = input(FALSE, expected_size, 2);
    auto *data = input(TRUE, 0, 2);
    if (data_first) {
        g_assert_cmpint(gst_zeddatamux_chain_data(mux->sinkpad_data, GST_OBJECT(mux), data),
                        ==, GST_FLOW_OK);
        g_assert_cmpint(freed, ==, 2);
        g_assert_cmpint(gst_zeddatamux_chain_video(mux->sinkpad_video, GST_OBJECT(mux), video),
                        ==, result);
    } else {
        g_assert_cmpint(gst_zeddatamux_chain_video(mux->sinkpad_video, GST_OBJECT(mux), video),
                        ==, GST_FLOW_OK);
        g_assert_cmpint(freed, ==, 2);
        g_assert_cmpint(gst_zeddatamux_chain_data(mux->sinkpad_data, GST_OBJECT(mux), data),
                        ==, result);
    }
    g_assert_cmpint(received, ==, 1);
    g_assert_cmpint(freed, ==, 3);
    g_assert_cmpint(gst_zeddatamux_chain_video(mux->sinkpad_video, GST_OBJECT(mux),
                                              input(FALSE, 16, 3)), ==, GST_FLOW_OK);
    g_assert_cmpint(freed, ==, 4); // Unmatched input is also released immediately.
    int cached_freed = 0;
    auto count_cached = [](gpointer count, GstMiniObject *) { ++*(int *) count; };
    if (mux->last_video_buf) {
        gst_mini_object_weak_ref(GST_MINI_OBJECT(mux->last_video_buf), count_cached, &cached_freed);
    }
    if (mux->last_data_buf) {
        gst_mini_object_weak_ref(GST_MINI_OBJECT(mux->last_data_buf), count_cached, &cached_freed);
    }
    const int cached_count = !!mux->last_video_buf + !!mux->last_data_buf;
    gst_pad_unlink(mux->srcpad, sink);
    gst_pad_set_active(mux->srcpad, FALSE);
    gst_pad_set_active(sink, FALSE);
    gst_object_unref(sink);
    gst_object_unref(mux);
    g_assert_cmpint(freed, ==, 4);
    g_assert_cmpint(cached_freed, ==, cached_count);
}

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    g_log_set_always_fatal((GLogLevelFlags) (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL));
    for (auto result : {GST_FLOW_OK, GST_FLOW_ERROR}) {
        run_case(FALSE, result);
        run_case(TRUE, result);
    }
    g_print("datamux: both arrival orders, growing buffers, metadata, ownership and push errors passed\n");
}
