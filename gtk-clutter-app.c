
#include <gst/gst.h>
#include <gtk/gtk.h>
#include <gst/interfaces/xoverlay.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <clutter-gst/clutter-gst.h>
#include <clutter-gtk/clutter-gtk.h>
#include <clutter/clutter.h>
#include <clutter/x11/clutter-x11.h>


#include "app.h"

typedef struct windows_s {
  GstElement *sinks[2];
  ClutterActor *stages[2];
  XID windows[2];
  int n;
} windows_t;


static void
bus_call(GstBus * bus, GstMessage *msg, gpointer data)
{
  windows_t *w = (windows_t *)data;
  if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ELEMENT &&
      gst_structure_has_name(msg->structure, "prepare-xwindow-id")){
    g_print("Got prepare-xwindow-id msg\n");
    for (int i = 0; i < 2; i++){
      clutter_x11_set_stage_foreign(CLUTTER_STAGE(w->stages[i]), w->windows[i]);
    }
  }
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
  g_print("got key %c\n", event->keyval);
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

void destroy_cb(GtkWidget * widget, gpointer data)
{
  GMainLoop *loop = (GMainLoop*) data;
  g_print("Window destroyed\n");
  g_main_loop_quit(loop);
}

static void
video_widget_realize_cb (GtkWidget * widget, gpointer data)
{
  windows_t *w = (windows_t *)data;
  if (w->n < 2){
    w->windows[w->n] = GDK_WINDOW_XID(GDK_WINDOW(widget->window));
  }
  else {
    g_print("wtf, there seem to be %d windows!\n", w->n);
  }
  w->n++;
  return;
}


static void
set_up_window(GMainLoop *loop, GtkWidget *window){
  static const GdkColor black = {0, 0, 0, 0};
  gtk_window_set_default_size(GTK_WINDOW(window), WIDTH, HEIGHT);

  //gtk_window_fullscreen(GTK_WINDOW(widget));
  //GdkScreen *screen = gdk_display_get_screen(0|1);
  //gtk_window_set_screen(GTK_WINDOW(window), screen);

  // attach key press signal to key press callback
  gtk_widget_set_events(window, GDK_KEY_PRESS_MASK);
  g_signal_connect(G_OBJECT(window), "key-press-event", G_CALLBACK(key_press_event_cb), NULL);
  g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(destroy_cb), loop);

  gtk_widget_modify_bg(window, GTK_STATE_NORMAL, &black);
  gtk_widget_show_all(window);
}


ClutterActor *
new_stage(){
  static const ClutterColor black = {0, 0, 0, 255};
  ClutterActor *stage = clutter_stage_get_default();
  clutter_stage_set_color(CLUTTER_STAGE(stage), &black);
  clutter_stage_set_fullscreen(CLUTTER_STAGE(stage), TRUE);
  clutter_stage_hide_cursor(CLUTTER_STAGE(stage));
  return stage;
}


gint main (gint argc, gchar *argv[])
{
  /* initialization */
  gst_init (&argc, &argv);
  gtk_init(&argc, &argv);
  clutter_init(&argc, &argv);

  /* herein we count windows and map them to sinks */
  windows_t windows;
  windows.n = 0;

  GMainLoop *loop = g_main_loop_new(NULL, FALSE);
  GtkWidget *window1 = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  GtkWidget *window2 = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_signal_connect (window1, "realize",
      G_CALLBACK (video_widget_realize_cb), &windows);
  g_signal_connect (window2, "realize",
      G_CALLBACK (video_widget_realize_cb), &windows);

  /* Make a timeline */
  ClutterTimeline  *timeline = clutter_timeline_new(1000);
  g_object_set(timeline, "loop", TRUE, NULL);

  ClutterActor *stage1 = new_stage();
  ClutterTexture *texture1 = g_object_new (CLUTTER_TYPE_TEXTURE,
      "sync-size",       FALSE,
      "disable-slicing", TRUE,
      NULL);
  GstElement *sink1 = clutter_gst_video_sink_new(CLUTTER_TEXTURE(texture1));

  ClutterActor *stage2 = new_stage();
  ClutterTexture *texture2 = g_object_new (CLUTTER_TYPE_TEXTURE,
      "sync-size",       FALSE,
      "disable-slicing", TRUE,
      NULL);
  GstElement *sink2 = clutter_gst_video_sink_new(CLUTTER_TEXTURE(texture2));


  /*To avoid flickering on show, you should call gtk_widget_show() or
    gtk_widget_realize() before calling clutter_actor_show() */

  //GstElement *sink1 = gst_element_factory_make("ximagesink", "sink1");
  //GstElement *sink2 = gst_element_factory_make("ximagesink", "sink2");

  GstElement *pipeline = (GstElement *)make_dual_pipeline(sink1, sink2);

  windows.sinks[0] = sink1;
  windows.sinks[1] = sink2;
  windows.stages[0] = stage1;
  windows.stages[1] = stage2;

  set_up_window(loop, window1);
  set_up_window(loop, window2);

  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  gst_bus_add_watch(bus, (GstBusFunc)bus_call, &windows);
  gst_object_unref(bus);

  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  /* start the timeline */
  clutter_timeline_start(timeline);
  clutter_group_add(CLUTTER_GROUP(stage1), texture1);
  clutter_group_add(CLUTTER_GROUP(stage2), texture2);
  clutter_actor_show_all(stage1);
  clutter_actor_show_all(stage2);

  g_main_loop_run(loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return 0;
}

