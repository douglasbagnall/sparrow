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
#include "play_core.h"

#define one_subpixel one_subpixel_gamma_clamp_oldpix

static inline void
do_one_pixel(sparrow_play_t *player, guint8 *outpix, guint8 *inpix, guint8 *jpegpix,
    guint8 *oldframe){
  outpix[0] = one_subpixel(player, inpix[0], jpegpix[0], oldframe[0]);
  outpix[1] = one_subpixel(player, inpix[1], jpegpix[1], oldframe[1]);
  outpix[2] = one_subpixel(player, inpix[2], jpegpix[2], oldframe[2]);
  outpix[3] = one_subpixel(player, inpix[3], jpegpix[3], oldframe[3]);
}

static inline int
select_jpeg_adaptive(GstSparrow *sparrow, sparrow_play_t *player){
  /*
  GST_DEBUG("in select_jpeg_adaptive, starting at %d", player->jpeg_index);
  GstBuffer *oldbuf = player->old_frames[player->old_frames_tail];
  if (! oldbuf){
    GST_DEBUG("old buffer is not there! using random jump: %d to %d",
        player->jpeg_index, player->jpeg_index);
    return RANDINT(sparrow, 0, sparrow->shared->image_count);
  }
  guint8 *old_frame = (guint8 *)GST_BUFFER_DATA(oldbuf);

  cvResize( const CvArr* src, CvArr* dst, CV_INTER_LINEAR)

  int x, y;
  guint32 summary[6 * 8 * PIXSIZE] = {0};
  int xdiv = sparrow->out.width / 8;
  int ydiv = sparrow->out.height / 6;
  int y2 = 0;
  int x2 = 0;
  for (y2 = 0; y2 < 6; y2++){
    for (y = y2 * ydiv; y < (y2 + 1) * ydiv; y += 4){
      for (x2 = 0; x2 < 8; x2++){
        for (x = x2 * xdiv; x < (x2 + 1) * xdiv; x++){
          int i2 = y2 * 8 + x2;
          int i = y * sparrow->out.width + x;

          summary[i2 + ] += old_frame[];
        }
      }
    }

    for (x = 0; x < sparrow->out.width; x++){




  GST_DEBUG("in select_jpeg_adaptive");
  */
  return RANDINT(sparrow, 0, sparrow->shared->image_count);
}


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
      if (i < 8){
        next = old_frame->successors[i];
        GST_DEBUG("CHANGE_SHOT: %d to %d (option %d)", player->jpeg_index, next, i);
      }
      else{
        next = select_jpeg_adaptive(sparrow, player);
      }
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

static void
play_from_full_lut(GstSparrow *sparrow, guint8 *in, guint8 *out){
  sparrow_play_t *player = sparrow->helper_struct;
  guint i;
  int ox, oy;
  guint32 *out32 = (guint32 *)out;
  guint32 *in32 = (guint32 *)in;
  GstBuffer *oldbuf = player->old_frames[player->old_frames_tail];
  guint8 *old_frame = (oldbuf) ? (guint8 *)GST_BUFFER_DATA(oldbuf) : out;

  set_up_jpeg(sparrow, player);

  guint8 *jpeg_row = player->image_row;
  i = 0;
  for (oy = 0; oy < sparrow->out.height; oy++){
    read_one_line(sparrow, jpeg_row);
    for (ox = 0; ox < sparrow->out.width; ox++, i++){
      guint inpos = sparrow->map_lut[i];
      if (inpos){
        do_one_pixel(player,
            &out[i * PIXSIZE],
            (guint8 *)&in32[inpos],
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
  player->old_frames[player->old_frames_head] = outbuf;
  gst_buffer_ref(outbuf);
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


static void
init_gamma_lut(sparrow_play_t *player){
  /* for each colour:
     1. perform inverse gamma calculation (-> linear colour space)
     2. negate
     3 undo gamma.
  */
  for (int i = 0; i < 256; i++){
    double x;
    x = (double)i / 255;
    x = pow(x, GAMMA) * (GAMMA_UNIT_LIMIT - 1);
    if (x >= GAMMA_UNIT_LIMIT){
      x = GAMMA_UNIT_LIMIT - 1;
    }
    player->lut_f[i] = (guint16)x;
  }
  for (int i = 0; i < GAMMA_UNIT_LIMIT; i++){
    double x;
    x = (double)i / (GAMMA_UNIT_LIMIT - 1);
    x = pow(x, INV_GAMMA) * 255 + 0.5;
    if (x > 255){
      x = 255;
    }
    player->lut_b[i] = (guint8)x;
  }
  /*add some extra on the table to catch overflow -- should perhaps ramp
    toward 255, if the top is not that high*/
  for (int i = GAMMA_UNIT_LIMIT; i < GAMMA_TABLE_TOP; i++){
    player->lut_b[i] = 255;
  }
}

INVISIBLE void init_play(GstSparrow *sparrow){
  GST_DEBUG("starting play mode\n");
  init_jpeg_src(sparrow);
  sparrow_play_t *player = zalloc_aligned_or_die(sizeof(sparrow_play_t));
  player->image_row = zalloc_aligned_or_die(sparrow->out.width * PIXSIZE);
  player->old_frames_head = MIN(sparrow->lag, OLD_FRAMES - 1) || 1;
  GST_INFO("using old frame lag of %d\n", player->old_frames_head);
  sparrow->helper_struct = player;
  init_gamma_lut(player);
  GST_DEBUG("finished init_play\n");
}

INVISIBLE void finalise_play(GstSparrow *sparrow){
  GST_DEBUG("leaving play mode\n");
}
