#include <gst/gst.h>
#include <glib.h>

//-----------------------------------------------------
#include <string.h>
#include <signal.h>
#include <stdlib.h>
//-----------------------------------------------------

// Values used in the code
#define VIDEO_INPUT 1   // Video input mode value for flag
#define CAMERA_INPUT 0  // Camera input mode value for flag

/* ===========================================================================
 *                      GST ELEMENT POINTER DEFINITION
 * ===========================================================================
 * */
GstElement *pipeline; // used in both

GstElement *source, *capsfilter, *nvvidconv1, *nvvideoconvert1; // used in camera only
GstElement *filesrc, *qtdemux, *h264parse, *nvv4l2decoder; // used in video only

GstElement *queue1;  // used in both

GstElement *streammux; // used in camera only
GstElement *videomux;  // used in video only

GstElement *nvvideoconvert2, *nvinfer1, *queue2, *nvtracker, *queue3,           // used in both
           *nvinfer2, *queue4, *nvdsosd, *tee, *queue_display, *nvoverlaysink,  // used in both
           *queue_net, *nvvideoconvert3, *encoder1, *parser1, *payloader,       // used in both  
           *sink, *queue_file, *nvvideoconvert4, *encoder2, *parser2, *mux,     // used in both
           *filesink;                                                           // used in both


/* ===========================================================================
 *                      FUNCTION DECLARATION 
 * ===========================================================================
 * */

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

gboolean send_eos(gpointer data) {
  GstElement *pipeline = (GstElement *)data;
  g_print("Sending EOS to pipeline...\n");
  gst_element_send_event(pipeline, gst_event_new_eos());
  return FALSE;
}

// ----------------CLOSE THE VIDEO WITH -E FLAG ------------------
// solution of this issue was taken from: https://gist.github.com/crearo/a49a8805857f1237c401be14ba6d3b03
void sigintHandler(int unused) {
	g_print("You ctrl-c-ed!");
	gst_element_send_event(pipeline, gst_event_new_eos());
	//return 0;
}
// ---------------------------------------------------------------


static void on_pad_added (GstElement *element, GstPad *pad, gpointer data){
  GstPad *sinkpad;
  GstElement *decoder = (GstElement *) data;

  /* We can now link this pad with the vorbis-decoder sink pad */
  g_print ("Dynamic pad created, linking demuxer/decoder\n");

  sinkpad = gst_element_get_static_pad (decoder, "sink");

  gst_pad_link (pad, sinkpad);

  gst_object_unref (sinkpad);
}


int main(int argc, char *argv[]) {

  int modeFlag = 0; // variable used as a flag for choosing the mode of
                    // operation for the input format for the video file
                    // or the imx219 camera.

   // Evaluation of the command-line arguments provided, so that a
  // specific value can be assigned to the modeFlag
  if (argc == 2 && (strcmp(argv[1], "--testingInputMP4") == 0)) {
      modeFlag = VIDEO_INPUT;
  }
  else {
      modeFlag = CAMERA_INPUT;
  }

  GMainLoop *loop;

  GstBus *bus;
  guint bus_watch_id;

  // -----------------CONNECT CTRL C WITH FUNC ------------------
  // solution of this issue was taken from: https://gist.github.com/crearo/a49a8805857f1237c401be14ba6d3b03
  signal(SIGINT, sigintHandler);
  // ------------------------------------------------------------

  gst_init(&argc, &argv);
  loop = g_main_loop_new(NULL, FALSE);

  /* =========================================================================
   *                  GST POINTER DEFINITION
   * =========================================================================
   * */
  pipeline        = gst_pipeline_new("video-stream"); // both

  if (modeFlag == CAMERA_INPUT) {
      source          = gst_element_factory_make("nvarguscamerasrc", "camera-source"); // camera 
      capsfilter      = gst_element_factory_make("capsfilter", "caps-filter");         // camera 
      nvvidconv1      = gst_element_factory_make("nvvidconv", "nvvidconv1");           // camera     
      nvvideoconvert1 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert1"); // camera
  }
  else { // modeFlag == VIDEO_INPUT
      filesrc         = gst_element_factory_make("filesrc", "filesrc");               // videofile 
      qtdemux         = gst_element_factory_make("qtdemux", "qtdemux");               // videofile  
      h264parse       = gst_element_factory_make("h264parse", "h264parse");           // videofile     
      nvv4l2decoder   = gst_element_factory_make("nvv4l2decoder", "nvv4l2decoder");   // videofile  
  }
  
  queue1          = gst_element_factory_make("queue", "queue1");                  // both

  if (modeFlag == CAMERA_INPUT) {
      streammux       = gst_element_factory_make("nvstreammux", "streammux");         // camera
  }
  else { // modeFlag == VIDEO_INPUT
      videomux        = gst_element_factory_make("nvstreammux", "videomux");          // videofile
  }

  // ALL BELLOW ARE FOR BOTH
  nvvideoconvert2 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert2");  
  nvinfer1        = gst_element_factory_make("nvinfer", "primary-inference");
  queue2          = gst_element_factory_make("queue", "queue2");
  nvtracker       = gst_element_factory_make("nvtracker", "tracker");
  queue3          = gst_element_factory_make("queue", "queue3");
  nvinfer2        = gst_element_factory_make("nvinfer", "secondary-inference");
  queue4          = gst_element_factory_make("queue", "queue4");
  nvdsosd         = gst_element_factory_make("nvdsosd", "onscreendisplay");
  tee             = gst_element_factory_make("tee", "tee");
  queue_display   = gst_element_factory_make("queue", "queue_display");
  nvoverlaysink   = gst_element_factory_make("nvoverlaysink", "display");
  queue_net       = gst_element_factory_make("queue", "queue_net");
  nvvideoconvert3 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert3");
  encoder1        = gst_element_factory_make("nvv4l2h264enc", "h264-encoder1");
  parser1         = gst_element_factory_make("h264parse", "h264-parser1");
  payloader       = gst_element_factory_make("rtph264pay", "rtp-payloader");
  sink = gst_element_factory_make("udpsink", "udp-sink");
  queue_file = gst_element_factory_make("queue", "queue_file");
  nvvideoconvert4 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert4");
  encoder2 = gst_element_factory_make("nvv4l2h264enc", "h264-encoder2");
  parser2 = gst_element_factory_make("h264parse", "h264-parser2");
  mux = gst_element_factory_make("qtmux", "mp4-muxer");
  filesink = gst_element_factory_make("filesink", "file-sink");

  /* =========================================================================
   *          CHECK CREATION AND INITIAL OBJ SETTING
   * =========================================================================
   * */

  if (modeFlag == CAMERA_INPUT) {
      if (!pipeline || !source || !capsfilter || !nvvidconv1 || !nvvideoconvert1 || !queue1 || !streammux || !nvvideoconvert2
          || !nvinfer1 || !queue2 || !nvtracker || !queue3 || !nvinfer2 || !queue4 || !nvdsosd || !tee || !queue_display
          || !nvoverlaysink || !queue_net || !nvvideoconvert3 || !encoder1 || !parser1 || !payloader || !sink || !queue_file
          || !nvvideoconvert4 || !encoder2 || !parser2 || !mux || !filesink) {
        g_printerr("Failed to create one or more elements.\n");
        return -1;
      }
    
      g_object_set(G_OBJECT(source), "bufapi-version", TRUE, NULL);
    
      GstCaps *caps = gst_caps_from_string("video/x-raw(memory:NVMM), format=NV12, width=1280, height=720");
      g_object_set(G_OBJECT(capsfilter), "caps", caps, NULL);
      gst_caps_unref(caps);
    
      g_object_set(G_OBJECT(streammux), "name", "mux", "batch-size", 1, "width", 1280, "height", 720, "batched-push-timeout",
                    40000, "live-source", 1, NULL);
  }
  else {  // modeFlag == VIDEO_INPUT
      if (!pipeline || !filesrc || !qtdemux || !h264parse || !nvv4l2decoder || !queue1 || !videomux || !nvvideoconvert2
          || !nvinfer1 || !queue2 || !nvtracker || !queue3 || !nvinfer2 || !queue4 || !nvdsosd || !tee || !queue_display
          || !nvoverlaysink || !queue_net || !nvvideoconvert3 || !encoder1 || !parser1 || !payloader || !sink || !queue_file
          || !nvvideoconvert4 || !encoder2 || !parser2 || !mux || !filesink) {
        g_printerr("Failed to create one or more elements.\n");
        return -1;
      }

      g_object_set(
          G_OBJECT(filesrc), "location", "/opt/nvidia/deepstream/deepstream-6.0/samples/streams/sample_1080p_h264.mp4", 
          NULL);

      g_object_set(
          G_OBJECT(videomux), "name", "mux", "batch-size", 1, "width", 1280, "height", 720, "batched-push-timeout",
          40000, NULL);
  }


  g_object_set(nvinfer1, "config-file-path",
    "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_primary_nano.txt",
    "model-engine-file",
    "/opt/nvidia/deepstream/deepstream-6.0/samples/models/Primary_Detector_Nano/resnet10.caffemodel_b8_gpu0_fp16.engine",
    "unique-id", 1, NULL);

  g_object_set(nvtracker, "tracker-width", 640, "tracker-height", 368,
    "ll-lib-file", "/opt/nvidia/deepstream/deepstream-6.0/lib/libnvds_nvmultiobjecttracker.so",
    "ll-config-file", "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_tracker_IOU.yml",
    "enable-batch-process", 1, NULL);

  g_object_set(nvinfer2, "config-file-path",
    "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_secondary_carmake.txt",
    "model-engine-file",
    "/opt/nvidia/deepstream/deepstream-6.0/samples/models/Secondary_CarMake/resnet18.caffemodel_b1_gpu0_fp16.engine",
    "process-mode", 2, "infer-on-gie-id", 1, "infer-on-class-ids", "0:", "batch-size", 1, "unique-id", 2, NULL);

  g_object_set(G_OBJECT(nvdsosd), "process-mode", 1, NULL);
  g_object_set(G_OBJECT(nvoverlaysink), "sync", FALSE, NULL);

  g_object_set(G_OBJECT(encoder1), "insert-sps-pps", TRUE, NULL);
  g_object_set(G_OBJECT(encoder2), "insert-sps-pps", TRUE, NULL);
  g_object_set(G_OBJECT(payloader), "pt", 96, "config-interval", 1, NULL);
  g_object_set(G_OBJECT(sink), "host", "192.168.55.100", "port", 8001, "sync", FALSE, "async", FALSE, NULL);
  g_object_set(G_OBJECT(filesink), "location", "Proyecto_camara.mp4", NULL);


  if (modeFlag == CAMERA_INPUT) {
      gst_bin_add_many(GST_BIN(pipeline), source, capsfilter, nvvidconv1, nvvideoconvert1, queue1, streammux, nvvideoconvert2,
      nvinfer1, queue2, nvtracker, queue3, nvinfer2, queue4, nvdsosd, tee, queue_display, nvoverlaysink, queue_net,
      nvvideoconvert3, encoder1, parser1, payloader, sink, queue_file, nvvideoconvert4, encoder2, parser2, mux, filesink, NULL);

      if (!gst_element_link_many(source, capsfilter, nvvidconv1, nvvideoconvert1, queue1, NULL)) {
        g_printerr("Failed to link source path for the camera\n");
        return -1;
      }

      if (!gst_element_link_pads(queue1, "src", streammux, "sink_0")) {
        g_printerr("Failed to link queue1 to streammux\n");
        return -1; 
      }

      if (!gst_element_link_many(streammux, nvvideoconvert2, nvinfer1, queue2, nvtracker, queue3, nvinfer2, queue4, nvdsosd,
          tee, NULL)) {
        g_printerr("Failed to link main processing pipeline in the camera\n");
        return -1;
      }

  }
  else { // modeFlag == VIDEO_INPUT
      gst_bin_add_many(GST_BIN(pipeline), filesrc, qtdemux, h264parse, nvv4l2decoder, queue1, videomux, nvvideoconvert2,
      nvinfer1, queue2, nvtracker, queue3, nvinfer2, queue4, nvdsosd, tee, queue_display, nvoverlaysink, queue_net,
      nvvideoconvert3, encoder1, parser1, payloader, sink, queue_file, nvvideoconvert4, encoder2, parser2, mux, filesink, NULL);

      if (!gst_element_link(filesrc, qtdemux)) {
        g_printerr("Failed to link source to qtdemux\n");
        return -1;
      }
      
      if (!gst_element_link_many(h264parse, nvv4l2decoder, queue1, NULL)) {
        g_printerr("Failed to link parser, coder and first queue\n");
        return -1;
      }
      
      if (!g_signal_connect (qtdemux, "pad-added", G_CALLBACK (on_pad_added), h264parse)){
	g_printerr("Failed to dinamically add pads to the qtdemuxer\n"); 
	return -1;
      }

      if (!gst_element_link_pads(queue1, "src", videomux, "sink_0")) {
        g_printerr("Failed to link queue1 to videomux\n");
        return -1; 
      }

      if (!gst_element_link_many(videomux, nvvideoconvert2, nvinfer1, queue2, nvtracker, queue3, nvinfer2, queue4, nvdsosd,
          tee, NULL)) {
        g_printerr("Failed to link main processing pipeline in the video\n");
        return -1;
      }


  }

  GstPad *tee_display_pad = gst_element_get_request_pad(tee, "src_%u");
  GstPad *queue_display_sink_pad = gst_element_get_static_pad(queue_display, "sink");

  if (gst_pad_link(tee_display_pad, queue_display_sink_pad) != GST_PAD_LINK_OK) {
    g_printerr("Failed to link tee to display queue\n");
    return -1;
  }
  g_printerr("SUCCESS TO LINK TEE TO DISPLAY\n");

  gst_object_unref(tee_display_pad);
  gst_object_unref(queue_display_sink_pad);

  if (!gst_element_link_many(queue_display, nvoverlaysink, NULL)) {
    g_printerr("Failed to link display branch\n");
    return -1;
  }
  g_printerr("SUCCESS TO LINK DISPLAY BRANCH\n");
  
  GstPad *tee_net_pad = gst_element_get_request_pad(tee, "src_%u");
  GstPad *queue_net_sink_pad = gst_element_get_static_pad(queue_net, "sink");

  if (gst_pad_link(tee_net_pad, queue_net_sink_pad) != GST_PAD_LINK_OK) {
    g_printerr("Failed to link tee to UDP queue\n");
    return -1;
  }
  g_printerr("SUCCESS TO LINK UDP QUEUE\n");

  gst_object_unref(tee_net_pad);
  gst_object_unref(queue_net_sink_pad);

  if (!gst_element_link_many(queue_net, nvvideoconvert3, encoder1, parser1, payloader, sink, NULL)) {
    g_printerr("Failed to link UDP branch\n");
    return -1;
  }
  g_printerr("SUCCESS TO LINK UDP BRANCH\n");

  GstPad *tee_file_pad = gst_element_get_request_pad(tee, "src_%u");
  GstPad *queue_file_sink_pad = gst_element_get_static_pad(queue_file, "sink");
  if (gst_pad_link(tee_file_pad, queue_file_sink_pad) != GST_PAD_LINK_OK) {
  g_printerr("Failed to link tee to file queue\n");
  return -1;
  }
  g_printerr("SUCCESS TO LINK TEE TO FILE QUEUE\n");

  gst_object_unref(tee_file_pad);
  gst_object_unref(queue_file_sink_pad);

  if (!gst_element_link_many(queue_file, nvvideoconvert4, encoder2, parser2, mux, filesink, NULL)) {
  g_printerr("Failed to link file branch\n");
  return -1;
  }
  g_printerr("SUCCESS TO LINK FILE BRANCH\n");

  bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
  gst_object_unref(bus);

  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  g_print("Streaming video...\n");
  //g_timeout_add(60000, send_eos, pipeline);
  g_main_loop_run(loop);

  g_print("Stopping pipeline\n");
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(GST_OBJECT(pipeline));
  g_source_remove(bus_watch_id);
  g_main_loop_unref(loop);
}


