//
// Created by sampleref on 11-02-2020.
//
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/audio/audio.h>
#include <gst/base/base.h>
#include <stdlib.h>

#define RELEASE_ELEMENT(x) if(x) {gst_object_unref(x); x = NULL;}

/*
 * Number of bytes received in the chain list function when using buffer lists
 */
static guint chain_list_bytes_received;

#define LOOP_COUNT 1

/*
 * RTP pipeline structure to store the required elements.
 */
typedef struct
{
    GstElement *pipeline;
    GstElement *appsrc;
    GstElement *rtppay;
    GstElement *rtpdepay;
    GstElement *fakesink;
    const guint8 *frame_data;
    int frame_data_size;
    int frame_count;
    GstEvent *custom_event;
} rtp_pipeline;

/*
 * Creates a RTP pipeline for one test.
 * @param frame_data Pointer to the frame data which is used to pass through pay/depayloaders.
 * @param frame_data_size Frame data size in bytes.
 * @param frame_count Frame count.
 * @param filtercaps Caps filters.
 * @param pay Payloader name.
 * @param depay Depayloader name.
 * @return
 * Returns pointer to the RTP pipeline.
 * The user must free the RTP pipeline when it's not used anymore.
 */
static rtp_pipeline *
rtp_pipeline_create (const guint8 * frame_data, int frame_data_size,
                     int frame_count, const char *filtercaps, const char *pay, const char *depay)
{
    gchar *pipeline_name;
    rtp_pipeline *p;
    GstCaps *caps;

    /* Check parameters. */
    if (!frame_data || !pay || !depay) {
        return NULL;
    }

    /* Allocate memory for the RTP pipeline. */
    p = (rtp_pipeline *) malloc (sizeof (rtp_pipeline));

    p->frame_data = frame_data;
    p->frame_data_size = frame_data_size;
    p->frame_count = frame_count;
    p->custom_event = NULL;

    /* Create elements. */
    pipeline_name = g_strdup_printf ("%s-%s-pipeline", pay, depay);
    p->pipeline = gst_pipeline_new (pipeline_name);
    g_free (pipeline_name);
    p->appsrc = gst_element_factory_make ("appsrc", NULL);
    p->rtppay = gst_element_factory_make (pay, NULL);
    p->rtpdepay = gst_element_factory_make (depay, NULL);
    p->fakesink = gst_element_factory_make ("fakesink", NULL);

    /* One or more elements are not created successfully or failed to create p? */
    if (!p->pipeline || !p->appsrc || !p->rtppay || !p->rtpdepay || !p->fakesink) {
        /* Release created elements. */
        RELEASE_ELEMENT (p->pipeline);
        RELEASE_ELEMENT (p->appsrc);
        RELEASE_ELEMENT (p->rtppay);
        RELEASE_ELEMENT (p->rtpdepay);
        RELEASE_ELEMENT (p->fakesink);

        /* Release allocated memory. */
        free (p);

        return NULL;
    }

    /* Set src properties. */
    caps = gst_caps_from_string (filtercaps);
    g_object_set (p->appsrc, "do-timestamp", TRUE, "caps", caps,
                  "format", GST_FORMAT_TIME, NULL);
    gst_caps_unref (caps);

    /* Add elements to the pipeline. */
    gst_bin_add (GST_BIN (p->pipeline), p->appsrc);
    gst_bin_add (GST_BIN (p->pipeline), p->rtppay);
    gst_bin_add (GST_BIN (p->pipeline), p->rtpdepay);
    gst_bin_add (GST_BIN (p->pipeline), p->fakesink);

    /* Link elements. */
    gst_element_link (p->appsrc, p->rtppay);
    gst_element_link (p->rtppay, p->rtpdepay);
    gst_element_link (p->rtpdepay, p->fakesink);

    return p;
}

/*
 * Chain list function for testing buffer lists
 */
static GstFlowReturn
rtp_pipeline_chain_list (GstPad * pad, GstObject * parent, GstBufferList * list)
{
    guint i, len;

    fail_if (!list);
    /*
     * Count the size of the payload in the buffer list.
     */
    len = gst_buffer_list_length (list);
    GST_LOG ("list length %u", len);

    /* Loop through all buffers */
    for (i = 0; i < len; i++) {
        GstBuffer *paybuf;
        GstMemory *mem;
        gint size;

        paybuf = gst_buffer_list_get (list, i);
        /* only count real data which is expected in last memory block */
        GST_LOG ("n_memory %d", gst_buffer_n_memory (paybuf));
                fail_unless (gst_buffer_n_memory (paybuf) > 1);
        mem = gst_buffer_get_memory_range (paybuf, gst_buffer_n_memory (paybuf) - 1,
                                           1);
        size = gst_memory_get_sizes (mem, NULL, NULL);
        gst_memory_unref (mem);
        chain_list_bytes_received += size;
        GST_LOG ("size %d, total %u", size, chain_list_bytes_received);
    }
    gst_buffer_list_unref (list);

    return GST_FLOW_OK;
}

static GstFlowReturn
rtp_pipeline_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);

/*
 * Enables buffer lists and adds a chain_list_function to the depayloader.
 * @param p Pointer to the RTP pipeline.
 */
static void
rtp_pipeline_enable_lists (rtp_pipeline * p)
{
    GstPad *pad;

    /* Add chain list function for the buffer list tests */
    pad = gst_element_get_static_pad (p->rtpdepay, "sink");
    gst_pad_set_chain_list_function (pad,
                                     GST_DEBUG_FUNCPTR (rtp_pipeline_chain_list));
    /* .. to satisfy this silly test code in case someone dares push a buffer */
    gst_pad_set_chain_function (pad, GST_DEBUG_FUNCPTR (rtp_pipeline_chain));
    gst_object_unref (pad);
}

static GstFlowReturn
rtp_pipeline_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
    GstBufferList *list;

    list = gst_buffer_list_new_sized (1);
    gst_buffer_list_add (list, buf);
    return rtp_pipeline_chain_list (pad, parent, list);
}

static GstPadProbeReturn
pay_event_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
    rtp_pipeline *p = (rtp_pipeline *) user_data;
    GstEvent *event = GST_EVENT_CAST(GST_PAD_PROBE_INFO_DATA (info));

    if (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_DOWNSTREAM) {
        const GstStructure *s0 = gst_event_get_structure (p->custom_event);
        const GstStructure *s1 = gst_event_get_structure (event);
        if (gst_structure_is_equal (s0, s1)) {
            return GST_PAD_PROBE_DROP;
        }
    }

    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
depay_event_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
    rtp_pipeline *p = (rtp_pipeline *) user_data;
    GstEvent *event = GST_EVENT_CAST(GST_PAD_PROBE_INFO_DATA (info));

    if (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_DOWNSTREAM) {
        const GstStructure *s0 = gst_event_get_structure (p->custom_event);
        const GstStructure *s1 = gst_event_get_structure (event);
        if (gst_structure_is_equal (s0, s1)) {
            gst_event_unref (p->custom_event);
            p->custom_event = NULL;
        }
    }

    return GST_PAD_PROBE_OK;
}

/*
 * RTP bus callback.
 */
static gboolean
rtp_bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
    GMainLoop *mainloop = (GMainLoop *) data;

    switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_ERROR:
        {
            GError *err;

            gchar *debug;

            gchar *element_name;

            element_name = (message->src) ? gst_object_get_name (message->src) : NULL;
            gst_message_parse_error (message, &err, &debug);
            g_print ("\nError from element %s: %s\n%s\n\n",
                     GST_STR_NULL (element_name), err->message, (debug) ? debug : "");
            g_error_free (err);
            g_free (debug);
            g_free (element_name);

            fail_if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR);

            g_main_loop_quit (mainloop);
        }
            break;

        case GST_MESSAGE_EOS:
        {
            g_main_loop_quit (mainloop);
        }
            break;
            break;

        default:
        {
        }
            break;
    }

    return TRUE;
}

/*
 * Runs the RTP pipeline.
 * @param p Pointer to the RTP pipeline.
 */
static void
rtp_pipeline_run (rtp_pipeline * p)
{
    GstFlowReturn flow_ret;
    GMainLoop *mainloop = NULL;
    GstBus *bus;
    gint i, j;

    /* Check parameters. */
    if (p == NULL) {
        return;
    }

    /* Create mainloop. */
    mainloop = g_main_loop_new (NULL, FALSE);
    if (!mainloop) {
        return;
    }

    /* Add bus callback. */
    bus = gst_pipeline_get_bus (GST_PIPELINE (p->pipeline));

    gst_bus_add_watch (bus, rtp_bus_callback, (gpointer) mainloop);

    /* Set pipeline to PLAYING. */
    gst_element_set_state (p->pipeline, GST_STATE_PLAYING);

    /* Push custom event into the pipeline */
    if (p->custom_event) {
        GstPad *srcpad;

        /* Install a probe to drop the event after it being serialized */
        srcpad = gst_element_get_static_pad (p->rtppay, "src");
        gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                           pay_event_probe_cb, p, NULL);
        gst_object_unref (srcpad);

        /* Install a probe to trace the deserialized event after depayloading */
        srcpad = gst_element_get_static_pad (p->rtpdepay, "src");
        gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                           depay_event_probe_cb, p, NULL);
        gst_object_unref (srcpad);
        /* Send the event */
        gst_element_send_event (p->appsrc, gst_event_ref (p->custom_event));
    }

    /* Push data into the pipeline */
    for (i = 0; i < LOOP_COUNT; i++) {
        const guint8 *data = p->frame_data;

        for (j = 0; j < p->frame_count; j++) {
            GstBuffer *buf;

            buf =
                    gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
                                                 (guint8 *) data, p->frame_data_size, 0, p->frame_data_size, NULL,
                                                 NULL);

            g_signal_emit_by_name (p->appsrc, "push-buffer", buf, &flow_ret);
            fail_unless_equals_int (flow_ret, GST_FLOW_OK);
            data += p->frame_data_size;

            gst_buffer_unref (buf);
        }
    }

    g_signal_emit_by_name (p->appsrc, "end-of-stream", &flow_ret);

    /* Run mainloop. */
    g_main_loop_run (mainloop);

    /* Set pipeline to NULL. */
    gst_element_set_state (p->pipeline, GST_STATE_NULL);

    /* Release mainloop. */
    g_main_loop_unref (mainloop);

    gst_bus_remove_watch (bus);
    gst_object_unref (bus);

    fail_if (p->custom_event);
}

/*
 * Destroys the RTP pipeline.
 * @param p Pointer to the RTP pipeline.
 */
static void
rtp_pipeline_destroy (rtp_pipeline * p)
{
    /* Check parameters. */
    if (p == NULL) {
        return;
    }

    /* Release pipeline. */
    RELEASE_ELEMENT (p->pipeline);

    /* Release allocated memory. */
    free (p);
}


static void
rtp_pipeline_test (const guint8 * frame_data, int frame_data_size,
                   int frame_count, const char *filtercaps, const char *pay, const char *depay,
                   guint bytes_sent, guint mtu_size, gboolean use_lists)
{
    /* Create RTP pipeline. */
    rtp_pipeline *p =
            rtp_pipeline_create (frame_data, frame_data_size, frame_count, filtercaps,
                                 pay, depay);

    if (p == NULL) {
        return;
    }

    /* set mtu size if needed */
    if (mtu_size > 0) {
        g_object_set (p->rtppay, "mtu", mtu_size, NULL);
    }

    if (use_lists) {
        rtp_pipeline_enable_lists (p);
        chain_list_bytes_received = 0;
    }

    /* Run RTP pipeline. */
    rtp_pipeline_run (p);

    /* Destroy RTP pipeline. */
    rtp_pipeline_destroy (p);

    if (use_lists) {
        /* 'next NAL' indicator is 4 bytes */
        fail_unless_equals_int (chain_list_bytes_received, bytes_sent * LOOP_COUNT);
    }
}

/* KLV data from Day_Flight.mpg */
static const guint8 rtp_KLV_frame_data[] = {
        0x06, 0x0e, 0x2b, 0x34, 0x02, 0x0b, 0x01, 0x01,
        0x0e, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0x00,
        0x81, 0x91, 0x02, 0x08, 0x00, 0x04, 0x6c, 0x8e,
        0x20, 0x03, 0x83, 0x85, 0x41, 0x01, 0x01, 0x05,
        0x02, 0x3d, 0x3b, 0x06, 0x02, 0x15, 0x80, 0x07,
        0x02, 0x01, 0x52, 0x0b, 0x03, 0x45, 0x4f, 0x4e,
        0x0c, 0x0e, 0x47, 0x65, 0x6f, 0x64, 0x65, 0x74,
        0x69, 0x63, 0x20, 0x57, 0x47, 0x53, 0x38, 0x34,
        0x0d, 0x04, 0x4d, 0xc4, 0xdc, 0xbb, 0x0e, 0x04,
        0xb1, 0xa8, 0x6c, 0xfe, 0x0f, 0x02, 0x1f, 0x4a,
        0x10, 0x02, 0x00, 0x85, 0x11, 0x02, 0x00, 0x4b,
        0x12, 0x04, 0x20, 0xc8, 0xd2, 0x7d, 0x13, 0x04,
        0xfc, 0xdd, 0x02, 0xd8, 0x14, 0x04, 0xfe, 0xb8,
        0xcb, 0x61, 0x15, 0x04, 0x00, 0x8f, 0x3e, 0x61,
        0x16, 0x04, 0x00, 0x00, 0x01, 0xc9, 0x17, 0x04,
        0x4d, 0xdd, 0x8c, 0x2a, 0x18, 0x04, 0xb1, 0xbe,
        0x9e, 0xf4, 0x19, 0x02, 0x0b, 0x85, 0x28, 0x04,
        0x4d, 0xdd, 0x8c, 0x2a, 0x29, 0x04, 0xb1, 0xbe,
        0x9e, 0xf4, 0x2a, 0x02, 0x0b, 0x85, 0x38, 0x01,
        0x2e, 0x39, 0x04, 0x00, 0x8d, 0xd4, 0x29, 0x01,
        0x02, 0x1c, 0x5f
};

GST_START_TEST (rtp_klv)
    {
        rtp_pipeline_test (rtp_KLV_frame_data, G_N_ELEMENTS (rtp_KLV_frame_data), 1,
                           "meta/x-klv, parsed=(bool)true", "rtpklvpay", "rtpklvdepay", 0, 0, FALSE);
    }

GST_END_TEST;


static Suite *
rtp_payloading_suite (void) {
    GstRegistry *registry = gst_registry_get();
    Suite *s = suite_create("rtp_data_test");

    TCase *tc_chain = tcase_create("linear");

    /* Set timeout to 60 seconds. */
    tcase_set_timeout(tc_chain, 60);

    suite_add_tcase(s, tc_chain);
    tcase_add_test (tc_chain, rtp_klv);
}

GST_CHECK_MAIN (rtp_payloading)
