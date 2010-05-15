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

#include "cv.h"

#define LINE_PERIOD 16



/* draw the line (in sparrow->colour) */
static inline void
draw_line(GstSparrow * sparrow, sparrow_line_t *line, guint8 *out){
  guint32 *p = (guint32 *)out;
  guint i;
  if (line->dir == SPARROW_HORIZONTAL){
    p += line->offset * sparrow->out.width;
    for (i = 0; i < sparrow->out.width; i++){
      p[i] = sparrow->colour;
    }
  }
  else {
    guint32 *p = (guint32 *)out;
    p += line->offset;
    for(i = 0; i < (guint)(sparrow->out.height); i++){
      *p = sparrow->colour;
      p += sparrow->out.width;
    }
  }
}

#define SPARROW_LINE_THRESHOLD 32

static void
look_for_line(GstSparrow *sparrow, guint8 *in, sparrow_find_lines_t *fl, sparrow_line_t *line){
  int i;
  guint32 colour;
  guint32 signal;
  guint32 *in32 = (guint32 *)in;
  line->points = fl->points + fl->n_points;
  line->n_points = 0;

  /*switch (sparrow->out.colour){
  case SPARROW_WHITE:
  case SPARROW_GREEN:

    break;
  case SPARROW_MAGENTA:

  }*/
  for (i = 0;  i < sparrow->in.size; i++){
    colour = in32[i] & sparrow->colour;

  /* could be cleverer here: in the case of green, there is only one byte,
     set; with magenta, two. use switch outside loop?
  */
  /* also, no overflow is possible when adding the bytes
     t = (colour & 0x00ff00ff) + ((colour & 0xff00ff00) >> 8)
     t = (t + (t >> 16)) &0xffff

     XXX and magenta will be twice as strong as green
 */
    signal = (((colour & 0xff)) +
        ((colour >> 8) & 0xff) +
        ((colour >> 16)& 0xff) +
        (colour >> 24));
    /* threshold can be set dynamically --> look at colour before showing anything. */
    if (signal > SPARROW_LINE_THRESHOLD){
      /*add to the points list */
      line->points[line->n_points].offset = i;
      line->points[line->n_points].signal = signal;
      line->n_points++;
    }
  }

  fl->n_points += line->n_points;
  /*check for overflow? */
}


/* show each line for 2 frames, then wait sparrow->lag frames, leaving line on
   until last one.
 */

INVISIBLE sparrow_state
mode_find_edges(GstSparrow *sparrow, guint8 *in, guint8 *out){
  sparrow_find_lines_t *fl = &sparrow->findlines;
  sparrow_line_t *line = fl->shuffled_lines[fl->current];
  sparrow->countdown--;
  memset(out, 0, sparrow->out.size);
  if (sparrow->countdown){
    /* show the line */
    draw_line(sparrow, line, out);
  }
  else{
    /*show nothing, look for result */
    look_for_line(sparrow, in, fl, line);


    fl->current++;
    if (fl->current == fl->n_lines){
      goto done;
    }
  }
  return SPARROW_STATUS_QUO;
 done:
  /*free stuff!*/
  return SPARROW_NEXT_STATE;
}



INVISIBLE void
init_find_edges(GstSparrow *sparrow){
  gint32 w = sparrow->out.width;
  gint32 h = sparrow->out.height;
  gint i;
  sparrow_find_lines_t *fl = &sparrow->findlines;
  gint h_lines = (h + LINE_PERIOD - 1) / LINE_PERIOD;
  gint v_lines = (w + LINE_PERIOD - 1) / LINE_PERIOD;
  gint n_lines = (h_lines + v_lines);

  fl->h_lines = malloc_aligned_or_die(sizeof(sparrow_line_t) * n_lines);
  fl->shuffled_lines = malloc_aligned_or_die(sizeof(*sparrow_line_t) * n_lines);

  sparrow_line_t *line = fl->h_lines;
  sparrow_line_t **sline = fl->shuffled_lines;
  for (i = LINE_PERIOD / 2; i < h; i += LINE_PERIOD){
    line->offset = i;
    line->dir = SPARROW_HORIZONTAL;
    line->n_points = 0;
    line->points = NULL;
    sline = &line;
    line++;
    sline++;
  }
  /*now add the vertical lines */
  fl->v_lines = line;
  for (i = LINE_PERIOD / 2; i < w; i += LINE_PERIOD){
    line->offset = i;
    line->dir = SPARROW_VERTICAL;
    line->n_points = 0;
    line->points = NULL;
    sline = &line;
    line++;
    sline++;
  }
  n_lines = line - fl->h_lines;
  GST_DEBUG("allocated %s lines, used %s\n", h_lines + v_lines, n_lines);

  /*now shuffle (triangluar, to no particular advantage) */
  for (i = 0; i < n_lines - 1; i++){
    int j = RAND_INT(i + 1, n_lines);
    sparrow_line_t *tmp = fl->shuffled_lines[j];
    fl->shuffled_lines[j] = fl->shuffled_lines[i];
    fl->shuffled_lines[i] = tmp;
  }
  fl->current = 0;
  fl->n_lines = n_lines;
  sparrow->countdown = sparrow->lag + 2;
}
