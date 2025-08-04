
#include <gst/gst.h>
#include <glib.h>

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
  GMainLoop *loop = (GMainLoop *)data;

  switch (GST_MESSAGE_TYPE(msg)) {
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

int main(int argc, char *argv[]) {
  GMainLoop *loop;
  GstElement *pipeline, *source, *capsfilter, *depay, *queue, *parser, *decoder, *conv, *sink;
  GstBus *bus;
  guint bus_watch_id;

  gst_init(&argc, &argv);
  loop = g_main_loop_new(NULL, FALSE);

  pipeline = gst_pipeline_new("video-receiver");
  source = gst_element_factory_make("udpsrc", "udp-source");
  capsfilter = gst_element_factory_make("capsfilter", "caps-filter");
  depay = gst_element_factory_make("rtph264depay", "depayloader");
  queue = gst_element_factory_make("queue", "buffer-queue");
  parser = gst_element_factory_make("h264parse", "parser");
  decoder = gst_element_factory_make("avdec_h264", "decoder");
  conv = gst_element_factory_make("videoconvert", "converter");
  sink = gst_element_factory_make("xvimagesink", "video-output");

  if (!pipeline || !source || !capsfilter || !depay || !queue || !parser || !decoder || !conv || !sink) {
    g_printerr("One element could not be created. Exiting.\n");
    return -1;
  }

  // Set properties
  g_object_set(G_OBJECT(source), "port", 8001, NULL);

  GstCaps *caps = gst_caps_from_string("application/x-rtp, encoding-name=(string)H264, payload=(int)96");
  g_object_set(G_OBJECT(capsfilter), "caps", caps, NULL);
  gst_caps_unref(caps);

  g_object_set(G_OBJECT(sink), "sync", FALSE, NULL);

  gst_bin_add_many(GST_BIN(pipeline), source, capsfilter, depay, queue, parser, decoder, conv, sink, NULL);
  if (!gst_element_link_many(source, capsfilter, depay, queue, parser, decoder, conv, sink, NULL)) {
    g_printerr("Failed to link elements.\n");
    return -1;
  }

  bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
  gst_object_unref(bus);

  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  g_print("Receiving and displaying video...\n");
  g_main_loop_run(loop);

  g_print("Stopping pipeline\n");
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(GST_OBJECT(pipeline));
  g_source_remove(bus_watch_id);
  g_main_loop_unref(loop);

  return 0;
}
