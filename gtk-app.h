#define WIDTH  800
#define HEIGHT 600
#define FPS    20

static gboolean option_fullscreen = FALSE;
static gint option_debug = -1;
static gint option_screens = 1;
static char **option_reload = NULL;
static char **option_save = NULL;

//static gboolean option_overlay = FALSE;

#define MAX_SCREENS 2

typedef struct windows_s {
  int realised;
  int requested;
  GstElement *sinks[MAX_SCREENS];
  XID        xwindows[MAX_SCREENS];
  GtkWidget  *gtk_windows[MAX_SCREENS];
} windows_t;


#define COMMON_CAPS \
  "framerate", GST_TYPE_FRACTION, FPS, 1,                       \
    "width", G_TYPE_INT, WIDTH,                                 \
    "height", G_TYPE_INT, HEIGHT,                               \
    NULL

