/*
 * Based on an example from the clutter-gst package:
 *
 * video-sink.c - A small example around the videotestsrc ! warptv !
 *                ffmpegcolorspace ! cluttersink pipeline.
 *
 * Copyright (C) 2007,2008 OpenedHand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <clutter-gst/clutter-gst.h>
#include "app.h"

void
size_change (ClutterTexture *texture,
             gfloat          width,
             gfloat          height,
             gpointer        user_data)
{
  ClutterActor *stage;
  gfloat new_x, new_y, new_width, new_height;
  gfloat stage_width, stage_height;

  stage = clutter_actor_get_stage (CLUTTER_ACTOR (texture));
  if (stage == NULL)
    return;

  clutter_actor_get_size (stage, &stage_width, &stage_height);

  new_height = (height * stage_width) / width;
  if (new_height <= stage_height)
    {
      new_width = stage_width;

      new_x = 0;
      new_y = (stage_height - new_height) / 2;
    }
  else
    {
      new_width  = (width * stage_height) / height;
      new_height = stage_height;

      new_x = (stage_width - new_width) / 2;
      new_y = 0;
    }

  clutter_actor_set_position (CLUTTER_ACTOR (texture), new_x, new_y);
  clutter_actor_set_size (CLUTTER_ACTOR (texture), new_width, new_height);
}

void
set_initial_size (ClutterTexture *texture)
{
  ClutterActor *stage;
  gfloat new_x, new_y;
  gfloat stage_width, stage_height;
  stage = clutter_actor_get_stage (CLUTTER_ACTOR (texture));
  clutter_actor_get_size (stage, &stage_width, &stage_height);
  new_x = (stage_width - WIDTH) / 2;
  new_y = (stage_height - HEIGHT) / 2;
  clutter_actor_set_position (CLUTTER_ACTOR(texture), new_x, new_y);
  clutter_actor_set_size (CLUTTER_ACTOR(texture), WIDTH, HEIGHT);
}


int
main (int argc, char *argv[])
{
  ClutterTimeline  *timeline;
  ClutterActor     *stage;
  ClutterActor     *texture;
  ClutterColor     black = {0, 0, 0, 255};

  clutter_init (&argc, &argv);
  gst_init (&argc, &argv);

  stage = clutter_stage_get_default ();
  clutter_stage_set_color(CLUTTER_STAGE(stage), &black);
  clutter_stage_set_fullscreen(CLUTTER_STAGE(stage), TRUE);
  clutter_stage_hide_cursor(CLUTTER_STAGE(stage));

  /* Make a timeline */
  timeline = clutter_timeline_new (1000);
  g_object_set(timeline, "loop", TRUE, NULL);

  /* We need to set certain props on the target texture currently for
   * efficient/corrent playback onto the texture (which sucks a bit)
  */
  texture = g_object_new (CLUTTER_TYPE_TEXTURE,
			  "sync-size",       FALSE,
			  "disable-slicing", TRUE,
			  NULL);

  /*
  g_signal_connect (CLUTTER_TEXTURE (texture),
		    "size-change",
		    G_CALLBACK (size_change), NULL);
  */


  GstElement *sink = clutter_gst_video_sink_new(CLUTTER_TEXTURE(texture));

  GstPipeline *pipeline = make_pipeline(sink);

  gst_element_set_state (GST_ELEMENT(pipeline), GST_STATE_PLAYING);

  /* start the timeline */
  clutter_timeline_start (timeline);

  clutter_group_add (CLUTTER_GROUP (stage), texture);
  // clutter_actor_set_opacity (texture, 0x11);
  clutter_actor_show_all (stage);

  set_initial_size(CLUTTER_TEXTURE(texture));
  clutter_main();

  return 0;
}
