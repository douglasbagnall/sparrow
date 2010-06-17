#define WIDTH  800
#define HEIGHT 600
//#define FPS    20

#define QUOTE_(x) #x
#define QUOTE(x) QUOTE_(x)

static gboolean option_fake = FALSE;
static gboolean option_fullscreen = FALSE;
static gint option_debug = -1;
static gint option_timer = -1;
static gint option_screens = 1;
static gint option_fps = 20;
static guint option_first_screen = 0;
static guint option_serial = 0;
static char **option_reload = NULL;
static char **option_save = NULL;
static char *option_avi = NULL;


#define MAX_SCREENS 2

static GOptionEntry entries[] =
{
  { "fake-source", 0, 0, G_OPTION_ARG_NONE, &option_fake,
    "use videotestsrc, not v4l2src (mostly won't work)", NULL },
  { "full-screen", 'f', 0, G_OPTION_ARG_NONE, &option_fullscreen, "run full screen", NULL },
  { "fps", 'p', 0, G_OPTION_ARG_INT, &option_fps,
    "speed (Frames per second, multiple of 5 <= 30)", "FPS" },
  { "screens", 's', 0, G_OPTION_ARG_INT, &option_screens, "Use this many screens, (max "
    QUOTE(MAX_SCREENS) ")", "S" },
  { "first-screen", 0, 0, G_OPTION_ARG_INT, &option_first_screen, "Start with this screen", "S" },
  { "debug", 'd', 0, G_OPTION_ARG_INT, &option_debug, "Save screen's debug images in /tmp", "SCREEN" },
  { "timer", 't', 0, G_OPTION_ARG_INT, &option_timer, "Log frame times in /tmp/timer.log", "SCREEN" },
  { "serial-calibration", 'c', 0, G_OPTION_ARG_NONE, &option_serial,
    "calibrate projections one at a time, not together", NULL },
  { "reload", 'r', 0, G_OPTION_ARG_FILENAME_ARRAY, &option_reload,
    "load calibration data from FILE (one per screen)", "FILE" },
  { "save", 'S', 0, G_OPTION_ARG_FILENAME_ARRAY, &option_save,
    "save calibration data to FILE (one per screen)", "FILE" },
  { "avi", 'a', 0, G_OPTION_ARG_FILENAME, &option_avi,
    "save mjpeg video to FILE", "FILE" },
  { NULL, 0, 0, 0, NULL, NULL, NULL }
};


typedef struct windows_s {
  int realised;
  int requested;
  GstElement *sinks[MAX_SCREENS];
  XID        xwindows[MAX_SCREENS];
  GtkWidget  *gtk_windows[MAX_SCREENS];
} windows_t;


#define COMMON_CAPS \
  "framerate", GST_TYPE_FRACTION, option_fps, 1,                \
    "width", G_TYPE_INT, WIDTH,                                 \
    "height", G_TYPE_INT, HEIGHT,                               \
    NULL



static void
mjpeg_branch(GstPipeline *pipeline, GstElement *tee)
{
  /* ! jpegenc ! avimux ! filesink location=mjpeg.avi */
  GstElement *queue = gst_element_factory_make("queue", NULL);
  GstElement *jpegenc = gst_element_factory_make("jpegenc", NULL);
  GstElement *avimux = gst_element_factory_make("avimux", NULL);
  GstElement *filesink = gst_element_factory_make("filesink", NULL);
  GstElement *cs = gst_element_factory_make("ffmpegcolorspace", NULL);
  g_object_set(G_OBJECT(filesink),
      "location", option_avi,
      NULL);
  gst_bin_add_many(GST_BIN(pipeline),
      queue,
      cs,
      jpegenc,
      avimux,
      filesink,
      NULL);
  gst_element_link_many(tee,
      queue,
      cs,
      jpegenc,
      avimux,
      filesink,
      NULL);
}

static void
post_tee_pipeline(GstPipeline *pipeline, GstElement *tee, GstElement *sink,
    int rngseed, int colour, int timer, int debug, char *save, char *reload){
  GstElement *queue = gst_element_factory_make("queue", NULL);
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
      "serial", option_serial,
      NULL);
  if (reload){
    g_object_set(G_OBJECT(sparrow),
        "reload", reload,
        NULL);
  }
  if (save){
    g_object_set(G_OBJECT(sparrow),
        "save", save,
        NULL);
  }

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

static GstElement *
pre_tee_pipeline(GstPipeline *pipeline){
  if (pipeline == NULL){
    pipeline = GST_PIPELINE(gst_pipeline_new("sparrow_pipeline"));
  }
  char * src_name = (option_fake) ? "videotestsrc" : "v4l2src";
  GstElement *src = gst_element_factory_make(src_name, NULL);
  GstElement *caps_priori = gst_element_factory_make("capsfilter", NULL);
  GstElement *cs_priori = gst_element_factory_make("ffmpegcolorspace", NULL);
  GstElement *caps_interiori = gst_element_factory_make("capsfilter", NULL);
  GstElement *tee = gst_element_factory_make ("tee", NULL);

  g_object_set(G_OBJECT(caps_priori), "caps",
      gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('Y', 'U', 'Y', '2'),
          COMMON_CAPS), NULL);

  g_object_set(G_OBJECT(caps_interiori), "caps",
      gst_caps_new_simple ("video/x-raw-rgb",
          COMMON_CAPS), NULL);

  gst_bin_add_many(GST_BIN(pipeline),
      src,
      caps_priori,
      cs_priori,
      //caps_interiori,
      tee,
      NULL);

  gst_element_link_many(src,
      caps_priori,
      cs_priori,
      //caps_interiori,
      tee,
      NULL);
  return tee;
}


static void hide_mouse(GtkWidget *widget){
  GdkWindow *w = GDK_WINDOW(widget->window);
  GdkDisplay *display = gdk_display_get_default();
  GdkCursor *cursor = gdk_cursor_new_for_display(display, GDK_BLANK_CURSOR);
  gdk_window_set_cursor(w, cursor);
  gdk_cursor_unref (cursor);
}
