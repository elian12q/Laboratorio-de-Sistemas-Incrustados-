
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
  GstElement *pipeline, *source, *capsfilter, *nvvidconv1, *nvvideoconvert1, *queue1, *streammux, *nvvideoconvert2,
  *nvinfer, *queue2, *nvdsosd, *tee, *queue_display, *nvoverlaysink, *queue_net, *nvvideoconvert3, *encoder, *parser,
  *payloader, *sink;
  GstBus *bus;
  guint bus_watch_id;

  gst_init(&argc, &argv);
  loop = g_main_loop_new(NULL, FALSE);

  pipeline = gst_pipeline_new("video-stream");
  source = gst_element_factory_make("nvarguscamerasrc", "camera-source");
  capsfilter = gst_element_factory_make("capsfilter", "caps-filter");
  nvvidconv1 = gst_element_factory_make("nvvidconv", "nvvidconv1");
  nvvideoconvert1 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert1");
  queue1 = gst_element_factory_make("queue", "queue1");
  streammux = gst_element_factory_make("nvstreammux", "streammux");
  nvvideoconvert2 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert2");
  nvinfer = gst_element_factory_make("nvinfer", "primary-inference");
  queue2 = gst_element_factory_make("queue", "queue2");
  nvdsosd = gst_element_factory_make("nvdsosd", "onscreendisplay");
  tee = gst_element_factory_make("tee", "tee");
  queue_display = gst_element_factory_make("queue", "queue_display");
  nvoverlaysink = gst_element_factory_make("nvoverlaysink", "display");
  queue_net = gst_element_factory_make("queue", "queue_net");
  nvvideoconvert3 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert3");
  encoder = gst_element_factory_make("nvv4l2h264enc", "h264-encoder");
  parser = gst_element_factory_make("h264parse", "h264-parser");
  payloader = gst_element_factory_make("rtph264pay", "rtp-payloader");
  sink = gst_element_factory_make("udpsink", "udp-sink");

  if (!pipeline || !source || !capsfilter || !nvvidconv1 || !nvvideoconvert1 || !queue1 || !streammux || !nvvideoconvert2
      || !nvinfer || !queue2 || !nvdsosd || !tee || !queue_display || !nvoverlaysink || !queue_net || !nvvideoconvert3 ||
      !encoder || !parser || !payloader || !sink) {
    g_printerr("Failed to create one or more elements.\n");
    return -1;
  }

  g_object_set(G_OBJECT(source), "bufapi-version", TRUE, NULL);

  GstCaps *caps = gst_caps_from_string("video/x-raw(memory:NVMM), format=NV12, width=1280, height=720");
  g_object_set(G_OBJECT(capsfilter), "caps", caps, NULL);
  gst_caps_unref(caps);

  g_object_set(G_OBJECT(streammux), "name", "mux", "batch-size", 1, "width", 1280, "height", 720, "live-source", 1, NULL);

  g_object_set(nvinfer, "config-file-path",
    "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_primary.txt", 
    "model-engine-file",
    "/opt/nvidia/deepstream/deepstream-6.0/samples/models/Primary_Detector/resnet10.caffemodel_b1_gpu0_fp16.engine", NULL);

  g_object_set(G_OBJECT(nvdsosd), "process-mode", 1, NULL);      
  g_object_set(G_OBJECT(nvoverlaysink), "sync", FALSE, NULL); 

  g_object_set(G_OBJECT(encoder), "insert-sps-pps", TRUE, NULL);
  g_object_set(G_OBJECT(payloader), "pt", 96, NULL);
  g_object_set(G_OBJECT(sink), "host", "192.168.100.10", "port", 8001, "sync", FALSE, "async", FALSE, NULL);

  gst_bin_add_many(GST_BIN(pipeline), source, capsfilter, nvvidconv1, nvvideoconvert1, queue1, streammux, nvvideoconvert2,
    nvinfer, queue2, nvdsosd, tee, queue_display, nvoverlaysink, queue_net, nvvideoconvert3, encoder, parser, payloader,
    sink, NULL);

  if (!gst_element_link_many(source, capsfilter, nvvidconv1, nvvideoconvert1, queue1, NULL)) {
    g_printerr("Failed to link source path\n");
    return -1;
  }

  if (!gst_element_link_pads(queue1, "src", streammux, "sink_0")) {
    g_printerr("Failed to link queue1 to streammux\n");
    return -1;
  }

  if (!gst_element_link_many(streammux, nvvideoconvert2, nvinfer, queue2, nvdsosd, tee, NULL)) {
    g_printerr("Failed to link main processing pipeline\n");
    return -1;
  }

  GstPad *tee_display_pad = gst_element_get_request_pad(tee, "src_%u");
  GstPad *queue_display_sink_pad = gst_element_get_static_pad(queue_display, "sink");
  
  if (gst_pad_link(tee_display_pad, queue_display_sink_pad) != GST_PAD_LINK_OK) {
    g_printerr("Failed to link tee to queue_display\n");
    return -1;
  }

  gst_object_unref(tee_display_pad);
  gst_object_unref(queue_display_sink_pad);

  if (!gst_element_link_many(queue_display, nvoverlaysink, NULL)) {
    g_printerr("Failed to link display branch\n");
    return -1;
  }

  GstPad *tee_net_pad = gst_element_get_request_pad(tee, "src_%u");
  GstPad *queue_net_sink_pad = gst_element_get_static_pad(queue_net, "sink");

  if (gst_pad_link(tee_net_pad, queue_net_sink_pad) != GST_PAD_LINK_OK) {
    g_printerr("Failed to link tee to queue_net\n");
    return -1;
  }

  gst_object_unref(tee_net_pad);
  gst_object_unref(queue_net_sink_pad);

  if (!gst_element_link_many(queue_net, nvvideoconvert3, encoder, parser, payloader, sink, NULL)) {
    g_printerr("Failed to link network branch\n");
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
}
