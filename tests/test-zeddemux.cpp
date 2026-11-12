#define PACKAGE "regression-test"
#define GST_PACKAGE_VERSION "test"
#define GST_PACKAGE_LICENSE "LGPL"
#define GST_PACKAGE_NAME "regression-test"
#define GST_PACKAGE_ORIGIN "local"
#include "gst-zed-demux/gstzeddemux.cpp"

static int received;
static GstFlowReturn consume(GstPad *pad, GstObject *, GstBuffer *buf) {
    ++received;
    const gboolean depth = g_str_equal(GST_PAD_NAME(pad), "depth");
    const gsize stride = depth ? GST_ROUND_UP_4(641 * 2) : 641 * 4;
    g_assert_cmpuint(gst_buffer_get_size(buf), ==, stride * 3);
    if (depth) {
        GstMapInfo map;
        g_assert_true(gst_buffer_map(buf, &map, GST_MAP_READ));
        for (int y = 0; y < 3; ++y) {
            auto *row = (guint16 *) (map.data + y * stride);
            for (int x = 0; x < 641; ++x) {
                g_assert_cmpuint(row[x], ==, y * 641 + x);
            }
        }
        gst_buffer_unmap(buf, &map);
    }
    gst_buffer_unref(buf);
    return GST_FLOW_OK;
}

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    g_log_set_always_fatal((GLogLevelFlags) (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL));
    auto *mux = GST_ZEDDEMUX(g_object_new(gst_zeddemux_get_type(), "is-depth", TRUE, NULL));
    for (const char *spec : {
             "video/x-raw,format=BGRA,width=640,height=960,framerate=30/1",
             "video/x-raw,format=BGRA,width=1920,height=1536,framerate=30/1"}) {
        GstCaps *caps = gst_caps_from_string(spec);
        GstCaps *allowed = gst_pad_get_pad_template_caps(mux->sinkpad);
        g_assert_true(gst_caps_is_subset(caps, allowed));
        gst_caps_unref(allowed);
        gst_caps_unref(caps);
    }
    GstPad *left = gst_pad_new("left", GST_PAD_SINK);
    GstPad *depth = gst_pad_new("depth", GST_PAD_SINK);
    GstPad *sources[] = {mux->srcpad_left, mux->srcpad_aux};
    GstPad *sinks[] = {left, depth};
    for (int i = 0; i < 2; ++i) {
        gst_pad_set_chain_function(sinks[i], consume);
        gst_pad_set_active(sinks[i], TRUE);
        gst_pad_set_active(sources[i], TRUE);
        g_assert_cmpint(gst_pad_link(sources[i], sinks[i]), ==, GST_PAD_LINK_OK);
        gst_pad_push_event(sources[i], gst_event_new_stream_start("test"));
    }
    GstCaps *caps = gst_caps_from_string("video/x-raw,format=BGRA,width=641,height=6,framerate=30/1");
    g_assert_true(set_out_caps(mux, caps));
    gst_caps_unref(caps);
    GstSegment segment;
    gst_segment_init(&segment, GST_FORMAT_TIME);
    for (auto *pad : sources) gst_pad_push_event(pad, gst_event_new_segment(&segment));
    GstBuffer *buf = gst_buffer_new_allocate(NULL, 641 * 6 * 4, NULL);
    GstMapInfo map;
    g_assert_true(gst_buffer_map(buf, &map, GST_MAP_WRITE));
    auto *pixels = (guint32 *) (map.data + 641 * 3 * 4);
    for (int i = 0; i < 641 * 3; ++i) pixels[i] = i;
    gst_buffer_unmap(buf, &map);
    g_assert_cmpint(gst_zeddemux_chain(mux->sinkpad, GST_OBJECT(mux), buf), ==, GST_FLOW_OK);
    g_assert_cmpint(received, ==, 2);
    for (int i = 0; i < 2; ++i) {
        gst_pad_unlink(sources[i], sinks[i]);
        gst_pad_set_active(sources[i], FALSE);
        gst_pad_set_active(sinks[i], FALSE);
        gst_object_unref(sinks[i]);
    }
    gst_object_unref(mux);
    g_print("demux: reduced/HDR caps and odd-width depth rows passed\n");
}
