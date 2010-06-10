
#define PIPELINE_TEST 0

#ifndef UNUSED
#define UNUSED __attribute__ ((unused))
#else
#warning UNUSED is set
#endif

#define WIDTH 320
#define HEIGHT 240

#define COMMON_CAPS \
  "framerate", GST_TYPE_FRACTION, 30, 1,                        \
    "width", G_TYPE_INT, WIDTH,                                 \
    "height", G_TYPE_INT, HEIGHT,                               \
    NULL

const gchar *PLUGIN_DIR = "/home/douglas/sparrow";

const guchar *SPARROW_XML = (guchar *)"/home/douglas/sparrow/sparrow.xml";

#if PIPELINE_TEST

static inline GstPipeline *
make_pipeline(GstElement *sink){
  GstPipeline *pipeline = GST_PIPELINE(gst_pipeline_new (NULL));
  GstElement *src = gst_element_factory_make ("videotestsrc", NULL);
  //GstElement *warp = gst_element_factory_make ("warptv", NULL);
  //GstElement *colorspace = gst_element_factory_make ("ffmpegcolorspace", NULL);

  gst_bin_add_many(GST_BIN (pipeline),
      src,
      //warp,
      //colorspace,
      sink, NULL);
  gst_element_link_many(src,
      //warp,
      //colorspace,
      sink, NULL);
  return pipeline;
}

#else

static inline GstPipeline *
make_pipeline(GstElement *sink){
  GstPipeline *pipeline = GST_PIPELINE(gst_pipeline_new("sparow_pipeline"));
  GstElement *src = gst_element_factory_make("v4l2src", NULL);
  //GstElement *src = gst_element_factory_make("videotestsrc", NULL);
  GstElement *caps1 = gst_element_factory_make("capsfilter", "caps1");
  GstElement *cs = gst_element_factory_make("ffmpegcolorspace", NULL);
  GstElement *sparrow = gst_element_factory_make("sparrow", NULL);
  GstElement *caps2 = gst_element_factory_make("capsfilter", "caps1");
  GstElement *cs2 = gst_element_factory_make("ffmpegcolorspace", NULL);

  g_object_set(G_OBJECT(caps1), "caps",
      gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'),
          COMMON_CAPS), NULL);

  g_object_set(G_OBJECT(caps2), "caps",
      gst_caps_new_simple ("video/x-raw-rgb",
          COMMON_CAPS), NULL);


  g_object_set(G_OBJECT(sparrow),
      "timer", TRUE,
      "debug", FALSE,
      "rngseed", 1,
      "colour", 1,
      //"reload", "dumpfiles/gtk.dump",
      //"save", "dumpfiles/gtk.dump",
      NULL);

  gst_bin_add_many (GST_BIN(pipeline), src,
      caps1,
      cs,
      sparrow,
      caps2,
      cs2,
      sink,
      NULL);
  gst_element_link_many(src,
      caps1,
      cs,
      sparrow,
      caps2,
      cs2,
      sink,
      NULL);
  return pipeline;
}



static inline void
post_tee_pipeline(GstPipeline *pipeline, GstElement *tee, GstElement *sink,
    int rngseed, int colour, int timer, int debug){
  GstElement *queue = gst_element_factory_make ("queue", NULL);
  GstElement *sparrow = gst_element_factory_make("sparrow", NULL);
  GstElement *caps_posteriori = gst_element_factory_make("capsfilter", NULL);
  GstElement *cs_posteriori = gst_element_factory_make("ffmpegcolorspace", NULL);

  g_object_set(G_OBJECT(caps_posteriori), "caps",
      gst_caps_new_simple ("video/x-raw-rgb",
          COMMON_CAPS), NULL);

  g_object_set(G_OBJECT(sparrow),
      "timer", timer,
      "debug", debug,
      "rngseed", rngseed,
      "colour", colour,
      //"reload", "dumpfiles/gtk.dump",
      //"save", "dumpfiles/gtk.dump",
      NULL);

  gst_bin_add_many (GST_BIN(pipeline),
      queue,
      sparrow,
      caps_posteriori,
      cs_posteriori,
      sink,
      NULL);

  gst_element_link_many(tee,
      queue,
      sparrow,
      caps_posteriori,
      cs_posteriori,
      sink,
      NULL);
}

/*
gst-launch-0.10  --gst-plugin-path=. --gst-debug=sparrow:5 v4l2src ! ffmpegcolorspace ! tee name=vid2 \
	! queue  ! sparrow  ! 'video/x-raw-rgb,width=320,height=240,framerate=25/1' ! ximagesink \
	vid2. ! queue  ! sparrow  ! 'video/x-raw-rgb,width=320,height=240,framerate=25/1' ! ximagesink
*/

static inline GstPipeline *
make_dual_pipeline(GstElement *sink1, GstElement *sink2)
{
  GstPipeline *pipeline = GST_PIPELINE(gst_pipeline_new("sparow_pipeline"));
  //GstElement *src = gst_element_factory_make("v4l2src", NULL);
  GstElement *src = gst_element_factory_make("videotestsrc", NULL);
  GstElement *caps_priori = gst_element_factory_make("capsfilter", NULL);
  GstElement *cs_priori = gst_element_factory_make("ffmpegcolorspace", NULL);
  GstElement *caps_interiori = gst_element_factory_make("capsfilter", NULL);
  GstElement *tee = gst_element_factory_make ("tee", NULL);

  g_object_set(G_OBJECT(caps_priori), "caps",
      gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'),
          COMMON_CAPS), NULL);

  g_object_set(G_OBJECT(caps_interiori), "caps",
      gst_caps_new_simple ("video/x-raw-rgb",
          COMMON_CAPS), NULL);

  gst_bin_add_many(GST_BIN(pipeline),
      src,
      caps_priori,
      cs_priori,
      caps_interiori,
      tee,
      NULL);

  gst_element_link_many(src,
      caps_priori,
      cs_priori,
      //caps_interiori,
      tee,
      NULL);

  post_tee_pipeline(pipeline, tee, sink1,
      1, 1, 1, 0);
  post_tee_pipeline(pipeline, tee, sink2,
      2, 2, 0, 0);


  return pipeline;

  /*
  pad = gst_element_get_static_pad (aqueue, "sink");
  rpad = gst_element_get_request_pad (tee, "src%d");
  gst_pad_link (rpad, pad);
  gst_object_unref (rpad);
  gst_object_unref (pad);
  gst_element_link_pads (aqueue, "src", asink, "sink");

  pad = gst_element_get_static_pad (vqueue, "sink");
  rpad = gst_element_get_request_pad (tee, "src%d");
  gst_pad_link (rpad, pad);
  gst_object_unref (rpad);
  gst_object_unref (pad);

  pad = gst_element_get_static_pad (tee, "sink");
  gst_element_add_pad (element, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  */
}

















#endif

