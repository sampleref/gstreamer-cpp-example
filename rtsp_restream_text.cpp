//
// Created by sampleref on 08-02-2020.
//

#include <gst/gst.h>

#include <gst/rtsp-server/rtsp-server.h>
#include <string>
#include <glib-2.0/glib/gstring.h>
#include <cstring>
#include <iostream>

#define DEFAULT_RTSP_PORT "8554"

static char *port = (char *) DEFAULT_RTSP_PORT;

static GOptionEntry entries[] = {
        {"port", 'p', 0, G_OPTION_ARG_STRING, &port,
                "Port to listen on (default: " DEFAULT_RTSP_PORT ")", "PORT"},
        {NULL}
};

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

typedef struct {
    gboolean white;
    GstClockTime timestamp;
} MyContext;

/* called when we need to give data to appsrc */
static void
need_data(GstElement *appsrc, guint unused, MyContext *ctx) {
    g_print("read data started\r\n");
    GstFlowReturn flow_ret;
    for (int j = 0; j < 100000; j++) {
        gsize size;
        guint8 *data;
        GBytes *bytes = g_string_free_to_bytes(g_string_new("hello world!"));
        data = (guint8 *) g_bytes_get_data(bytes, &size);

        GstBuffer *buf;

        /*buf = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY,
                                          data, size, 0, size, NULL,
                                          NULL);*/
        buf = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY,
                                          (guint8 *) rtp_KLV_frame_data, G_N_ELEMENTS (rtp_KLV_frame_data), 0,
                                          G_N_ELEMENTS (rtp_KLV_frame_data), NULL,
                                          NULL);

        g_signal_emit_by_name(appsrc, "push-buffer", buf, &flow_ret);
        if (flow_ret != GST_FLOW_OK) {
            g_printerr("Error in push buffer \n");
        }

        gst_buffer_unref(buf);
    }
    //g_signal_emit_by_name(appsrc, "end-of-stream", &flow_ret);
    g_print("read data stopped\r\n");
}


static void
media_configure_cb(GstRTSPMediaFactory *factory, GstRTSPMedia *media) {
    GstElement *element, *appsrc;
    MyContext *ctx;

    /* get the element used for providing the streams of the media */
    element = gst_rtsp_media_get_element(media);

    /* get our appsrc, we named it 'mysrc' with the name property */
    appsrc = gst_bin_get_by_name_recurse_up(GST_BIN (element), "mysrc");
    //gst_element_set_clock(appsrc, gst_rtsp_media_get_clock(media));

    /* this instructs appsrc that we will be dealing with timed buffer */
    g_object_set(appsrc, "do-timestamp", FALSE, "format", GST_FORMAT_TIME, "is-live", TRUE, NULL);
    /* configure the caps of the video */
    g_object_set(G_OBJECT (appsrc), "caps", gst_caps_new_simple("meta/x-klv", "parsed", G_TYPE_BOOLEAN, TRUE, nullptr),
                 NULL);

    /* install the callback that will be called when a buffer is needed */
    g_signal_connect (appsrc, "need-data", (GCallback) need_data, ctx);
    gst_object_unref(appsrc);
    gst_object_unref(element);
}

int
main(int argc, char *argv[]) {
    GMainLoop *loop;
    GstRTSPServer *server;
    GstRTSPMountPoints *mounts;
    GstRTSPMediaFactory *factory;
    GOptionContext *optctx;
    GError *error = NULL;
    gchar *str;

    optctx = g_option_context_new("<filename.mp4> - Test RTSP Server, MP4");
    g_option_context_add_main_entries(optctx, entries, NULL);
    g_option_context_add_group(optctx, gst_init_get_option_group());
    if (!g_option_context_parse(optctx, &argc, &argv, &error)) {
        g_printerr("Error parsing options: %s\n", error->message);
        g_option_context_free(optctx);
        g_clear_error(&error);
        return -1;
    }

    if (argc < 2) {
        g_print("%s\n", g_option_context_get_help(optctx, TRUE, NULL));
        return 1;
    }
    g_option_context_free(optctx);

    loop = g_main_loop_new(NULL, FALSE);

    /* create a server instance */
    server = gst_rtsp_server_new();
    g_object_set(server, "service", port, NULL);

    /* get the mount points for this server, every server has a default object
     * that be used to map uri mount points to media factories */
    mounts = gst_rtsp_server_get_mount_points(server);

    str = g_strdup_printf("( appsrc name=mysrc ! rtpklvpay name=pay0 pt=98 )");

    /* make a media factory for a test stream. The default media factory can use
     * gst-launch syntax to create pipelines.
     * any launch line works as long as it contains elements named pay%d. Each
     * element with pay%d names will be a stream */
    factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory, str);
    g_signal_connect (factory, "media-configure", (GCallback) media_configure_cb,
                      factory);
    g_free(str);

    /* attach the test factory to the /test url */
    gst_rtsp_mount_points_add_factory(mounts, "/test", factory);

    /* don't need the ref to the mapper anymore */
    g_object_unref(mounts);

    /* attach the server to the default maincontext */
    gst_rtsp_server_attach(server, NULL);

    /* start serving */
    g_print("stream ready at rtsp://127.0.0.1:%s/test\n", port);
    g_main_loop_run(loop);

    return 0;
}

