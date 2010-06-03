
#define PIPELINE_TEST 0

#ifndef UNUSED
#define UNUSED __attribute__ ((unused))
#else
#warning UNUSED is set
#endif

#define WIDTH 352
#define HEIGHT 288

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
          "format", G_TYPE_STRING, "(fourcc)YUY2",
          "width", G_TYPE_INT, WIDTH,
          "height", G_TYPE_INT, HEIGHT,
          "framerate", GST_TYPE_FRACTION, 25, 1,
          NULL), NULL);

  g_object_set(G_OBJECT(caps2), "caps",
      gst_caps_new_simple ("video/x-raw-rgb",
          //"format", G_TYPE_STRING, "(fourcc)YUY2",
          "width", G_TYPE_INT, WIDTH,
          "height", G_TYPE_INT, HEIGHT,
          "framerate", GST_TYPE_FRACTION, 25, 1,
          NULL), NULL);


  g_object_set(G_OBJECT(sparrow),
      "timer", TRUE,
      "debug", FALSE,
      "rngseed", 1,
      "colour", 1,
      //"reload", "dumpfiles/gtk.dump",
      //"save", "dumpfiles/gtk.dump",
      NULL);

  gst_bin_add_many (GST_BIN(pipeline), src,
      //caps1,
      cs,
      sparrow,
      caps2,
      cs2,
      sink,
      NULL);
  gst_element_link_many(src,
      //caps1,
      cs,
      sparrow,
      caps2,
      cs2,
      sink,
      NULL);
  return pipeline;
}

#endif

