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

