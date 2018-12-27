/*
 * Demo gstreamer app for negotiating and streaming a sendrecv webrtc stream
 * with a browser JS app.
 *
 *
 */
#include <gst/gst.h>
#include <gst/sdp/sdp.h>

#define GST_USE_UNSTABLE_API

#include <gst/webrtc/webrtc.h>

/* For signalling */
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup-websocket.h>

/* For application */
#include <string.h>
#include <stdio.h>
#include <map>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <execinfo.h>
#include <signal.h>
#include <time.h>
#include <string>
#include <regex>
#include <iostream>
#include <list>
#include <thread>

using namespace std;

//CONSTANTS
const std::string LEVEL_ASYMMETRY_ALLOWED = ";level-asymmetry-allowed=1";
const std::string PROFILE_LEVEL_ID_REGEX = "profile-level-id=*[A-za-z0-9]*;";
const std::string H264_BROWSER_PROFILE_LEVEL_ID = "profile-level-id=42e01f;";
const bool CHANGE_PROFILE_LEVEL_ID = true;

const std::string BASE_RECORDING_PATH = "/home/kantipud/videos/";
const std::string SIGNAL_SERVER = "wss://127.0.0.1:8443";

#define STUN_SERVER " stun-server=stun://stun.l.google.com:19302 "
#define RTP_CAPS_OPUS "application/x-rtp,media=audio,encoding-name=OPUS,payload="
#define RTP_CAPS_VP8 "application/x-rtp,media=video,encoding-name=VP8,payload="
#define RTP_CAPS_H264 "application/x-rtp,media=video,encoding-name=H264,payload=96"


enum AppState {
    APP_STATE_UNKNOWN = 0,
    APP_STATE_ERROR = 1, /* generic error */
    SERVER_CONNECTING = 1000,
    SERVER_CONNECTION_ERROR,
    SERVER_CONNECTED, /* Ready to register */
    SERVER_REGISTERING = 2000,
    SERVER_REGISTRATION_ERROR,
    SERVER_REGISTERED, /* Ready to call a peer */
    SERVER_CLOSED, /* server connection closed by us or the server */
    PEER_CONNECTING = 3000,
    PEER_CONNECTION_ERROR,
    PEER_CONNECTED,
    PEER_CALL_NEGOTIATING = 4000,
    PEER_CALL_STARTED,
    PEER_CALL_STOPPING,
    PEER_CALL_STOPPED,
    PEER_CALL_ERROR,
};

class WebrtcViewer {

public:
    //Attributes
    GstElement *pipeline;
    GstElement *webrtc1;
    GMainLoop *loop;
    GObject *send_channel, *receive_channel;
    SoupSession *session;
    SoupWebsocketConnection *ws_conn = NULL;
    enum AppState app_state = APP_STATE_UNKNOWN;
    int pipeline_execution_id;
    std::string peer_id;
    std::string server_url = SIGNAL_SERVER.c_str();
    gboolean disable_ssl = FALSE;

    //Methods
    gboolean start_webrtcbin(void);

    void remove_peer_from_pipeline(void);

    void close_peer_from_server(void);

    gboolean setup_call(void);

    void connect_to_websocket_server_async(void);

    void remove_webrtc_peer_from_pipelinehandler_map();
};

typedef std::shared_ptr<WebrtcViewer> WebrtcViewerPtr;

class RtspPipelineHandler {

public:
    //Attributes
    GstElement *pipeline;
    std::string device_id;
    std::string rtsp_url;
    int pipeline_execution_id;
    int current_file_index = 0;
    std::map<std::string, WebrtcViewerPtr> peers; //Connected webrtc peers with key as remote peer id

    //Methods
    gboolean start_streaming(void);

    gboolean stop_streaming(void);

    std::string prepare_next_file_name(void);

};

typedef std::shared_ptr<RtspPipelineHandler> RtspPipelineHandlerPtr;

static std::map<int, RtspPipelineHandlerPtr> pipelineHandlers;

void WebrtcViewer::remove_webrtc_peer_from_pipelinehandler_map() {
    auto it_pipeline = pipelineHandlers.find(pipeline_execution_id);
    if (it_pipeline != pipelineHandlers.end()) {
        auto it_peer = it_pipeline->second->peers.find(peer_id);
        if (it_peer != it_pipeline->second->peers.end()) {
            it_pipeline->second->peers.erase(it_peer);
            g_print("Deleted webrtc peer from map for peer %s\n", peer_id.c_str());
        }
    }
}

gboolean cleanup_and_quit_loop(const gchar *msg, enum AppState state, WebrtcViewer *webrtcViewer) {
    if (msg)
        g_printerr("%s\n", msg);
    if (state > 0)
        webrtcViewer->app_state = state;

    if (webrtcViewer->ws_conn) {
        if (soup_websocket_connection_get_state(webrtcViewer->ws_conn) ==
            SOUP_WEBSOCKET_STATE_OPEN)
            /* This will call us again */
            soup_websocket_connection_close(webrtcViewer->ws_conn, 1000, "");
        else
            g_object_unref(webrtcViewer->ws_conn);
    }

    /* To allow usage as a GSourceFunc */
    return G_SOURCE_REMOVE;
}

static gchar *
get_string_from_json_object(JsonObject *object) {
    JsonNode *root;
    JsonGenerator *generator;
    gchar *text;

    /* Make it the root node */
    root = json_node_init_object(json_node_alloc(), object);
    generator = json_generator_new();
    json_generator_set_root(generator, root);
    text = json_generator_to_data(generator, NULL);

    /* Release everything */
    g_object_unref(generator);
    json_node_free(root);
    return text;
}

static void
handle_media_stream(GstPad *pad, GstElement *pipe, const char *convert_name,
                    const char *sink_name) {
    GstPad *qpad;
    GstElement *q, *conv, *resample, *sink;
    GstPadLinkReturn ret;

    g_print("Trying to handle stream with %s ! %s", convert_name, sink_name);

    q = gst_element_factory_make("queue", NULL);
    g_assert_nonnull (q);
    conv = gst_element_factory_make(convert_name, NULL);
    g_assert_nonnull (conv);
    sink = gst_element_factory_make(sink_name, NULL);
    g_assert_nonnull (sink);

    if (g_strcmp0(convert_name, "audioconvert") == 0) {
        /* Might also need to resample, so add it just in case.
         * Will be a no-op if it's not required. */
        resample = gst_element_factory_make("audioresample", NULL);
        g_assert_nonnull (resample);
        gst_bin_add_many(GST_BIN (pipe), q, conv, resample, sink, NULL);
        gst_element_sync_state_with_parent(q);
        gst_element_sync_state_with_parent(conv);
        gst_element_sync_state_with_parent(resample);
        gst_element_sync_state_with_parent(sink);
        gst_element_link_many(q, conv, resample, sink, NULL);
    } else {
        gst_bin_add_many(GST_BIN (pipe), q, conv, sink, NULL);
        gst_element_sync_state_with_parent(q);
        gst_element_sync_state_with_parent(conv);
        gst_element_sync_state_with_parent(sink);
        gst_element_link_many(q, conv, sink, NULL);
    }

    qpad = gst_element_get_static_pad(q, "sink");

    ret = gst_pad_link(pad, qpad);
    g_assert_cmphex (ret, ==, GST_PAD_LINK_OK);
}

static void
on_incoming_decodebin_stream(GstElement *decodebin, GstPad *pad,
                             GstElement *pipe) {
    GstCaps *caps;
    const gchar *name;

    if (!gst_pad_has_current_caps(pad)) {
        g_printerr("Pad '%s' has no caps, can't do anything, ignoring\n",
                   GST_PAD_NAME (pad));
        return;
    }

    caps = gst_pad_get_current_caps(pad);
    name = gst_structure_get_name(gst_caps_get_structure(caps, 0));

    if (g_str_has_prefix(name, "video")) {
        handle_media_stream(pad, pipe, "videoconvert", "fakesink");
    } else if (g_str_has_prefix(name, "audio")) {
        handle_media_stream(pad, pipe, "audioconvert", "fakesink");
    } else {
        g_printerr("Unknown pad %s, ignoring", GST_PAD_NAME (pad));
    }
}

static void
on_incoming_stream(GstElement *webrtc, GstPad *pad, GstElement *pipe) {
    GstElement *decodebin;

    if (GST_PAD_DIRECTION (pad) != GST_PAD_SRC)
        return;

    decodebin = gst_element_factory_make("decodebin", NULL);
    g_signal_connect (decodebin, "pad-added",
                      G_CALLBACK(on_incoming_decodebin_stream), pipe);
    gst_bin_add(GST_BIN (pipe), decodebin);
    gst_element_sync_state_with_parent(decodebin);
    gst_element_link(webrtc, decodebin);
}

void static send_ice_candidate_message(GstElement *webrtc G_GNUC_UNUSED, guint mlineindex,
                                       gchar *candidate, WebrtcViewer *user_data G_GNUC_UNUSED) {
    gchar *text;
    JsonObject *ice, *msg;

    if (user_data->app_state < PEER_CALL_NEGOTIATING) {
        cleanup_and_quit_loop("Can't send ICE, not in call", APP_STATE_ERROR, user_data);
        return;
    }

    ice = json_object_new();
    json_object_set_string_member(ice, "candidate", candidate);
    json_object_set_int_member(ice, "sdpMLineIndex", mlineindex);
    msg = json_object_new();
    json_object_set_object_member(msg, "ice", ice);
    text = get_string_from_json_object(msg);
    json_object_unref(msg);

    soup_websocket_connection_send_text(user_data->ws_conn, text);
    g_free(text);
}

void static send_sdp_offer(GstWebRTCSessionDescription *offer, WebrtcViewer *webrtcViewer) {
    gchar *text;
    JsonObject *msg, *sdp;

    if (webrtcViewer->app_state < PEER_CALL_NEGOTIATING) {
        cleanup_and_quit_loop("Can't send offer, not in call", APP_STATE_ERROR, webrtcViewer);
        return;
    }

    text = gst_sdp_message_as_text(offer->sdp);
    g_print("Sending offer:\n%s\n", text);

    sdp = json_object_new();
    json_object_set_string_member(sdp, "type", "offer");
    json_object_set_string_member(sdp, "sdp", text);
    g_free(text);

    msg = json_object_new();
    json_object_set_object_member(msg, "sdp", sdp);
    text = get_string_from_json_object(msg);
    json_object_unref(msg);

    soup_websocket_connection_send_text(webrtcViewer->ws_conn, text);
    g_free(text);
}

/* Offer created by our pipeline, to be sent to the peer */
static void on_offer_created(GstPromise *promise, gpointer user_data) {
    GstWebRTCSessionDescription *offer = NULL;
    const GstStructure *reply;
    WebrtcViewer *webrtcViewer = static_cast<WebrtcViewer *>(user_data);
    g_assert_cmphex (webrtcViewer->app_state, ==, PEER_CALL_NEGOTIATING);

    g_assert_cmphex (gst_promise_wait(promise), ==, GST_PROMISE_RESULT_REPLIED);
    reply = gst_promise_get_reply(promise);
    gst_structure_get(reply, "offer",
                      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
    gst_promise_unref(promise);


    if (CHANGE_PROFILE_LEVEL_ID) {
        const gchar *text_fmtp = gst_sdp_media_get_attribute_val(
                (GstSDPMedia *) &g_array_index(offer->sdp->medias, GstSDPMedia, 0), "fmtp");
        if (strstr(text_fmtp, "profile-level-id") != NULL) {
            g_print("Found source fmtp attribute as:  %s\n", text_fmtp);
            std::string delimiter = ";";
            std::string fmtp_attr(text_fmtp);
            fmtp_attr.append(LEVEL_ASYMMETRY_ALLOWED);
            //Replacing profile-level-id
            fmtp_attr = std::regex_replace(fmtp_attr, std::regex(PROFILE_LEVEL_ID_REGEX),
                                           H264_BROWSER_PROFILE_LEVEL_ID);
            printf("Updated fmtp attribute as: %s\n", fmtp_attr.c_str());

            guint attr_len = gst_sdp_media_attributes_len(
                    (GstSDPMedia *) &g_array_index(offer->sdp->medias, GstSDPMedia, 0));
            printf("Attributes Length: %d\n", attr_len);
            guint fmtp_index;
            for (guint index = 0; index < attr_len; index++) {
                const GstSDPAttribute *gstSDPAttribute = gst_sdp_media_get_attribute(
                        (GstSDPMedia *) &g_array_index(offer->sdp->medias, GstSDPMedia, 0), index);
                const gchar *attr_val = gstSDPAttribute->value;
                if (attr_val != NULL && strstr(attr_val, "profile-level-id") != NULL) {
                    printf("Found fmtp attribute at index: %d\n", index);
                    fmtp_index = index;
                }
            }
            if (fmtp_index > 0) {
                printf("Replacing fmtp attribute at index: %d \n", fmtp_index);
                gst_sdp_media_remove_attribute((GstSDPMedia *) &g_array_index(offer->sdp->medias, GstSDPMedia, 0),
                                               fmtp_index);
                gst_sdp_media_add_attribute((GstSDPMedia *) &g_array_index(offer->sdp->medias, GstSDPMedia, 0), "fmtp",
                                            fmtp_attr.c_str());
                //Frame rate hard code - Disabled
                /*gst_sdp_media_add_attribute((GstSDPMedia *) &g_array_index(offer->sdp->medias, GstSDPMedia, 0), "framerate", "29.985014985014985");*/
            }
        }
    }


    promise = gst_promise_new();
    g_signal_emit_by_name(webrtcViewer->webrtc1, "set-local-description", offer, promise);
    gst_promise_interrupt(promise);
    gst_promise_unref(promise);

    /* Send offer to peer */
    send_sdp_offer(offer, webrtcViewer);
    gst_webrtc_session_description_free(offer);
}

void static on_negotiation_needed(GstElement *element, WebrtcViewer *user_data) {
    GstPromise *promise;
    WebrtcViewer *webrtcViewer = static_cast<WebrtcViewer *>(user_data);
    webrtcViewer->app_state = PEER_CALL_NEGOTIATING;
    promise = gst_promise_new_with_change_func(on_offer_created, webrtcViewer, NULL);;
    g_signal_emit_by_name(webrtcViewer->webrtc1, "create-offer", NULL, promise);
}

void data_channel_on_error(GObject *dc, gpointer user_data) {
    cleanup_and_quit_loop("Data channel error", APP_STATE_UNKNOWN, static_cast<WebrtcViewer *>(user_data));
}

static void
data_channel_on_open(GObject *dc, gpointer user_data) {
    GBytes *bytes = g_bytes_new("data", strlen("data"));
    g_print("data channel opened\n");
    g_signal_emit_by_name(dc, "send-string", "Hi! from GStreamer");
    g_signal_emit_by_name(dc, "send-data", bytes);
    g_bytes_unref(bytes);
}

void data_channel_on_close(GObject *dc, gpointer user_data) {
    cleanup_and_quit_loop("Data channel closed", APP_STATE_UNKNOWN, static_cast<WebrtcViewer *>(user_data));
}

static void
data_channel_on_message_string(GObject *dc, gchar *str, gpointer user_data) {
    g_print("Received data channel message: %s\n", str);
}

static void
connect_data_channel_signals(GObject *data_channel) {
    g_signal_connect (data_channel, "on-error", G_CALLBACK(data_channel_on_error),
                      NULL);
    g_signal_connect (data_channel, "on-open", G_CALLBACK(data_channel_on_open),
                      NULL);
    g_signal_connect (data_channel, "on-close", G_CALLBACK(data_channel_on_close),
                      NULL);
    g_signal_connect (data_channel, "on-message-string", G_CALLBACK(data_channel_on_message_string),
                      NULL);
}

static void
on_data_channel(GstElement *webrtc, GObject *data_channel, gpointer user_data) {
    connect_data_channel_signals(data_channel);
    static_cast<WebrtcViewer *>(user_data)->receive_channel = data_channel;
}

void WebrtcViewer::close_peer_from_server(void) {
    g_print("Closing peer connection from server for: %s\n", peer_id.c_str());
    if (session) {
        g_print("Closing session for peer: %s\n", peer_id.c_str());
        //soup_session_abort(session);
        g_object_unref(session);
    }
    std::string message = "Pipeline closed due to source disconnection, please retry and connect again";
    if (ws_conn) {
        g_print("Closing websocket connection for peer: %s\n", peer_id.c_str());
        if (soup_websocket_connection_get_state(ws_conn) == SOUP_WEBSOCKET_STATE_OPEN)
            soup_websocket_connection_close(ws_conn, SOUP_WEBSOCKET_CLOSE_BAD_DATA, message.c_str());
        g_object_unref(ws_conn);
    }
    remove_peer_from_pipeline();
    g_print("Closed peer connection from server for: %s\n", peer_id.c_str());
}

void WebrtcViewer::remove_peer_from_pipeline(void) {
    gchar *tmp;
    GstPad *srcpad, *sinkpad;
    GstElement *webrtc, *rtph264pay, *queue, *tee;

    if (webrtc1) {
        gst_object_unref(webrtc1);
    }

    webrtc = gst_bin_get_by_name(GST_BIN (pipeline), this->peer_id.c_str());
    if (webrtc) {
        g_print("Removing existing webrtcbin for remote peer %s \n", this->peer_id.c_str());
        gst_element_set_state(webrtc, GST_STATE_NULL);
        gst_bin_remove(GST_BIN (pipeline), webrtc);
        gst_object_unref(webrtc);
    }

    tmp = g_strdup_printf("rtph264pay-%s", this->peer_id.c_str());
    rtph264pay = gst_bin_get_by_name(GST_BIN (pipeline), tmp);
    g_free(tmp);
    if (rtph264pay) {
        gst_element_set_state(rtph264pay, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(pipeline), rtph264pay);
        gst_object_unref(rtph264pay);
    }

    tmp = g_strdup_printf("queue-%s", this->peer_id.c_str());
    queue = gst_bin_get_by_name(GST_BIN (pipeline), tmp);
    g_free(tmp);

    if (queue) {
        gst_element_set_state(queue, GST_STATE_NULL);

        sinkpad = gst_element_get_static_pad(queue, "sink");
        g_assert_nonnull (sinkpad);
        srcpad = gst_pad_get_peer(sinkpad);
        g_assert_nonnull (srcpad);
        gst_object_unref(sinkpad);

        gst_bin_remove(GST_BIN (pipeline), queue);
        gst_object_unref(queue);

        tee = gst_bin_get_by_name(GST_BIN (pipeline), "videotee");
        if (tee) {
            g_assert_nonnull (tee);
            gst_element_release_request_pad(tee, srcpad);
            gst_object_unref(srcpad);
            gst_object_unref(tee);
        }
    }

    if (loop) {
        g_main_quit(this->loop);
    }

    if (send_channel)
        g_object_unref(send_channel);

    g_print("Removed webrtcbin peer for remote peer : %s\n", this->peer_id.c_str());
    remove_webrtc_peer_from_pipelinehandler_map();
}

gboolean WebrtcViewer::start_webrtcbin(void) {

    GstWebRTCRTPTransceiver *trans;
    GArray *transceivers;

    int ret;
    gchar *tmp;
    GstElement *tee, *queue, *rtph264pay;
    GstCaps *caps;
    GstPad *srcpad, *sinkpad;

    //Create queue
    tmp = g_strdup_printf("queue-%s", this->peer_id.c_str());
    queue = gst_element_factory_make("queue", tmp);
    g_free(tmp);

    //Create rtph264depay with caps
    tmp = g_strdup_printf("rtph264pay-%s", this->peer_id.c_str());
    rtph264pay = gst_element_factory_make("rtph264pay", tmp);
    g_object_set(rtph264pay, "config-interval", 1, NULL);
    g_free(tmp);
    srcpad = gst_element_get_static_pad(rtph264pay, "src");
    caps = gst_caps_from_string(RTP_CAPS_H264);
    gst_pad_set_caps(srcpad, caps);
    gst_caps_unref(caps);
    gst_object_unref(srcpad);

    //Create webrtcbin
    this->webrtc1 = gst_element_factory_make("webrtcbin", this->peer_id.c_str());

    //Add elements to pipeline
    gst_bin_add_many(GST_BIN (pipeline), queue, rtph264pay, this->webrtc1, NULL);

    //Link queue -> rtph264depay
    srcpad = gst_element_get_static_pad(queue, "src");
    g_assert_nonnull (srcpad);
    sinkpad = gst_element_get_static_pad(rtph264pay, "sink");
    g_assert_nonnull (sinkpad);
    ret = gst_pad_link(srcpad, sinkpad);
    g_assert_cmpint (ret, ==, GST_PAD_LINK_OK);
    gst_object_unref(srcpad);
    gst_object_unref(sinkpad);

    //Link rtph264depay -> webrtcbin
    srcpad = gst_element_get_static_pad(rtph264pay, "src");
    g_assert_nonnull (srcpad);
    sinkpad = gst_element_get_request_pad(this->webrtc1, "sink_%u");
    g_assert_nonnull (sinkpad);
    ret = gst_pad_link(srcpad, sinkpad);
    g_assert_cmpint (ret, ==, GST_PAD_LINK_OK);
    gst_object_unref(srcpad);
    gst_object_unref(sinkpad);

    //Link videotee -> queue
    tee = gst_bin_get_by_name(GST_BIN (pipeline), "videotee");
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

    g_assert_nonnull (webrtc1);

    /* This is the gstwebrtc entry point where we create the offer and so on. It
     * will be called when the pipeline goes to PLAYING. */
    g_signal_connect (webrtc1, "on-negotiation-needed",
                      G_CALLBACK(on_negotiation_needed), this);
    /* We need to transmit this ICE candidate to the browser via the websockets
     * signalling server. Incoming ice candidates from the browser need to be
     * added by us too, see on_server_message() */
    g_signal_connect (webrtc1, "on-ice-candidate",
                      G_CALLBACK(send_ice_candidate_message), this);

    //Change webrtcbin to send only
    g_signal_emit_by_name(webrtc1, "get-transceivers", &transceivers);
    if (transceivers != NULL) {
        g_print("Changing webrtcbin to sendonly...\n");
        trans = g_array_index (transceivers, GstWebRTCRTPTransceiver *, 0);
        trans->direction = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
        g_object_unref(trans);
        g_array_unref(transceivers);
    }

    g_signal_emit_by_name(webrtc1, "create-data-channel", "channel", NULL,
                          &send_channel);


    if (send_channel) {
        g_print("Created data channel\n");
        connect_data_channel_signals(send_channel);
    } else {
        g_print("Could not create data channel, is usrsctp available?\n");
    }

    g_signal_connect (webrtc1, "on-data-channel", G_CALLBACK(on_data_channel),
                      this);
    /* Incoming streams will be exposed via this signal */
    g_signal_connect (webrtc1, "pad-added", G_CALLBACK(on_incoming_stream),
                      pipeline);

    g_print("Created webrtc bin for peer %s\n", this->peer_id.c_str());

    /* Set to pipeline branch to PLAYING */
    ret = gst_element_sync_state_with_parent(queue);
    g_assert_true (ret);
    ret = gst_element_sync_state_with_parent(rtph264pay);
    g_assert_true (ret);
    ret = gst_element_sync_state_with_parent(webrtc1);
    g_assert_true (ret);

    return TRUE;

    err:
    if (webrtc1)
        webrtc1 = NULL;
    return FALSE;
}

gboolean WebrtcViewer::setup_call(void) {
    gchar *msg;

    if (soup_websocket_connection_get_state(this->ws_conn) != SOUP_WEBSOCKET_STATE_OPEN) {
        g_print("Websocket connection is not in state SOUP_WEBSOCKET_STATE_OPEN \n");
        return FALSE;
    }

    if (this->peer_id == "") {
        g_print("WebrtcViewer::setup_call peer id is blank\n");
        return FALSE;
    }

    g_print("Setting up signalling server call with %s\n", this->peer_id.c_str());
    app_state = PEER_CONNECTING;
    msg = g_strdup_printf("SESSION %s", this->peer_id.c_str());
    soup_websocket_connection_send_text(ws_conn, msg);
    g_free(msg);
    return TRUE;
}

static gboolean
register_with_server(WebrtcViewer *webrtcViewer) {
    gchar *hello;
    gint32 our_id;

    if (soup_websocket_connection_get_state(webrtcViewer->ws_conn) !=
        SOUP_WEBSOCKET_STATE_OPEN)
        return FALSE;

    our_id = g_random_int_range(10, 10000);
    g_print("Registering id %i with server\n", our_id);
    webrtcViewer->app_state = SERVER_REGISTERING;

    /* Register with the server with a random integer id. Reply will be received
     * by on_server_message() */
    hello = g_strdup_printf("HELLO %i", our_id);
    soup_websocket_connection_send_text(webrtcViewer->ws_conn, hello);
    g_free(hello);

    return TRUE;
}

static void
on_server_closed(SoupWebsocketConnection *conn G_GNUC_UNUSED,
                 gpointer user_data G_GNUC_UNUSED) {
    static_cast<WebrtcViewer *>(user_data)->app_state = SERVER_CLOSED;
    cleanup_and_quit_loop("Server connection closed", APP_STATE_UNKNOWN, static_cast<WebrtcViewer *>(user_data));
    static_cast<WebrtcViewer *>(user_data)->remove_peer_from_pipeline();
}

/* One mega message handler for our asynchronous calling mechanism */
static void
on_server_message(SoupWebsocketConnection *conn, SoupWebsocketDataType type,
                  GBytes *message, gpointer user_data) {
    gsize size;
    gchar *text, *data;
    WebrtcViewer *webrtcViewer = static_cast<WebrtcViewer *>(user_data);
    switch (type) {
        case SOUP_WEBSOCKET_DATA_BINARY:
            g_printerr("Received unknown binary message, ignoring\n");
            return;
        case SOUP_WEBSOCKET_DATA_TEXT: {
            gsize size;
            data = static_cast<gchar *>(g_bytes_unref_to_data(message, &size));
            /* Convert to NULL-terminated string */
            text = g_strndup(data, size);
            g_free(data);
            break;
        }
        default:
            g_assert_not_reached ();
    }

    /* Server has accepted our registration, we are ready to send commands */
    if (g_strcmp0(text, "HELLO") == 0) {
        if (webrtcViewer->app_state != SERVER_REGISTERING) {
            cleanup_and_quit_loop("ERROR: Received HELLO when not registering",
                                  APP_STATE_ERROR, webrtcViewer);
            goto out;
        }
        webrtcViewer->app_state = SERVER_REGISTERED;
        g_print("Registered with server\n");
        /* Ask signalling server to connect us with a specific peer */
        if (!webrtcViewer->setup_call()) {
            cleanup_and_quit_loop("ERROR: Failed to setup call", PEER_CALL_ERROR, webrtcViewer);
            goto out;
        }
        /* Call has been setup by the server, now we can start negotiation */
    } else if (g_strcmp0(text, "SESSION_OK") == 0) {
        if (webrtcViewer->app_state != PEER_CONNECTING) {
            cleanup_and_quit_loop("ERROR: Received SESSION_OK when not calling",
                                  PEER_CONNECTION_ERROR, webrtcViewer);
            goto out;
        }

        webrtcViewer->app_state = PEER_CONNECTED;
        /* Start negotiation (exchange SDP and ICE candidates) */
        if (!webrtcViewer->start_webrtcbin())
            cleanup_and_quit_loop("ERROR: failed to start pipeline",
                                  PEER_CALL_ERROR, webrtcViewer);
        /* Handle errors */
    } else if (g_str_has_prefix(text, "ERROR")) {
        switch (webrtcViewer->app_state) {
            case SERVER_CONNECTING:
                webrtcViewer->app_state = SERVER_CONNECTION_ERROR;
                break;
            case SERVER_REGISTERING:
                webrtcViewer->app_state = SERVER_REGISTRATION_ERROR;
                break;
            case PEER_CONNECTING:
                webrtcViewer->app_state = PEER_CONNECTION_ERROR;
                break;
            case PEER_CONNECTED:
            case PEER_CALL_NEGOTIATING:
                webrtcViewer->app_state = PEER_CALL_ERROR;
            default:
                webrtcViewer->app_state = APP_STATE_ERROR;
        }
        cleanup_and_quit_loop(text, APP_STATE_UNKNOWN, webrtcViewer);
        /* Look for JSON messages containing SDP and ICE candidates */
    } else {
        JsonNode *root;
        JsonObject *object, *child;
        JsonParser *parser = json_parser_new();
        if (!json_parser_load_from_data(parser, text, -1, NULL)) {
            g_printerr("Unknown message '%s', ignoring", text);
            g_object_unref(parser);
            goto out;
        }

        root = json_parser_get_root(parser);
        if (!JSON_NODE_HOLDS_OBJECT (root)) {
            g_printerr("Unknown json message '%s', ignoring", text);
            g_object_unref(parser);
            goto out;
        }

        object = json_node_get_object(root);
        /* Check type of JSON message */
        if (json_object_has_member(object, "sdp")) {
            int ret;
            GstSDPMessage *sdp;
            const gchar *text, *sdptype;
            GstWebRTCSessionDescription *answer;

            g_assert_cmphex (webrtcViewer->app_state, ==, PEER_CALL_NEGOTIATING);

            child = json_object_get_object_member(object, "sdp");

            if (!json_object_has_member(child, "type")) {
                cleanup_and_quit_loop("ERROR: received SDP without 'type'",
                                      PEER_CALL_ERROR, webrtcViewer);
                goto out;
            }

            sdptype = json_object_get_string_member(child, "type");
            /* In this example, we always create the offer and receive one answer.
             * See tests/examples/webrtcbidirectional.c in gst-plugins-bad for how to
             * handle offers from peers and reply with answers using webrtcbin. */
            g_assert_cmpstr (sdptype, ==, "answer");

            text = json_object_get_string_member(child, "sdp");

            g_print("Received answer:\n%s\n", text);

            ret = gst_sdp_message_new(&sdp);
            g_assert_cmphex (ret, ==, GST_SDP_OK);

            ret = gst_sdp_message_parse_buffer((guint8 *) text, strlen(text), sdp);
            g_assert_cmphex (ret, ==, GST_SDP_OK);

            answer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER,
                                                        sdp);
            g_assert_nonnull (answer);

            /* Set remote description on our pipeline */
            {
                GstPromise *promise = gst_promise_new();
                g_signal_emit_by_name(webrtcViewer->webrtc1, "set-remote-description", answer,
                                      promise);
                gst_promise_interrupt(promise);
                gst_promise_unref(promise);
            }

            webrtcViewer->app_state = PEER_CALL_STARTED;
        } else if (json_object_has_member(object, "ice")) {
            const gchar *candidate;
            gint sdpmlineindex;

            child = json_object_get_object_member(object, "ice");
            candidate = json_object_get_string_member(child, "candidate");
            sdpmlineindex = json_object_get_int_member(child, "sdpMLineIndex");

            /* Add ice candidate sent by remote peer */
            g_signal_emit_by_name(webrtcViewer->webrtc1, "add-ice-candidate", sdpmlineindex,
                                  candidate);
        } else {
            g_printerr("Ignoring unknown JSON message:\n%s\n", text);
        }
        g_object_unref(parser);
    }

    out:
    g_free(text);
}

static void
on_server_connected(SoupSession *session, GAsyncResult *res,
                    WebrtcViewer *webrtcViewer) {
    GError *error = NULL;

    webrtcViewer->ws_conn = soup_session_websocket_connect_finish(session, res, &error);
    if (error) {
        cleanup_and_quit_loop(error->message, SERVER_CONNECTION_ERROR, webrtcViewer);
        g_error_free(error);
        return;
    }

    g_assert_nonnull (webrtcViewer->ws_conn);

    webrtcViewer->app_state = SERVER_CONNECTED;
    g_print("Connected to signalling server\n");

    g_signal_connect (webrtcViewer->ws_conn, "closed", G_CALLBACK(on_server_closed), webrtcViewer);
    g_signal_connect (webrtcViewer->ws_conn, "message", G_CALLBACK(on_server_message), webrtcViewer);

    /* Register with the server so it knows about us and can accept commands */
    register_with_server(webrtcViewer);
}

/*
 * Connect to the signalling server. This is the entrypoint for everything else.
 */
void WebrtcViewer::connect_to_websocket_server_async(void) {
    SoupLogger *logger;
    SoupMessage *message;
    const char *https_aliases[] = {"wss", NULL};

    session = soup_session_new_with_options(SOUP_SESSION_SSL_STRICT, !disable_ssl,
                                            SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
            //SOUP_SESSION_SSL_CA_FILE, "/etc/ssl/certs/ca-bundle.crt",
                                            SOUP_SESSION_HTTPS_ALIASES, https_aliases, NULL);

    logger = soup_logger_new(SOUP_LOGGER_LOG_BODY, -1);
    soup_session_add_feature(session, SOUP_SESSION_FEATURE (logger));
    g_object_unref(logger);

    message = soup_message_new(SOUP_METHOD_GET, server_url.c_str());

    g_print("Connecting to server...\n");

    /* Once connected, we will register */
    soup_session_websocket_connect_async(session, message, NULL, NULL, NULL,
                                         (GAsyncReadyCallback) on_server_connected, this);
    app_state = SERVER_CONNECTING;
}

static gboolean
check_plugins(void) {
    int i;
    gboolean ret;
    GstPlugin *plugin;
    GstRegistry *registry;
    const gchar *needed[] = {"opus", "vpx", "nice", "webrtc", "dtls", "srtp",
                             "rtpmanager", "videotestsrc", "audiotestsrc", NULL};

    registry = gst_registry_get();
    ret = TRUE;
    for (i = 0; i < g_strv_length((gchar **) needed); i++) {
        plugin = gst_registry_find_plugin(registry, needed[i]);
        if (!plugin) {
            g_print("Required gstreamer plugin '%s' not found\n", needed[i]);
            ret = FALSE;
            continue;
        }
        gst_object_unref(plugin);
    }
    return ret;
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

class WebRTC_Launch_Task {
public:
    void execute(WebrtcViewerPtr webrtcViewer) {
        GMainContext *async_context = g_main_context_new();
        GMainLoop *loop = g_main_loop_new(async_context, FALSE);
        g_main_context_push_thread_default(async_context);
        webrtcViewer->disable_ssl = TRUE;
        webrtcViewer->loop = loop;
        g_print("WebRTC_Launch_Task:execute Creating webrtc bin for remote peer %s\n", webrtcViewer->peer_id.c_str());
        webrtcViewer->connect_to_websocket_server_async();
        g_main_loop_run(loop);
        g_main_context_pop_thread_default(async_context);
        //g_main_loop_unref(loop); // As already quit is issued on this loop, no need to unref
        g_main_context_unref(async_context);
        g_print("WebRTC_Launch_Task:execute Exited for remote peer %s\n", webrtcViewer->peer_id.c_str());
    }

};

static int generate_random_int(void) {
    srand(time(0));
    return rand();
}

std::string RtspPipelineHandler::prepare_next_file_name(void) {
    std::string file_name;
    if (device_id == "") {
        g_print("Using default device_name %s\n", "test_device");
        device_id.assign("test_device");
    }
    file_name = device_id;
    file_name.insert(0, BASE_RECORDING_PATH);
    file_name.append("__" + std::to_string(pipeline_execution_id) + "-" + std::to_string(++current_file_index) + "__");
    file_name.append(std::to_string(generate_random_int()));
    file_name.append(".mp4");
    g_print("New recording file name generated as: %s\n", file_name.c_str());
    return file_name;
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

static gboolean check_rtsp_socket() {
    g_print("Checking rtsp connection \n");
    int CreateSocket = 0, n = 0;
    char dataReceived[1024];
    struct sockaddr_in ipOfServer;

    memset(dataReceived, '0', sizeof(dataReceived));

    if ((CreateSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Socket not created \n");
        return 1;
    }

    ipOfServer.sin_family = AF_INET;
    ipOfServer.sin_port = htons(8554);
    ipOfServer.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(CreateSocket, (struct sockaddr *) &ipOfServer, sizeof(ipOfServer)) < 0) {
        printf("Connection failed \n");
        return false;
    }
    g_print("Checking rtsp connection - success \n");
    return true;
}

static void pause_play_pipeline(gpointer data) {
    RtspPipelineHandler *pipelineHandler = static_cast<RtspPipelineHandler *>(data);

    if (pipelineHandler->stop_streaming()) {
        GstStateChangeReturn ret = gst_element_set_state(pipelineHandler->pipeline, GST_STATE_NULL);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            g_printerr("pause_play_pipeline: Unable to set the pipeline to the NULL state.\n");
        }
    }
    while (!check_rtsp_socket()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    pipelineHandler->start_streaming();
}

static gboolean pipeline_bus_callback(GstBus *bus, GstMessage *message, gpointer data) {
    switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug;
            gst_message_parse_error(message, &err, &debug);
            g_print("pipeline_bus_callback:GST_MESSAGE_ERROR Error/code : %s/%d\n", err->message, err->code);
            if (err->code == 7) {
                pause_play_pipeline(data);
                g_error_free(err);
                g_free(debug);
                return FALSE;
            }
            g_error_free(err);
            g_free(debug);
            break;
        }
        case GST_MESSAGE_EOS: {
            g_print("pipeline_bus_callback:GST_MESSAGE_EOS \n");
            //pause_play_pipeline(data);
            return FALSE;
        }
        default: {
            //g_print("pipeline_bus_callback:default Got %s message \n", GST_MESSAGE_TYPE_NAME (message));
            break;
        }
    }
    return TRUE;
}

gboolean RtspPipelineHandler::start_streaming() {
    GstStateChangeReturn ret;
    GError *error = NULL;
    GstBus *bus;

    /* NOTE: webrtcbin currently does not support dynamic addition/removal of
     * streams, so we use a separate webrtcbin for each peer, but all of them are
     * inside the same pipeline. We start by connecting it to a fakesink so that
     * we can preroll early. */
    std::string pipeline_string = string("tee name=videotee ! queue ! fakesink ") +
                                  string("rtspsrc name=rtspsource location=" + rtsp_url +
                                         " latency=10 drop-on-latency=TRUE ! rtph264depay name=rtspdepay ! videotee. ");

    pipeline = gst_parse_launch(pipeline_string.c_str(), &error);

    if (error) {
        g_printerr("Failed to parse launch: %s\n", error->message);
        g_error_free(error);
        goto err;
    }

    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_enable_sync_message_emission(bus);
    gst_bus_set_sync_handler(bus, (GstBusSyncHandler) pipeline_bus_callback, this, NULL);
    gst_object_unref(GST_OBJECT(bus));

    start_recording_video(prepare_next_file_name(), pipeline); //Need to check this

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

gboolean RtspPipelineHandler::stop_streaming() {
    g_print("stop_recording_video file \n");
    GstElement *queue;
    GstPad *queue_src_pad;

    queue = gst_bin_get_by_name(GST_BIN (pipeline), "queue-recorder");
    queue_src_pad = gst_element_get_static_pad(queue, "sink");
    g_print("pushing EOS event on pad %s:%s\n", GST_DEBUG_PAD_NAME (queue_src_pad));

    /* tell pipeline to forward EOS message from filesink immediately and not
     * hold it back until it also got an EOS message from the video sink */
    g_object_set(pipeline, "message-forward", TRUE, NULL);

    gst_pad_send_event(queue_src_pad, gst_event_new_eos());
    gst_object_unref(queue_src_pad);
    gst_object_unref(queue);
    g_print("stopped_recording_video file \n");

    auto it = pipelineHandlers.find(pipeline_execution_id);
    if (it != pipelineHandlers.end()) {
        for (auto elem : it->second->peers) {
            WebrtcViewer webrtcViewer = *(elem.second);
            webrtcViewer.close_peer_from_server();
        }
    }

    peers.clear();
    gst_element_send_event(pipeline, gst_event_new_eos());
    g_print("Removed peers for pipeline \n");

    std::this_thread::sleep_for(std::chrono::seconds(2));
    if (pipeline) {
        g_print("Pipeline Ref Count %d\n", GST_OBJECT_REFCOUNT_VALUE(pipeline));
        gst_element_set_state(GST_ELEMENT (pipeline), GST_STATE_PAUSED);
        gst_element_set_state(GST_ELEMENT (pipeline), GST_STATE_NULL);
        gst_object_unref(pipeline);
        g_print("Pipeline stopped for rtsp url %s\n", rtsp_url.c_str());
    }
    return TRUE;
}

static void add_webrtc_peer(RtspPipelineHandlerPtr pipelineHandlerPtr, std::string peer_id) {
    WebrtcViewerPtr webrtcViewerPtr = std::make_shared<WebrtcViewer>();
    webrtcViewerPtr->peer_id = peer_id;
    webrtcViewerPtr->pipeline = pipelineHandlerPtr->pipeline;
    /* Disable ssl when running a localhost server, because
    * it's probably a test server with a self-signed certificate */
    {
        GstUri *uri = gst_uri_from_string(webrtcViewerPtr->server_url.c_str());
        if (g_strcmp0("localhost", gst_uri_get_host(uri)) == 0 ||
            g_strcmp0("127.0.0.1", gst_uri_get_host(uri)) == 0)
            webrtcViewerPtr->disable_ssl = TRUE;
        gst_uri_unref(uri);
    }
    WebRTC_Launch_Task *webRTC_launch_task = new WebRTC_Launch_Task();
    std::thread th(&WebRTC_Launch_Task::execute, webRTC_launch_task, webrtcViewerPtr);
    th.detach();
    webrtcViewerPtr->pipeline_execution_id = pipelineHandlerPtr->pipeline_execution_id;
    pipelineHandlerPtr->peers[webrtcViewerPtr->peer_id] = webrtcViewerPtr;
}

int
main(int argc, char *argv[]) {
    signal(SIGSEGV, handler);
    GOptionContext *context;
    GError *error = NULL;

    context = g_option_context_new("- gstreamer rtsp -> webrtc demo");

    g_option_context_add_group(context, gst_init_get_option_group());
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("Error initializing: %s\n", error->message);
        return -1;
    }

    if (!check_plugins())
        return -1;

    //Take input rtsp
    std::string rtsp_url;
    cout << "Please enter rtsp url";
    //getline(cin, rtsp_url);
    if (rtsp_url == "") {
        g_print("Using default rtsp url %s\n", "rtsp://127.0.0.1:8554/test");
        rtsp_url.assign("rtsp://127.0.0.1:8554/test");
    } else {
        g_print("Using rtsp url %s\n", rtsp_url.c_str());
    }

    //take input device name
    std::string device_name;
    cout << "Please enter device name";
    //getline(cin, device_name);

    //Start base pipeline
    RtspPipelineHandlerPtr rtspPipelineHandlerPtr = std::make_shared<RtspPipelineHandler>();
    rtspPipelineHandlerPtr->pipeline_execution_id = generate_random_int();
    rtspPipelineHandlerPtr->device_id = device_name;
    rtspPipelineHandlerPtr->rtsp_url = rtsp_url;
    rtspPipelineHandlerPtr->start_streaming();
    if (rtspPipelineHandlerPtr->pipeline == NULL) {
        g_print("Pipeline cannot be created \n");
        return 0;
    }
    pipelineHandlers[rtspPipelineHandlerPtr->pipeline_execution_id] = rtspPipelineHandlerPtr;
    //loop for peers
    while (true) {
        cout << "Please enter a peer id: \n";
        std::string peer_id;
        getline(cin, peer_id);
        if (peer_id == "") {
            g_printerr("peer-id is a required argument\n");
            continue;
            //return -1;
        } else if (peer_id == "exit") {
            g_printerr("Exiting program \n");
            break;
            //return 0;
        }
        add_webrtc_peer(rtspPipelineHandlerPtr, peer_id);
    }

    //To test with single peer - debugging
    /*
    add_webrtc_peer(rtspPipelineHandlerPtr, "5338");
    while (true){}
     */
    rtspPipelineHandlerPtr->stop_streaming();
    return 0;
}
