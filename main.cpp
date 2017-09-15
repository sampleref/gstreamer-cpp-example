#include <iostream>
#include <memory>
#include <gst/gst.h>
#include <glib.h>

/*int main() {
    std::cout << "Hello, World!" << std::endl;
    std::string str = "ls -l";
    std::cout << str;
    return 0;
}*/

static gboolean bus_call (GstBus *bus, GstMessage *msg, gpointer data);

int main (int argc, char *argv[])
{
    GMainLoop *loop;

    GstElement *pipeline, *videotestsrcm, *autovideosinkm;
    GstBus *bus;
    guint bus_watch_id;

    /* Initialisation */
    gst_init (&argc, &argv);

    loop = g_main_loop_new (NULL, FALSE);


    /* Create gstreamer elements */
    pipeline = gst_pipeline_new ("videotest-pipeline");
    videotestsrcm   = gst_element_factory_make ("videotestsrc", "testsource");
    autovideosinkm = gst_element_factory_make ("autovideosink", "videosink");

    if (!pipeline || !videotestsrcm || !autovideosinkm) {
        g_printerr ("One element could not be created. Exiting.\n");
        return -1;
    }

    /* Set up the pipeline */

    /* we add a message handler */
    bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
    bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
    gst_object_unref (bus);


    /* we add all elements into the pipeline */
    gst_bin_add_many (GST_BIN (pipeline),
                      videotestsrcm, autovideosinkm, NULL);

    /* we link the elements together */
    /* videotestsrcm -> autovideosinkm */
    gst_element_link (videotestsrcm, autovideosinkm);


    /* Set the pipeline to "playing" state*/
    g_print ("Now set pipeline in state playing");
    gst_element_set_state (pipeline, GST_STATE_PLAYING);


    /* Iterate */
    g_print ("Running...\n");
    g_main_loop_run (loop);


    /* Out of the main loop, clean up nicely */
    g_print ("Returned, stopping playback\n");
    gst_element_set_state (pipeline, GST_STATE_NULL);

    g_print ("Deleting pipeline\n");
    gst_object_unref (GST_OBJECT (pipeline));
    g_source_remove (bus_watch_id);
    g_main_loop_unref (loop);

    return 0;
}

static gboolean bus_call (GstBus *bus, GstMessage *msg, gpointer data)
{
    GMainLoop *loop = (GMainLoop *) data;

    switch (GST_MESSAGE_TYPE (msg)) {

        case GST_MESSAGE_EOS:
            g_print ("End of stream\n");
            g_main_loop_quit (loop);
            break;

        case GST_MESSAGE_ERROR: {
            gchar  *debug;
            GError *error;

            gst_message_parse_error (msg, &error, &debug);
            g_free (debug);

            g_printerr ("Error: %s\n", error->message);
            g_error_free (error);

            g_main_loop_quit (loop);
            break;
        }
        default:
            break;
    }

    return TRUE;
}
