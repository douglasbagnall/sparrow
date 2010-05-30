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


static void
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
    //GST_DEBUG("row %d: s %d e%d", y, row->start, row->end);
    line += sparrow->out.width;
  }
}


UNUSED
static void
simple_negation(guint8 * bytes, guint size){
  guint i;
  guint32 * data = (guint32 *)bytes;
  //could use sse for superspeed
  for (i = 0; i < size / 4; i++){
    data[i] = ~data[i];
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
  play_from_lut(sparrow, in, out);
  return SPARROW_STATUS_QUO;
}

INVISIBLE void init_play(GstSparrow *sparrow){}
INVISIBLE void finalise_play(GstSparrow *sparrow){}
