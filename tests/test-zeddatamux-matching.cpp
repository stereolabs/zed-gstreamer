#define PACKAGE "regression-test"
#define GST_PACKAGE_VERSION "test"
#define GST_PACKAGE_LICENSE "LGPL"
#define GST_PACKAGE_NAME "regression-test"
#define GST_PACKAGE_ORIGIN "local"
#include "gst-zed-data-mux/gstzeddatamux.cpp"

// Frame k has the timestamp k ms; its metadata carries frame_id k
static const GstClockTime FRAME = GST_MSECOND;

static int received;
static int eos_received;
static GstClockTime last_pts;
static GstFlowReturn downstream = GST_FLOW_OK;

static GstFlowReturn consume(GstPad *, GstObject *, GstBuffer *buf) {
    g_assert_cmpint(eos_received, ==, 0);
    auto *meta = (GstZedSrcMeta *) gst_buffer_get_meta(buf, GST_ZED_SRC_META_API_TYPE);
    g_assert_nonnull(meta);
    // Each frame is muxed with its own metadata, in order
    g_assert_cmpuint(meta->frame_id * FRAME, ==, GST_BUFFER_PTS(buf));
    g_assert_true(!GST_CLOCK_TIME_IS_VALID(last_pts) || GST_BUFFER_PTS(buf) > last_pts);
    last_pts = GST_BUFFER_PTS(buf);
    ++received;
    gst_buffer_unref(buf);
    return downstream;
}

static gboolean consume_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    if (GST_EVENT_TYPE(event) == GST_EVENT_EOS) {
        ++eos_received;
    }
    gst_event_unref(event);
    return TRUE;
}

struct Harness {
    GstElement *mux;
    GstPad *video;
    GstPad *data;
    GstPad *sink;
};

static void start_stream(Harness &h) {
    for (GstPad *src : {h.video, h.data}) {
        gst_pad_set_active(src, TRUE);
        gst_pad_push_event(src, gst_event_new_stream_start("test"));
    }
    GstCaps *caps = gst_caps_from_string("video/x-raw,format=BGRA,width=4,height=2,framerate=30/1");
    gst_pad_push_event(h.video, gst_event_new_caps(caps));
    gst_caps_unref(caps);
    caps = gst_caps_from_string("application/data");
    gst_pad_push_event(h.data, gst_event_new_caps(caps));
    gst_caps_unref(caps);
    GstSegment segment;
    gst_segment_init(&segment, GST_FORMAT_TIME);
    for (GstPad *src : {h.video, h.data}) {
        gst_pad_push_event(src, gst_event_new_segment(&segment));
    }
    received = eos_received = 0;
    last_pts = GST_CLOCK_TIME_NONE;
}

static Harness setup() {
    Harness h;
    h.mux = GST_ELEMENT(g_object_new(gst_zeddatamux_get_type(), NULL));
    h.video = gst_pad_new("video", GST_PAD_SRC);
    h.data = gst_pad_new("data", GST_PAD_SRC);
    h.sink = gst_pad_new("consumer", GST_PAD_SINK);
    gst_pad_set_chain_function(h.sink, consume);
    gst_pad_set_event_function(h.sink, consume_event);
    gst_pad_set_active(h.sink, TRUE);
    auto *mux = GST_ZEDDATAMUX(h.mux);
    g_assert_cmpint(gst_pad_link(h.video, mux->sinkpad_video), ==, GST_PAD_LINK_OK);
    g_assert_cmpint(gst_pad_link(h.data, mux->sinkpad_data), ==, GST_PAD_LINK_OK);
    g_assert_cmpint(gst_pad_link(mux->srcpad, h.sink), ==, GST_PAD_LINK_OK);
    g_assert_cmpint(gst_element_set_state(h.mux, GST_STATE_PLAYING), ==, GST_STATE_CHANGE_SUCCESS);
    start_stream(h);
    return h;
}

static void teardown(Harness &h) {
    gst_element_set_state(h.mux, GST_STATE_NULL);
    for (GstPad *pad : {h.video, h.data, h.sink}) {
        gst_pad_set_active(pad, FALSE);
        gst_object_unref(pad);
    }
    gst_object_unref(h.mux);
}

static void video(Harness &h, int k) {
    GstBuffer *buf = gst_buffer_new_allocate(NULL, 4 * 2 * 4, NULL);
    gst_buffer_memset(buf, 0, k, gst_buffer_get_size(buf));
    GST_BUFFER_PTS(buf) = k * FRAME;
    g_assert_cmpint(gst_pad_push(h.video, buf), ==, GST_FLOW_OK);
}

static void data(Harness &h, int k) {
    GstBuffer *buf = gst_buffer_new_allocate(NULL, sizeof(GstZedSrcMeta), NULL);
    gst_buffer_memset(buf, 0, 0, gst_buffer_get_size(buf));
    GstMapInfo map;
    g_assert_true(gst_buffer_map(buf, &map, GST_MAP_WRITE));
    ((GstZedSrcMeta *) map.data)->frame_id = k;
    gst_buffer_unmap(buf, &map);
    GST_BUFFER_PTS(buf) = k * FRAME;
    g_assert_cmpint(gst_pad_push(h.data, buf), ==, GST_FLOW_OK);
}

// Frames first to last, the video or the data arriving first in turn
static void both_orders(Harness &h, int first, int last, gboolean data_first) {
    for (int k = first; k <= last; ++k) {
        if ((k % 2 == 0) == !data_first) {
            data(h, k);
            video(h, k);
        } else {
            video(h, k);
            data(h, k);
        }
    }
}

static void report(const char *what, int expected) {
    g_print("  %-52s %d/%d muxed\n", what, received, expected);
    g_assert_cmpint(received, ==, expected);
}

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    // plugin_init() is not called: register the debug category so that GST_DEBUG works
    GST_DEBUG_CATEGORY_INIT(gst_zeddatamux_debug, "zeddatamux", 0, "test");
    g_log_set_always_fatal((GLogLevelFlags) (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL));

    {
        // README pipeline: the data branch is not queued and is pushed first
        Harness h = setup();
        data(h, 1);
        data(h, 2);
        video(h, 1);
        data(h, 3);
        video(h, 2);
        video(h, 3);
        report("video branch one frame behind", 3);
        teardown(h);
    }
    {
        Harness h = setup();
        for (int k = 1; k <= 3; ++k)
            video(h, k);
        for (int k = 1; k <= 3; ++k)
            data(h, k);
        report("data branch three frames behind", 3);
        teardown(h);
    }
    {
        // A frame whose metadata never comes is skipped, the next ones still match
        Harness h = setup();
        data(h, 1);
        data(h, 3);
        video(h, 1);
        video(h, 2);
        video(h, 3);
        report("frame without metadata skipped", 2);
        teardown(h);
    }
    {
        // Both orders, then NULL -> PLAYING: the timestamps start again from the beginning
        Harness h = setup();
        both_orders(h, 1, 10, TRUE);
        report("first run, both arrival orders", 10);
        gst_element_set_state(h.mux, GST_STATE_NULL);
        gst_element_set_state(h.mux, GST_STATE_PLAYING);
        start_stream(h);
        both_orders(h, 1, 10, FALSE);
        report("after a restart", 10);
        teardown(h);
    }
    {
        // A flush (seek) also starts the timestamps again
        Harness h = setup();
        both_orders(h, 1, 6, TRUE);
        video(h, 7);
        for (GstPad *src : {h.video, h.data}) {
            gst_pad_push_event(src, gst_event_new_flush_start());
            gst_pad_push_event(src, gst_event_new_flush_stop(TRUE));
        }
        GstSegment segment;
        gst_segment_init(&segment, GST_FORMAT_TIME);
        for (GstPad *src : {h.video, h.data}) {
            gst_pad_push_event(src, gst_event_new_segment(&segment));
        }
        last_pts = GST_CLOCK_TIME_NONE;
        both_orders(h, 1, 6, FALSE);
        report("after a flush", 12);
        teardown(h);
    }
    {
        // The data branch ends first: the queued video frames are still muxed, then one EOS
        Harness h = setup();
        data(h, 1);
        data(h, 2);
        g_assert_true(gst_pad_push_event(h.data, gst_event_new_eos()));
        g_assert_cmpint(eos_received, ==, 0);
        video(h, 1);
        video(h, 2);
        g_assert_true(gst_pad_push_event(h.video, gst_event_new_eos()));
        report("frames queued when the data branch ends", 2);
        g_assert_cmpint(eos_received, ==, 1);
        teardown(h);
    }
    {
        // Once the output takes no buffers anymore, both inputs report it, not only the one that
        // pushed, so that zeddemux and the source can stop
        Harness h = setup();
        downstream = GST_FLOW_EOS;
        data(h, 1);
        GstBuffer *buf = gst_buffer_new_allocate(NULL, 4 * 2 * 4, NULL);
        GST_BUFFER_PTS(buf) = 1 * FRAME;
        g_assert_cmpint(gst_pad_push(h.video, buf), ==, GST_FLOW_EOS);
        buf = gst_buffer_new_allocate(NULL, sizeof(GstZedSrcMeta), NULL);
        gst_buffer_memset(buf, 0, 0, gst_buffer_get_size(buf));
        GST_BUFFER_PTS(buf) = 2 * FRAME;
        const GstFlowReturn ret = gst_pad_push(h.data, buf);
        g_print("  %-52s %s\n", "data input after the output ended", gst_flow_get_name(ret));
        g_assert_cmpint(ret, ==, GST_FLOW_EOS);
        downstream = GST_FLOW_OK;
        teardown(h);
    }
#ifdef MAX_PENDING_DATA
    {
        // The pending buffers are bounded: only the most recent ones wait
        Harness h = setup();
        for (int k = 1; k <= MAX_PENDING_DATA + 4; ++k)
            data(h, k);
        for (int k = 1; k <= MAX_PENDING_DATA + 4; ++k)
            video(h, k);
        report("data far ahead: only the most recent ones kept", MAX_PENDING_DATA);
        g_assert_cmpuint(last_pts, ==, (MAX_PENDING_DATA + 4) * FRAME);
        teardown(h);
    }
    {
        Harness h = setup();
        for (int k = 1; k <= MAX_PENDING_VIDEO + 4; ++k)
            video(h, k);
        for (int k = 1; k <= MAX_PENDING_VIDEO + 4; ++k)
            data(h, k);
        report("video far ahead: only the most recent ones kept", MAX_PENDING_VIDEO);
        teardown(h);
    }
#endif
    g_print("datamux: late frames, restart, flush and end of stream passed\n");
}
