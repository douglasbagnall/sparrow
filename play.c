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

typedef struct sparrow_play_s{
  guint8 lut[256];

} sparrow_play_t;


static inline guint8
negate(sparrow_play_t *player, guint8 in){
  return player->lut[in];
}

static void UNUSED
play_from_lut(GstSparrow *sparrow, guint8 *in, guint8 *out){
  sparrow_map_t *map = &sparrow->map;
  memset(out, 0, sparrow->out.size);
  int x, y;
  guint32 *line = (guint32 *)out;
  for (y = 0; y < sparrow->out.height; y++){
    sparrow_map_row_t *row = map->rows + y;
    for(x = row->start; x < row->end; x++){
      line[x] = ~0;
    }
    /*
    GST_DEBUG("row %p %d: s %d e%d line %d\n",
        row, y, row->start, row->end, line);
    */
    line += sparrow->out.width;
  }
}

static void
play_from_full_lut(GstSparrow *sparrow, guint8 *in, guint8 *out){
  memset(out, 0, sparrow->out.size);
  sparrow_play_t *player = sparrow->helper_struct;
  guint i;
  guint32 *out32 = (guint32 *)out;
  guint32 *in32 = (guint32 *)in;
  for (i = 0; i < sparrow->out.pixcount; i++){
    if (sparrow->screenmask[i]){
      int x = sparrow->map_lut[i].x >> SPARROW_MAP_LUT_SHIFT;
      int y = sparrow->map_lut[i].y >> SPARROW_MAP_LUT_SHIFT;
      if (x || y){
        //out32[i] = ~in32[y * sparrow->in.width + x];
        guint8 *ib = (guint8 *)&in32[y * sparrow->in.width + x];
        guint8 *ob = (guint8 *)&out32[i];
        ob[0] = player->lut[ib[0]];
        ob[1] = player->lut[ib[1]];
        ob[2] = player->lut[ib[2]];
        ob[3] = player->lut[ib[3]];
      }
    }
  }
  if (sparrow->debug){
    debug_frame(sparrow, out, sparrow->out.width, sparrow->out.height, PIXSIZE);
  }
}


UNUSED
static void
gamma_negation(GstSparrow *sparrow, guint8 *in, guint8 *out){
  //guint i;
  //XXX  could try oil_tablelookup_u8
  //for (i = 0; i < size; i++){
  //  out[i] = sparrow_rgb_gamma_full_range_REVERSE[in[i]];
  // }
}

INVISIBLE sparrow_state
mode_play(GstSparrow *sparrow, guint8 *in, guint8 *out){
  //do actual stuff here
  //memcpy(out, in, sparrow->out.size);
  //simple_negation(out, sparrow->out.size);
#if USE_FULL_LUT
  play_from_full_lut(sparrow, in, out);
#else
  play_from_lut(sparrow, in, out);
#endif
  return SPARROW_STATUS_QUO;
}

static const double GAMMA = 2.0;
static const double INV_GAMMA = 1.0 / 2.2;
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
  sparrow_play_t *player = zalloc_aligned_or_die(sizeof(sparrow_play_t));
  sparrow->helper_struct = player;
  init_gamma_lut(player);
}

INVISIBLE void finalise_play(GstSparrow *sparrow){
  GST_DEBUG("leaving play mode\n");
}
