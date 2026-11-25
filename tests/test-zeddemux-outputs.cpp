#define PACKAGE "regression-test"
#define GST_PACKAGE_VERSION "test"
#define GST_PACKAGE_LICENSE "LGPL"
#define GST_PACKAGE_NAME "regression-test"
#define GST_PACKAGE_ORIGIN "local"
#include "gst-zed-demux/gstzeddemux.cpp"

// How each output behaves: linked or not, and what its consumer returns
struct Config {
    GstFlowReturn result;
    gboolean linked;
};

struct Consumer {
    const char *name;
    Config config;
    int received;
    GstPad *pad;
};
static Consumer consumers[3] = {{"left", {GST_FLOW_OK, FALSE}, 0, nullptr},
                                {"aux", {GST_FLOW_OK, FALSE}, 0, nullptr},
                                {"data", {GST_FLOW_OK, FALSE}, 0, nullptr}};

static GstFlowReturn consume(GstPad *pad, GstObject *, GstBuffer *buf) {
    auto *consumer = (Consumer *) gst_pad_get_element_private(pad);
    ++consumer->received;
    gst_buffer_unref(buf);
    return consumer->config.result;
}

static GstBuffer *frame(gboolean with_meta) {
    GstBuffer *buf = gst_buffer_new_allocate(NULL, 4 * 4 * 4, NULL);
    gst_buffer_memset(buf, 0, 0, gst_buffer_get_size(buf));
    GST_BUFFER_PTS(buf) = 0;
    if (with_meta) {
        ZedInfo info = {};
        ZedPose pose = {};
        ZedSensors sens = {};
        gst_buffer_add_zed_src_meta(buf, info, pose, sens, FALSE, 0, NULL, 1);
    }
    return buf;
}

// Pushes one stacked frame through a stereo demux whose outputs behave as configured
static GstFlowReturn run(gboolean stream_data, const Config config[3]) {
    auto *demux =
        GST_ZEDDEMUX(g_object_new(gst_zeddemux_get_type(), "stream-data", stream_data, NULL));
    GstPad *sources[] = {demux->srcpad_left, demux->srcpad_aux, demux->srcpad_data};
    for (int i = 0; i < 3; ++i) {
        Consumer &c = consumers[i];
        c.config = config[i];
        c.received = 0;
        c.pad = gst_pad_new(c.name, GST_PAD_SINK);
        gst_pad_set_element_private(c.pad, &c);
        gst_pad_set_chain_function(c.pad, consume);
        gst_pad_set_active(c.pad, TRUE);
        gst_pad_set_active(sources[i], TRUE);
        if (c.config.linked) {
            g_assert_cmpint(gst_pad_link(sources[i], c.pad), ==, GST_PAD_LINK_OK);
        }
        gst_pad_push_event(sources[i], gst_event_new_stream_start("test"));
    }
    GstCaps *caps = gst_caps_from_string("video/x-raw,format=BGRA,width=4,height=4,framerate=30/1");
    g_assert_true(set_out_caps(demux, caps));
    gst_caps_unref(caps);
    GstSegment segment;
    gst_segment_init(&segment, GST_FORMAT_TIME);
    for (auto *pad : sources) {
        gst_pad_push_event(pad, gst_event_new_segment(&segment));
    }

    const GstFlowReturn ret = gst_zeddemux_chain(demux->sinkpad, GST_OBJECT(demux), frame(TRUE));

    for (int i = 0; i < 3; ++i) {
        if (consumers[i].config.linked) {
            gst_pad_unlink(sources[i], consumers[i].pad);
        }
        gst_pad_set_active(sources[i], FALSE);
        gst_pad_set_active(consumers[i].pad, FALSE);
        gst_object_unref(consumers[i].pad);
    }
    gst_object_unref(demux);
    return ret;
}

static void expect(const char *what, gboolean stream_data, const Config config[3],
                   GstFlowReturn expected_ret, int left, int aux, int data) {
    const GstFlowReturn ret = run(stream_data, config);
    g_print("  %-44s %-11s left %d aux %d data %d\n", what, gst_flow_get_name(ret),
            consumers[0].received, consumers[1].received, consumers[2].received);
    g_assert_cmpstr(gst_flow_get_name(ret), ==, gst_flow_get_name(expected_ret));
    g_assert_cmpint(consumers[0].received, ==, left);
    g_assert_cmpint(consumers[1].received, ==, aux);
    g_assert_cmpint(consumers[2].received, ==, data);
}

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    // plugin_init() is not called: register the debug category so that GST_DEBUG works
    GST_DEBUG_CATEGORY_INIT(gst_zeddemux_debug, "zeddemux", 0, "test");
    g_log_set_always_fatal((GLogLevelFlags) (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL));

    const GstFlowReturn OK = GST_FLOW_OK, EOS = GST_FLOW_EOS, ERR = GST_FLOW_ERROR;
    {
        const Config c[3] = {{OK, TRUE}, {OK, TRUE}, {OK, FALSE}};
        expect("all outputs take the frame", FALSE, c, OK, 1, 1, 0);
    }
    {
        const Config c[3] = {{OK, FALSE}, {OK, TRUE}, {OK, FALSE}};
        expect("left not linked: aux still fed", FALSE, c, OK, 0, 1, 0);
    }
    {
        const Config c[3] = {{EOS, TRUE}, {OK, TRUE}, {OK, FALSE}};
        expect("left at the end: aux still fed", FALSE, c, OK, 1, 1, 0);
    }
    {
        const Config c[3] = {{OK, TRUE}, {OK, TRUE}, {OK, FALSE}};
        expect("data not linked: video still fed", TRUE, c, OK, 1, 1, 0);
    }
    {
        const Config c[3] = {{OK, TRUE}, {OK, TRUE}, {EOS, TRUE}};
        expect("data at the end: video still fed", TRUE, c, OK, 1, 1, 1);
    }
    {
        const Config c[3] = {{OK, FALSE}, {OK, FALSE}, {OK, FALSE}};
        expect("no output linked: not-linked", FALSE, c, GST_FLOW_NOT_LINKED, 0, 0, 0);
    }
    {
        const Config c[3] = {{EOS, TRUE}, {EOS, TRUE}, {EOS, TRUE}};
        expect("all outputs at the end: eos", TRUE, c, EOS, 1, 1, 1);
    }
    {
        const Config c[3] = {{ERR, TRUE}, {OK, TRUE}, {OK, FALSE}};
        expect("error on left: stops at once", FALSE, c, ERR, 1, 0, 0);
    }
    {
        const Config c[3] = {{GST_FLOW_FLUSHING, TRUE}, {OK, TRUE}, {OK, FALSE}};
        expect("left flushing: stops at once", FALSE, c, GST_FLOW_FLUSHING, 1, 0, 0);
    }
    g_print("demux: outputs not linked or at the end of the stream do not stop the others\n");
}
