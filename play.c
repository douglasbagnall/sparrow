/* Copyright (C) <2010> Douglas Bagnall <douglas@halo.gen.nz>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "sparrow.h"
#include "gstsparrow.h"

#include <string.h>
#include <math.h>

#define DEBUG_PLAY 0
#define OLD_FRAMES 4


typedef struct sparrow_play_s{
  guint8 lut_hi[256];
  guint8 lut_lo[256];
  guint8 *image_row;
  guint jpeg_index;
  GstBuffer *old_frames[OLD_FRAMES];
  int old_frames_head;
  int old_frames_tail;
} sparrow_play_t;


static void
set_up_jpeg(GstSparrow *sparrow, sparrow_play_t *player){
  /*XXX pick a jpeg, somehow*/
  /*first, chance of random jump anywhere. */
  if (rng_uniform(sparrow) < 1.0 / 500){
    player->jpeg_index = RANDINT(sparrow, 0, sparrow->shared->image_count);
    GST_DEBUG("CHANGE_SHOT: random jump: %d to %d", player->jpeg_index, player->jpeg_index);
  }
  else {
    sparrow_frame_t *old_frame = &sparrow->shared->index[player->jpeg_index];
    int next = old_frame->successors[0];
    if (!next){
      int i = RANDINT(sparrow, 1, 8);
      next = old_frame->successors[i];
      GST_DEBUG("CHANGE_SHOT: %d to %d (option %d)", player->jpeg_index, next, i);
    }
    player->jpeg_index = next;
  }

  sparrow_frame_t *frame = &sparrow->shared->index[player->jpeg_index];
  guint8 *src = sparrow->shared->jpeg_blob + frame->offset;
  guint size = frame->jpeg_size;
  GST_DEBUG("blob is %p, offset %d, src %p, size %d\n",
      sparrow->shared->jpeg_blob, frame->offset, src, size);

  begin_reading_jpeg(sparrow, src, size);
}


static inline guint8 one_subpixel(sparrow_play_t *player, guint8 inpix, guint8 jpegpix){
  //jpeg is target.
  //there is diff. (in gamma space)
  // compensate for diff (only).
  // if the pixel needs to be darker, send nothing
  // if it needs to be brighter, send difference.
  //XXX difference needs gamma scaling
#if 0
  /*clamp in pseudo gamma space*/
  int gj = player->lut_hi[jpegpix];
  int gi = player->lut_hi[inpix];
  int diff = gj - gi;
  if (diff < 0)
    return 0;
  return player->lut_lo[diff];
#endif
#if 1
  /*clamp */
  int diff = jpegpix - inpix;
  if (diff < 0)
    return 0;
  return diff;
#endif
#if 0
  /*full mirror */
  int diff = jpegpix - inpix;
  if (diff < 0)
    return -diff; /*or -diff /2 */
  return diff;
#endif
#if 0
  /* simplest possible (average)*/
  guint sum = player->lut_hi[inpix] + jpegpix;
  return sum >> 1;
#endif
}


UNUSED static inline void
do_one_pixel(GstSparrow *sparrow, guint8 *outpix, guint8 *inpix, guint8 *jpegpix){
  /* rather than cancel the whole other one, we need to calculate the
     difference from the desired image, and only compensate by that
     amount.  If a lot of negative compensation (i.e, trying to blacken)
     is needed, then it is time to raise the black level for the next
     round (otherwise, lower the black level). Use sum of
     compensations?, or count? or thresholded? or squared (via LUT)?

     How are relative calculations calculated via LUT?

     1. pre scale
     2. combine
     3. re scale

  */
  sparrow_play_t *player = sparrow->helper_struct;
  outpix[0] = one_subpixel(player, inpix[0], jpegpix[0]);
  outpix[1] = one_subpixel(player, inpix[1], jpegpix[1]);
  outpix[2] = one_subpixel(player, inpix[2], jpegpix[2]);
  outpix[3] = one_subpixel(player, inpix[3], jpegpix[3]);
}

#define GENTLE 1
#if GENTLE
static inline guint8 one_subpixel_lagged(sparrow_play_t *player, guint8 inpix,
    guint8 jpegpix, guint8 oldpix){
  /*clamp */
  int error = MAX(inpix - oldpix, 0) >> 1;
  int diff = jpegpix - error;
  if (diff < 0)
    return 0;
  return diff;
}

#else
static inline guint8 one_subpixel_lagged(sparrow_play_t *player, guint8 inpix,
    guint8 jpegpix, guint8 oldpix){
  /*clamp */
  //jpegpix -= oldpix >> 1;
  //inpix += oldpix;
  int target = 2 * jpegpix - oldpix;
  int diff = (target - inpix) >> 1;
  if (diff < 0)
    return 0;
  if (diff > 255)
    return 255;
  return diff;
}
#endif

static inline void
do_one_pixel_lagged(sparrow_play_t *player, guint8 *outpix, guint8 *inpix, guint8 *jpegpix,
    guint8 *oldframe){
  outpix[0] = one_subpixel_lagged(player, inpix[0], jpegpix[0], oldframe[0]);
  outpix[1] = one_subpixel_lagged(player, inpix[1], jpegpix[1], oldframe[1]);
  outpix[2] = one_subpixel_lagged(player, inpix[2], jpegpix[2], oldframe[2]);
  outpix[3] = one_subpixel_lagged(player, inpix[3], jpegpix[3], oldframe[3]);
}



static void
play_from_full_lut(GstSparrow *sparrow, guint8 *in, guint8 *out){
  GST_DEBUG("play_from_full_lut\n");
#if 0
  memset(out, 0, sparrow->out.size); /*is this necessary? (only for outside
                                       screen map, maybe in-loop might be
                                       quicker) */
#endif
  sparrow_play_t *player = sparrow->helper_struct;
  guint i;
  int ox, oy;
  guint32 *out32 = (guint32 *)out;
  guint32 *in32 = (guint32 *)in;
  set_up_jpeg(sparrow, player);
  GST_DEBUG("in %p out %p", in, out);

  guint8 *jpeg_row = player->image_row;
  i = 0;
  for (oy = 0; oy < sparrow->out.height; oy++){
    //GST_DEBUG("about to read line to %p", jpeg_row);
    read_one_line(sparrow, jpeg_row);
    for (ox = 0; ox < sparrow->out.width; ox++, i++){
      int x = sparrow->map_lut[i].x;
      int y = sparrow->map_lut[i].y;
      if (x || y){
        guint8 *inpix = (guint8*)&in32[y * sparrow->in.width + x];
        /*do_one_pixel(sparrow,
            &out[i * PIXSIZE],
            inpix,
            &jpeg_row[ox * PIXSIZE]);*/
        do_one_pixel_lagged(player,
            &out[i * PIXSIZE],
            inpix,
            &jpeg_row[ox * PIXSIZE],
            &old_frame[i * PIXSIZE]
        );
      }
      else {
        out32[i] = 0;
      }
    }
  }
  finish_reading_jpeg(sparrow);

  if (DEBUG_PLAY && sparrow->debug){
    debug_frame(sparrow, out, sparrow->out.width, sparrow->out.height, PIXSIZE);
  }
}

static void
store_old_frame(GstSparrow *sparrow, GstBuffer *outbuf){
  sparrow_play_t *player = sparrow->helper_struct;
  GstBuffer *head = player->old_frames[player->old_frames_head];
  if (head){
    head = outbuf;
    gst_buffer_ref(head);
  }
  player->old_frames_head++;
  player->old_frames_head %= OLD_FRAMES;
}

static void
drop_old_frame(GstSparrow *sparrow, GstBuffer *outbuf){
  sparrow_play_t *player = sparrow->helper_struct;
  GstBuffer *tail = player->old_frames[player->old_frames_tail];
  if (tail){
    gst_buffer_unref(tail);
  }
  player->old_frames_tail++;
  player->old_frames_tail %= OLD_FRAMES;
}


INVISIBLE sparrow_state
mode_play(GstSparrow *sparrow, GstBuffer *inbuf, GstBuffer *outbuf){
  guint8 *in = GST_BUFFER_DATA(inbuf);
  guint8 *out = GST_BUFFER_DATA(outbuf);
  store_old_frame(sparrow, outbuf);
  play_from_full_lut(sparrow, in, out);
  drop_old_frame(sparrow, outbuf);
  return SPARROW_STATUS_QUO;
}

static const double GAMMA = 1.5;
static const double INV_GAMMA = 1.0 / 1.5;
static const double FALSE_CEILING = 275;

static void
init_gamma_lut(sparrow_play_t *player){
  for (int i = 0; i < 256; i++){
    /* for each colour:
       1. perform inverse gamma calculation (-> linear colour space)
       2. negate
       3 undo gamma.
     */
    double x;
    x = i / 255.01;
    x = 1 - pow(x, INV_GAMMA);
    x = pow(x, GAMMA) * FALSE_CEILING;
    if (x > 255){
      x = 255;
    }
    player->lut[i] = (guint8)x;
  }
}

INVISIBLE void init_play(GstSparrow *sparrow){
  GST_DEBUG("starting play mode\n");
  init_jpeg_src(sparrow);
  sparrow_play_t *player = zalloc_aligned_or_die(sizeof(sparrow_play_t));
  player->image_row = zalloc_aligned_or_die(sparrow->out.width * PIXSIZE);
  sparrow->helper_struct = player;
  init_gamma_lut(player);
  GST_DEBUG("finished init_play\n");
}

INVISIBLE void finalise_play(GstSparrow *sparrow){
  GST_DEBUG("leaving play mode\n");
}
