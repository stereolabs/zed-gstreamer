// Feeds the two inputs of zeddatamux from two threads, as in a pipeline, each one running up to
// MAX_LEAD frames ahead of the other. Build it with -fsanitize=thread to check the locking.
#define PACKAGE "regression-test"
#define GST_PACKAGE_VERSION "test"
#define GST_PACKAGE_LICENSE "LGPL"
#define GST_PACKAGE_NAME "regression-test"
#define GST_PACKAGE_ORIGIN "local"
#include "gst-zed-data-mux/gstzeddatamux.cpp"

#include <atomic>
#include <thread>

static const int FRAMES = 2000;
static const int MAX_LEAD = 3;
static const GstClockTime FRAME = GST_MSECOND;

static int received;
static GstClockTime last_pts = GST_CLOCK_TIME_NONE;

static GstFlowReturn consume(GstPad *, GstObject *, GstBuffer *buf) {
    auto *meta = (GstZedSrcMeta *) gst_buffer_get_meta(buf, GST_ZED_SRC_META_API_TYPE);
    g_assert_nonnull(meta);
    g_assert_cmpuint(meta->frame_id * FRAME, ==, GST_BUFFER_PTS(buf));
    GstMapInfo map;
    g_assert_true(gst_buffer_map(buf, &map, GST_MAP_READ));
    g_assert_cmpuint(map.data[0], ==, meta->frame_id & 0xff);
    gst_buffer_unmap(buf, &map);
    g_assert_true(!GST_CLOCK_TIME_IS_VALID(last_pts) || GST_BUFFER_PTS(buf) > last_pts);
    last_pts = GST_BUFFER_PTS(buf);
    ++received;
    gst_buffer_unref(buf);
    return GST_FLOW_OK;
}

static void start(GstPad *src, const char *caps_str) {
    gst_pad_set_active(src, TRUE);
    gst_pad_push_event(src, gst_event_new_stream_start("test"));
    GstCaps *caps = gst_caps_from_string(caps_str);
    gst_pad_push_event(src, gst_event_new_caps(caps));
    gst_caps_unref(caps);
    GstSegment segment;
    gst_segment_init(&segment, GST_FORMAT_TIME);
    gst_pad_push_event(src, gst_event_new_segment(&segment));
}

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    // plugin_init() is not called: register the debug category so that GST_DEBUG works
    GST_DEBUG_CATEGORY_INIT(gst_zeddatamux_debug, "zeddatamux", 0, "test");
    g_log_set_always_fatal((GLogLevelFlags) (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL));

    GstElement *mux = GST_ELEMENT(g_object_new(gst_zeddatamux_get_type(), NULL));
    GstPad *video = gst_pad_new("video", GST_PAD_SRC);
    GstPad *data = gst_pad_new("data", GST_PAD_SRC);
    GstPad *sink = gst_pad_new("consumer", GST_PAD_SINK);
    gst_pad_set_chain_function(sink, consume);
    gst_pad_set_active(sink, TRUE);
    g_assert_cmpint(gst_pad_link(video, GST_ZEDDATAMUX(mux)->sinkpad_video), ==, GST_PAD_LINK_OK);
    g_assert_cmpint(gst_pad_link(data, GST_ZEDDATAMUX(mux)->sinkpad_data), ==, GST_PAD_LINK_OK);
    g_assert_cmpint(gst_pad_link(GST_ZEDDATAMUX(mux)->srcpad, sink), ==, GST_PAD_LINK_OK);
    g_assert_cmpint(gst_element_set_state(mux, GST_STATE_PLAYING), ==, GST_STATE_CHANGE_SUCCESS);
    start(video, "video/x-raw,format=BGRA,width=4,height=2,framerate=30/1");
    start(data, "application/data");

    std::atomic<int> pushed[2] = {{0}, {0}};
    auto feed = [&](int self) {
        GRand *rand = g_rand_new_with_seed(self + 1);
        for (int k = 1; k <= FRAMES; ++k) {
            while (pushed[1 - self].load() < k - MAX_LEAD) {
                std::this_thread::yield();
            }
            if (g_rand_int_range(rand, 0, 4) == 0) {
                g_usleep(g_rand_int_range(rand, 0, 200));
            }
            GstBuffer *buf;
            if (self == 0) {
                buf = gst_buffer_new_allocate(NULL, 4 * 2 * 4, NULL);
                gst_buffer_memset(buf, 0, k & 0xff, gst_buffer_get_size(buf));
            } else {
                buf = gst_buffer_new_allocate(NULL, sizeof(GstZedSrcMeta), NULL);
                gst_buffer_memset(buf, 0, 0, gst_buffer_get_size(buf));
                GstMapInfo map;
                g_assert_true(gst_buffer_map(buf, &map, GST_MAP_WRITE));
                ((GstZedSrcMeta *) map.data)->frame_id = k;
                gst_buffer_unmap(buf, &map);
            }
            GST_BUFFER_PTS(buf) = k * FRAME;
            g_assert_cmpint(gst_pad_push(self == 0 ? video : data, buf), ==, GST_FLOW_OK);
            pushed[self].store(k);
        }
        g_rand_free(rand);
    };
    std::thread video_thread(feed, 0);
    std::thread data_thread(feed, 1);
    video_thread.join();
    data_thread.join();

    g_print("  %d/%d frames muxed from two threads\n", received, FRAMES);
    g_assert_cmpint(received, ==, FRAMES);

    gst_element_set_state(mux, GST_STATE_NULL);
    for (GstPad *pad : {video, data, sink}) {
        gst_pad_set_active(pad, FALSE);
        gst_object_unref(pad);
    }
    gst_object_unref(mux);
    g_print("datamux: two streaming threads passed\n");
}
