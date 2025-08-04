
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
  GstElement *pipeline, *source, *capsfilter, *encoder, *parser, *payloader, *sink;
  GstBus *bus;
  guint bus_watch_id;

  gst_init(&argc, &argv);
  loop = g_main_loop_new(NULL, FALSE);

  pipeline = gst_pipeline_new("video-stream");
  source = gst_element_factory_make("nvarguscamerasrc", "camera-source");
  capsfilter = gst_element_factory_make("capsfilter", "caps-filter");
  encoder = gst_element_factory_make("nvv4l2h264enc", "h264-encoder");
  parser = gst_element_factory_make("h264parse", "h264-parser");
  payloader = gst_element_factory_make("rtph264pay", "rtp-payloader");
  sink = gst_element_factory_make("udpsink", "udp-sink");

  if (!pipeline || !source || !capsfilter || !encoder || !parser || !payloader || !sink) {
    g_printerr("Failed to create one or more elements.\n");
    return -1;
  }

  GstCaps *caps = gst_caps_from_string("video/x-raw(memory:NVMM), format=NV12, width=1280, height=720");
  g_object_set(G_OBJECT(capsfilter), "caps", caps, NULL);
  gst_caps_unref(caps);

  g_object_set(G_OBJECT(encoder), "insert-sps-pps", TRUE, NULL);
  g_object_set(G_OBJECT(payloader), "pt", 96, NULL);
  g_object_set(G_OBJECT(sink), "host", "172.17.0.1", "port", 8001, "sync", FALSE, NULL);

  gst_bin_add_many(GST_BIN(pipeline), source, capsfilter, encoder, parser, payloader, sink, NULL);
  if (!gst_element_link_many(source, capsfilter, encoder, parser, payloader, sink, NULL)) {
    g_printerr("Failed to link elements.\n");
    return -1;
  }

  bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
  gst_object_unref(bus);

  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  g_print("Streaming video...\n");
  g_main_loop_run(loop);

  g_print("Stopping pipeline\n");
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(GST_OBJECT(pipeline));
  g_source_remove(bus_watch_id);
  g_main_loop_unref(loop);

  return 0;
}
