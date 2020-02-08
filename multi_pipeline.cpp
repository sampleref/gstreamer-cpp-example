//
// Created by kantipud on 01-04-2019.
//

#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include <stdlib.h>
#include <execinfo.h>
#include <signal.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <iostream>

using namespace std;

const std::string BASE_RECORDING_PATH = "/home/kantipud/videos/";
const std::string DEFAULT_RTSP_URL = "rtsp://10.142.138.174/z3-1.mp4";
GstElement *pipeline;
GstClock *clock_gst;
GstElement *pipeline2;
int current_file_index = 0;


std::string prepare_next_file_name(string key);

const char *gst_stream_status_string(GstStreamStatusType status) {
    switch (status) {
        case GST_STREAM_STATUS_TYPE_CREATE:
            return "CREATE";
        case GST_STREAM_STATUS_TYPE_ENTER:
            return "ENTER";
        case GST_STREAM_STATUS_TYPE_LEAVE:
            return "LEAVE";
        case GST_STREAM_STATUS_TYPE_DESTROY:
            return "DESTROY";
        case GST_STREAM_STATUS_TYPE_START:
            return "START";
        case GST_STREAM_STATUS_TYPE_PAUSE:
            return "PAUSE";
        case GST_STREAM_STATUS_TYPE_STOP:
            return "STOP";
        default:
            return "UNKNOWN";
    }
}

static GstPadProbeReturn
cb_have_data(GstPad *pad,
             GstPadProbeInfo *info,
             char *user_data) {
    g_print("************* ||| *********** cb_have_data for %s\n", user_data);
    return GST_PAD_PROBE_OK;
}

static void start_recording_video(std::string file_path, GstElement *pipe1) {
    g_print("start_recording_video file to %s\n", file_path.c_str());

    int ret;
    gchar *tmp;
    GstElement *tee, *queue, *h264parse, *mp4mux, *filesink;
    GstPad *srcpad, *sinkpad;

    //Create queue
    tmp = g_strdup_printf("queue-%s", "recorder");
    queue = gst_element_factory_make("queue", tmp);
    g_free(tmp);

    //Create h264parse
    tmp = g_strdup_printf("h264parse-%s", "recorder");
    h264parse = gst_element_factory_make("h264parse", tmp);
    g_object_set(h264parse, "config-interval", 1, NULL);
    g_free(tmp);

    //Create mp4mux
    tmp = g_strdup_printf("mp4mux-%s", "recorder");
    mp4mux = gst_element_factory_make("mp4mux", tmp);
    g_free(tmp);

    //Create filesink
    tmp = g_strdup_printf("filesink-%s", "recorder");
    filesink = gst_element_factory_make("filesink", tmp);
    g_object_set(filesink, "location", file_path.c_str(), NULL);
    g_free(tmp);

    //Add elements to pipeline
    gst_bin_add_many(GST_BIN (pipe1), queue, h264parse, mp4mux, filesink, NULL);

    //Link queue -> h264parse
    srcpad = gst_element_get_static_pad(queue, "src");
    g_assert_nonnull (srcpad);
    sinkpad = gst_element_get_static_pad(h264parse, "sink");
    g_assert_nonnull (sinkpad);
    ret = gst_pad_link(srcpad, sinkpad);
    g_assert_cmpint (ret, ==, GST_PAD_LINK_OK);
    gst_object_unref(srcpad);
    gst_object_unref(sinkpad);

    //Link h264parse -> mp4mux
    srcpad = gst_element_get_static_pad(h264parse, "src");
    g_assert_nonnull (srcpad);
    sinkpad = gst_element_get_request_pad(mp4mux, "video_%u");
    g_assert_nonnull (sinkpad);
    ret = gst_pad_link(srcpad, sinkpad);
    g_assert_cmpint (ret, ==, GST_PAD_LINK_OK);
    gst_object_unref(srcpad);
    gst_object_unref(sinkpad);

    //Link mp4mux -> filesink
    srcpad = gst_element_get_static_pad(mp4mux, "src");
    g_assert_nonnull (srcpad);
    sinkpad = gst_element_get_static_pad(filesink, "sink");
    g_assert_nonnull (sinkpad);
    ret = gst_pad_link(srcpad, sinkpad);
    g_assert_cmpint (ret, ==, GST_PAD_LINK_OK);
    gst_object_unref(srcpad);
    gst_object_unref(sinkpad);

    //Link videotee -> queue
    tee = gst_bin_get_by_name(GST_BIN (pipe1), "videotee");
    g_assert_nonnull (tee);
    srcpad = gst_element_get_request_pad(tee, "src_%u");
    g_assert_nonnull (srcpad);
    gst_object_unref(tee);
    sinkpad = gst_element_get_static_pad(queue, "sink");
    g_assert_nonnull (sinkpad);
    ret = gst_pad_link(srcpad, sinkpad);
    g_assert_cmpint (ret, ==, GST_PAD_LINK_OK);
    gst_object_unref(srcpad);
    gst_object_unref(sinkpad);

    g_assert_nonnull (filesink);

    ret = gst_element_sync_state_with_parent(queue);
    g_assert_true (ret);
    ret = gst_element_sync_state_with_parent(h264parse);
    g_assert_true (ret);
    ret = gst_element_sync_state_with_parent(mp4mux);
    g_assert_true (ret);
    ret = gst_element_sync_state_with_parent(filesink);
    g_assert_true (ret);

    g_print("Recording file to %s\n", file_path.c_str());
}

static gboolean pipeline_bus_callback(GstBus *bus, GstMessage *message, gchar *data) {
    switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug;
            gst_message_parse_error(message, &err, &debug);
            g_print("pipeline_bus_callback %s:GST_MESSAGE_ERROR Error/code : %s/%d\n", data, err->message, err->code);
            if (err->code > 0) {
                g_error_free(err);
                g_free(debug);
                return FALSE;
            }
            g_error_free(err);
            g_free(debug);
            break;
        }
        case GST_MESSAGE_EOS: {
            g_print("pipeline_bus_callback:GST_MESSAGE_EOS %s \n", data);
            return FALSE;
        }
        case GST_MESSAGE_STATE_CHANGED: {
            GstState old_state, new_state;
            gst_message_parse_state_changed(message, &old_state, &new_state, NULL);
            g_print("pipeline_bus_callback:GST_MESSAGE_STATE_CHANGED- Element %s changed state from %s to %s. for %s \n",
                    GST_OBJECT_NAME(message->src),
                    gst_element_state_get_name(old_state),
                    gst_element_state_get_name(new_state), data);
            break;
        }
        case GST_MESSAGE_STREAM_STATUS: {
            GstStreamStatusType statusType;
            gst_message_parse_stream_status(message, &statusType, NULL);
            g_print("pipeline_bus_callback:GST_MESSAGE_STREAM_STATUS- %s. for %s \n",
                    gst_stream_status_string(statusType), data);
            break;
        }
        default: {
            g_print("pipeline_bus_callback:default Got %s message for %s \n", GST_MESSAGE_TYPE_NAME (message), data);
            break;
        }
    }
    return TRUE;
}

gboolean start_proxy_sink() {

    g_print("===============< start_proxy_sink - Proxy sink starting for  %s \n", "pipeline_1");

    int ret;
    gchar *tmp;
    GstElement *tee, *queue, *proxysink;
    GstPad *srcpad, *sinkpad;

    //Create queue
    tmp = g_strdup_printf("queue-%s", "proxysink");
    queue = gst_element_factory_make("queue", tmp);
    //g_object_set(queue, "leaky", 2, NULL);
    g_free(tmp);

    //Create proxy sink
    tmp = g_strdup_printf("%s", "psink");
    proxysink = gst_element_factory_make("proxysink", tmp);
    g_free(tmp);

    //Add elements to pipeline
    gst_bin_add_many(GST_BIN (pipeline), proxysink, NULL);

    //Link queue -> proxysink
    /*srcpad = gst_element_get_static_pad(queue, "src");
    g_assert_nonnull (srcpad);
    sinkpad = gst_element_get_static_pad(proxysink, "sink");
    gst_pad_add_probe(sinkpad, GST_PAD_PROBE_TYPE_BUFFER,
                      (GstPadProbeCallback) cb_have_data, g_strdup_printf("%s", "proxysink - sink"), NULL);
    g_assert_nonnull (sinkpad);
    ret = gst_pad_link(srcpad, sinkpad);
    g_assert_cmpint (ret, ==, GST_PAD_LINK_OK);
    gst_object_unref(srcpad);
    gst_object_unref(sinkpad);*/

    //Sync state to parent
    /*ret = gst_element_sync_state_with_parent(queue);
    g_assert_true (ret);*/
    ret = gst_element_sync_state_with_parent(proxysink);
    g_assert_true (ret);

    //Link videotee -> queue
    tee = gst_bin_get_by_name(GST_BIN (pipeline), "videotee");
    g_assert_nonnull (tee);
    srcpad = gst_element_get_request_pad(tee, "src_%u");
    g_assert_nonnull (srcpad);
    gst_object_unref(tee);
    sinkpad = gst_element_get_static_pad(proxysink, "sink");
    g_assert_nonnull (sinkpad);
    gst_pad_add_probe(sinkpad, GST_PAD_PROBE_TYPE_BUFFER,
                      (GstPadProbeCallback) cb_have_data, g_strdup_printf("%s", "proxysink - sink"), NULL);
    ret = gst_pad_link(srcpad, sinkpad);
    g_assert_cmpint (ret, ==, GST_PAD_LINK_OK);
    gst_object_unref(srcpad);
    gst_object_unref(sinkpad);

    g_assert_nonnull (proxysink);

    g_print("===============> start_proxy_sink - Proxy sink linked for  %s \n", "pipeline_1");

}

gboolean start_streaming(gchar *name) {
    GstStateChangeReturn ret;
    GError *error = NULL;
    GstBus *bus;

    std::string pipeline_string = "";
    bool value = true;
    value = false;
    if (value) {
        pipeline_string =
                std::string("tee name=videotee allow-not-linked=TRUE ! queue leaky=2 ! proxysink name=psink ") +
                string("rtspsrc name=rtspsource location=" + DEFAULT_RTSP_URL +
                       " latency=100 drop-on-latency=TRUE ! watchdog timeout=5000 ! rtph264depay name=rtspdepay ! videotee. ");
    } else {
        pipeline_string = std::string("tee name=videotee allow-not-linked=TRUE ! queue ! fakesink ") +
                          string("rtspsrc name=rtspsource location=" + DEFAULT_RTSP_URL +
                                 " latency=100 drop-on-latency=TRUE ! watchdog timeout=5000 ! rtph264depay name=rtspdepay ! videotee. ");
    }

    pipeline = gst_parse_launch(pipeline_string.c_str(), &error);

    if (error) {
        g_printerr("Failed to parse launch: %s\n", error->message);
        g_error_free(error);
        goto err;
    }

    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_enable_sync_message_emission(bus);
    gst_bus_set_sync_handler(bus, (GstBusSyncHandler) pipeline_bus_callback, name, NULL);
    gst_object_unref(GST_OBJECT(bus));

    clock_gst = gst_system_clock_obtain();
    gst_element_set_base_time(pipeline, 0);
    gst_pipeline_use_clock(GST_PIPELINE (pipeline), clock_gst);

    //start_proxy_sink();
    start_recording_video(prepare_next_file_name("main_pipeline"), pipeline);

    g_print("Starting pipeline, not transmitting yet\n");
    ret = gst_element_set_state(GST_ELEMENT (pipeline), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
        goto err;

    g_print("Started pipeline, try adding peers \n");
    return TRUE;

    err:
    g_print("State change failure\n");
    if (pipeline)
        g_clear_object (&pipeline);
    return FALSE;
}

void handler(int sig) {
    void *array[10];
    size_t size;

    // get void*'s for all entries on the stack
    size = backtrace(array, 10);

    // print out all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}

static int generate_random_int(void) {
    srand(time(0));
    return rand();
}

std::string prepare_next_file_name(string key) {
    std::string file_name;
    file_name = key;
    file_name.insert(0, BASE_RECORDING_PATH);
    file_name.append("-" + std::to_string(++current_file_index) + "__");
    file_name.append(std::to_string(generate_random_int()));
    file_name.append(".mp4");
    g_print("New recording file name generated as: %s\n", file_name.c_str());
    return file_name;
}

gboolean start_second_pipeline_recording(gchar *name) {
    GstStateChangeReturn ret;
    GError *error = NULL;
    GstBus *bus;
    GstElement *proxysrc, *proxysink;

    std::string pipeline_string = "";
    pipeline_string =
            string("proxysrc name=proxysrc ! queue leaky=2 ! h264parse config-interval=1 ! mp4mux ! filesink location=\"") +
            prepare_next_file_name("test_multipipeline").c_str() + string("\"");

    pipeline2 = gst_parse_launch(pipeline_string.c_str(), &error);

    if (error) {
        g_printerr("Failed to parse launch: %s\n", error->message);
        g_error_free(error);
        goto err;
    }

    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline2));
    gst_bus_enable_sync_message_emission(bus);
    gst_bus_set_sync_handler(bus, (GstBusSyncHandler) pipeline_bus_callback, name, NULL);
    gst_object_unref(GST_OBJECT(bus));

    proxysrc = gst_bin_get_by_name(GST_BIN (pipeline2), "proxysrc");
    g_assert_nonnull (proxysrc);
    proxysink = gst_bin_get_by_name(GST_BIN (pipeline), "psink");
    g_assert_nonnull (proxysink);

    // Connect the two pipelines
    g_object_set(proxysrc, "proxysink", proxysink, NULL);

    //gst_element_set_start_time(pipeline2, GST_CLOCK_TIME_NONE);
    gst_pipeline_use_clock(GST_PIPELINE (pipeline2), clock_gst);
    gst_element_set_base_time(pipeline2, gst_element_get_base_time(pipeline));

    g_print("start_second_pipeline_recording - Starting pipeline2...\n");
    ret = gst_element_set_state(GST_ELEMENT (pipeline2), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
        goto err;

    g_print("start_second_pipeline_recording - Started pipeline2 \n");

    GstPad *pad;
    g_assert_nonnull (proxysink);
    pad = gst_element_get_static_pad(proxysrc, "src");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
                      (GstPadProbeCallback) cb_have_data, g_strdup_printf("%s", "pipeline_2"), NULL);
    gst_object_unref(pad);

    return TRUE;

    err:
    g_print("start_second_pipeline_recording - State change failure\n");
    if (pipeline2)
        g_clear_object (&pipeline2);
    return FALSE;
}

int
main(int argc, char *argv[]) {
    signal(SIGSEGV, handler);
    GOptionContext *context;
    GError *error = NULL;

    context = g_option_context_new("- gstreamer pipeline -> pipeline demo");

    g_option_context_add_group(context, gst_init_get_option_group());
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("Error initializing: %s\n", error->message);
        return -1;
    }

    start_streaming(g_strdup_printf("%s", "pipeline_1"));
    while (true) {
        cout << "Please enter anything key to start: \n";
        std::string input;
        getline(cin, input);
        if (input == "") {
            continue;
        }
        if (input == "!") {
            break;
        }
        if (input == "psink") {
            start_proxy_sink();
            continue;
        } else {
            start_second_pipeline_recording(g_strdup_printf("%s", "pipeline_2"));
            cout << "Please wait... \n";
        }
    }
    return 0;
}