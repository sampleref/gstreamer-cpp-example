#include <iostream>
#include <memory>
#include <gst/gst.h>
#include <glib.h>
#include <string>
#include <string.h>
//#include <regex>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <x264.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#ifdef __cplusplus
}
#endif

/*int main() {
    std::cout << "Hello, World!" << std::endl;
    std::string str = "ls -l";
    std::cout << str;
    return 0;
}*/

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data);

static gboolean check_rtsp_socket(std::string);

int main(int argc, char *argv[]) {


    /*std::string test_string = "96 packetization-mode=1;profile-level-id=4d401f;sprop-parameter-sets=Z01AH+iAKALdgLUBAQFAAAADAEAAAA8DxgxEgA==,aOuvIA==";
    std::cout << "Actual" << std::endl;
    std::cout << test_string << std::endl;
    test_string = std::regex_replace(test_string, std::regex("profile-level-id=*[A-za-z0-9]*;"), "profile-level-id=42e01f;");
    std::cout << "After" << std::endl;
    std::cout << test_string << std::endl;*/

    //bool result = check_rtsp_socket("rtsp://127.0.0.1:8554/test");
    std::cout << "RTSP local Check" << std::endl;
    //std::cout << result << std::endl;

    av_register_all();
    AVCodec *codec = NULL;
    codec = avcodec_find_encoder_by_name("libx264");
    if(codec != NULL && codec->long_name != NULL){
        std::string str(codec->long_name);
        std::cout << str << std::endl;
    } else{
        std::cout << "Null long name" << std::endl;
    }

    /*GMainLoop *loop;

    GstElement *pipeline, *videotestsrcm, *autovideosinkm;
    GstBus *bus;
    guint bus_watch_id;

    *//* Initialisation *//*
    gst_init (&argc, &argv);

    loop = g_main_loop_new (NULL, FALSE);


    *//* Create gstreamer elements *//*
    pipeline = gst_pipeline_new ("videotest-pipeline");
    videotestsrcm   = gst_element_factory_make ("videotestsrc", "testsource");
    autovideosinkm = gst_element_factory_make ("autovideosink", "videosink");

    if (!pipeline || !videotestsrcm || !autovideosinkm) {
        g_printerr ("One element could not be created. Exiting.\n");
        return -1;
    }

    *//* Set up the pipeline *//*

    *//* we add a message handler *//*
    bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
    bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
    gst_object_unref (bus);


    *//* we add all elements into the pipeline *//*
    gst_bin_add_many (GST_BIN (pipeline),
                      videotestsrcm, autovideosinkm, NULL);

    *//* we link the elements together *//*
    *//* videotestsrcm -> autovideosinkm *//*
    gst_element_link (videotestsrcm, autovideosinkm);


    *//* Set the pipeline to "playing" state*//*
    g_print ("Now set pipeline in state playing");
    gst_element_set_state (pipeline, GST_STATE_PLAYING);


    *//* Iterate *//*
    g_print ("Running...\n");
    g_main_loop_run (loop);


    *//* Out of the main loop, clean up nicely *//*
    g_print ("Returned, stopping playback\n");
    gst_element_set_state (pipeline, GST_STATE_NULL);

    g_print ("Deleting pipeline\n");
    gst_object_unref (GST_OBJECT (pipeline));
    g_source_remove (bus_watch_id);
    g_main_loop_unref (loop);*/

    return 0;
}

static gboolean check_rtsp_socket(std::string rtsp_url) {
    g_print("Checking rtsp connection \n");
    int CreateSocket = 0, n = 0;
    char dataReceived[1024];
    struct sockaddr_in ipOfServer;

    memset(dataReceived, '0', sizeof(dataReceived));

    if ((CreateSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Socket not created \n");
        return 1;
    }

    size_t found = rtsp_url.find_first_of(":");
    std::string protocol = rtsp_url.substr(0, found);
    std::string expected_protocol = "rtsp";
    if (expected_protocol.compare(protocol) != 0) {
        printf("Not a rtsp url \n");
        return false;
    }
    std::string url_new = rtsp_url.substr(found + 3); //url_new is the url excluding the rtsp part
    size_t found1 = url_new.find_first_of("/");
    std::string host_ip_port = url_new.substr(0, found1);

    g_print("Resolved host port is %s\n", host_ip_port.c_str());
    if (host_ip_port.find(':') != std::string::npos) {
        size_t found = host_ip_port.find_first_of(":");
        std::string host_ip = host_ip_port.substr(0, found);
        ipOfServer.sin_addr.s_addr = inet_addr(host_ip.c_str());
        size_t found_end = host_ip_port.find_first_of("/");
        std::string host_port = host_ip_port.substr(++found, found_end);
        ipOfServer.sin_port = htons(atoi(host_port.c_str()));
        g_print("IP and Port resolved as %s and %s \n", host_ip.c_str(), host_port.c_str());
    } else {
        ipOfServer.sin_addr.s_addr = inet_addr(host_ip_port.c_str());
        ipOfServer.sin_port = htons(554); //Default RTSP port
        g_print("IP and Port resolved as %s and %d \n", host_ip_port.c_str(), 554);
    }

    ipOfServer.sin_family = AF_INET;

    if (connect(CreateSocket, (struct sockaddr *) &ipOfServer, sizeof(ipOfServer)) < 0) {
        printf("Connection failed \n");
        return false;
    }
    g_print("Checking rtsp connection - success \n");
    return true;
}

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    GMainLoop *loop = (GMainLoop *) data;

    switch (GST_MESSAGE_TYPE (msg)) {

        case GST_MESSAGE_EOS:
            g_print("End of stream\n");
            g_main_loop_quit(loop);
            break;

        case GST_MESSAGE_ERROR: {
            gchar *debug;
            GError *error;

            gst_message_parse_error(msg, &error, &debug);
            g_free(debug);

            g_printerr("Error: %s\n", error->message);
            g_error_free(error);

            g_main_loop_quit(loop);
            break;
        }
        default:
            break;
    }

    return TRUE;
}
