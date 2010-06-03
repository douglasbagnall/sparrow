/*
Largely based on an example by Tristan Matthews
http://tristanswork.blogspot.com/2008/09/fullscreen-video-in-gstreamer-with-gtk.html

*/
#include <gst/gst.h>
#include <gtk/gtk.h>
#include <gst/interfaces/xoverlay.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include "app.h"

static void
bus_call(GstBus * bus, GstMessage *msg, gpointer data)
{
  GtkWidget *window = (GtkWidget*) data;
  if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ELEMENT &&
      gst_structure_has_name(msg->structure, "prepare-xwindow-id")){
    g_print("Got prepare-xwindow-id msg\n");
    gst_x_overlay_set_xwindow_id(GST_X_OVERLAY(GST_MESSAGE_SRC(msg)),
        GDK_WINDOW_XWINDOW(window->window));
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


gint main (gint argc, gchar *argv[])
{
  /* initialization */
  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  GMainLoop *loop = g_main_loop_new(NULL, FALSE);
  GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_window_set_default_size(GTK_WINDOW(window), WIDTH, HEIGHT);
  //gtk_window_fullscreen(GTK_WINDOW(widget));
  //GdkScreen *screen = gdk_display_get_screen(0|1);
  //gtk_window_set_screen(GTK_WINDOW(window), screen);

  GstElement *sink = gst_element_factory_make("ximagesink", "sink");
  GstElement *pipeline = (GstElement *)make_pipeline(sink);

  gst_bus_add_watch(gst_pipeline_get_bus(GST_PIPELINE(pipeline)),
      (GstBusFunc)bus_call, window);

  // attach key press signal to key press callback
  gtk_widget_set_events(window, GDK_KEY_PRESS_MASK);
  g_signal_connect(G_OBJECT(window), "key-press-event", G_CALLBACK(key_press_event_cb), NULL);
  g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(destroy_cb), loop);


  GdkColor black = {0, 0, 0, 0};
  gtk_widget_modify_bg(window, GTK_STATE_NORMAL, &black);

  gtk_widget_show_all(window);

  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  g_main_loop_run(loop);


  /* clean up */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return 0;
}

