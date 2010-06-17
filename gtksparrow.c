/*
initially based on an example by Tristan Matthews
http://tristanswork.blogspot.com/2008/09/fullscreen-video-in-gstreamer-with-gtk.html
*/
#include <gst/gst.h>
#include <gtk/gtk.h>
#include <gst/interfaces/xoverlay.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include "gtk-app.h"

GstElement *global_pipeline = NULL;

static GstPipeline *
make_multi_pipeline(GstElement **sinks, int count)
{
  GstPipeline *pipeline = GST_PIPELINE(gst_pipeline_new("sparrow_pipeline"));
  GstElement *tee = pre_tee_pipeline(pipeline);
  char *reload = NULL;
  char *save = NULL;
  int i;
  for (i = 0; i < count; i++){
    GstElement *sink = gst_element_factory_make("ximagesink", NULL);
    sinks[i] = sink;
    //args are:
    //(pipeline, tee, sink, int rngseed, int colour, timer flag, int debug flag)
    /* timer should only run on one of them. colour >= 3 is undefined */
    int debug = option_debug == i;
    int timer = option_timer == i;
    if (option_reload != NULL){
      if (option_reload[i] == NULL){
        g_critical("You can't reload some screens and not others!");
        exit(1);
      }
      reload = option_reload[i];
    }
    if (option_save && option_save[i]){
      save = option_save[i];
    }
    post_tee_pipeline(pipeline, tee, sink, i, i + 1, timer, debug, save, reload);
  }
  if (option_avi){
    /*add a branch saving the video to a file */
    mjpeg_branch(pipeline, tee);
  }

  return pipeline;
}


static void
toggle_fullscreen(GtkWidget *widget){
  GdkWindowState state = gdk_window_get_state(GDK_WINDOW(widget->window));
  if (state == GDK_WINDOW_STATE_FULLSCREEN){
    gtk_window_unfullscreen(GTK_WINDOW(widget));
  }
  else{
    gtk_window_fullscreen(GTK_WINDOW(widget));
  }
}

static gboolean
key_press_event_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
  switch (event->keyval){
  case 'f':
    toggle_fullscreen(widget);
    break;
  case 'q':
    g_signal_emit_by_name(widget, "destroy");
    break;
  default:
    break;
  }
  return TRUE;
}

static void
window_closed(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
  gtk_widget_hide_all(widget);
  gst_element_set_state(global_pipeline, GST_STATE_NULL);
  gtk_main_quit();
}



static GtkWidget *
set_up_window(GstElement *sink, int screen_no){
  static const GdkColor black = {0, 0, 0, 0};

  GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_signal_connect(G_OBJECT(window), "delete-event",
      G_CALLBACK(window_closed), (gpointer)sink);


  gtk_window_set_default_size(GTK_WINDOW(window), WIDTH, HEIGHT);

  GtkWidget *video_window = window;
  //GtkWidget *video_window = gtk_drawing_area_new();
  //gtk_container_add(GTK_CONTAINER(window), video_window);
  gtk_widget_set_double_buffered(video_window, FALSE);
  gtk_container_set_border_width(GTK_CONTAINER(window), 0);


  gtk_widget_show_all(window);
  gtk_widget_realize(window);

  GdkWindow *gdk_window = gtk_widget_get_window(video_window);

  XID xid = GDK_WINDOW_XID(gdk_window);
  gst_x_overlay_set_xwindow_id(GST_X_OVERLAY(sink), xid);

  /*if more than one screen is requested, set the screen position.
    otherwise let it fall were it falls */
  if (option_screens > 1 || option_first_screen){
    /* "screen" is not the same thing as "monitor" */
    GdkScreen * screen = gdk_screen_get_default();
    int width = gdk_screen_get_width(screen);
    /* XXX window selection is specific to equally sized windows arranged
       horizontally.  This could be generalised, perhaps using trial and
       error */
    gtk_window_move(GTK_WINDOW(window),
        (width / 2 * screen_no + 200) % width, 50);
  }
  if (option_fullscreen){
    gtk_window_fullscreen(GTK_WINDOW(window));
  }

  gtk_widget_modify_bg(window, GTK_STATE_NORMAL, &black);
  g_signal_connect(G_OBJECT(video_window), "key-press-event",
      G_CALLBACK(key_press_event_cb), NULL);
  g_signal_connect(G_OBJECT(window), "destroy",
      G_CALLBACK(window_closed), (gpointer)sink);


  hide_mouse(window);
  return window;
}


gint main (gint argc, gchar *argv[])
{
  //initialise threads before any gtk stuff (because not using gtk_init)
  g_thread_init(NULL);
  /*this is more complicated than plain gtk_init/gst_init, so that options from
    all over can be gathered and presented together.
   */
  GOptionGroup *gst_opts = gst_init_get_option_group();
  GOptionGroup *gtk_opts = gtk_get_option_group(TRUE);
  GOptionContext *ctx = g_option_context_new("...!");
  g_option_context_add_main_entries(ctx, entries, NULL);
  g_option_context_add_group(ctx, gst_opts);
  g_option_context_add_group(ctx, gtk_opts);
  GError *error = NULL;
  if (!g_option_context_parse(ctx, &argc, &argv, &error)){
    g_print ("Error initializing: %s\n", GST_STR_NULL(error->message));
    exit(1);
  }
  g_option_context_free(ctx);

  /*get the pipeline */
  GstElement *sinks[MAX_SCREENS];
  global_pipeline = (GstElement *)make_multi_pipeline(sinks, option_screens);

  /*get the windows */
  int i;
  for (i = 0; i < option_screens; i++){
    set_up_window(sinks[i], i + option_first_screen);
  }

  GstStateChangeReturn sret = gst_element_set_state(global_pipeline, GST_STATE_PLAYING);
  if (sret == GST_STATE_CHANGE_FAILURE)
    gst_element_set_state (global_pipeline, GST_STATE_NULL);
  else
    gtk_main();

  gst_object_unref(global_pipeline);
  return 0;
}
