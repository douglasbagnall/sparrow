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
#include "edges.h"

#include <string.h>
#include <math.h>

#include "cv.h"

/*
*/

#define SIG_WEIGHT 5

/*3 pixels manhatten distance makes you an outlier */
#define OUTLIER_THRESHOLD 3 << (SPARROW_FIXED_POINT)
#define OUTLIER_PENALTY 8

/*
*/


#define SPARROW_MAP_LUT_SHIFT 1
#define SPARROW_FP_2_LUT (SPARROW_FIXED_POINT - SPARROW_MAP_LUT_SHIFT)

#define OFFSET(x, y, w)((((y) * (w)) >> SPARROW_FIXED_POINT) + ((x) >> SPARROW_FIXED_POINT))

static void corners_to_lut(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  sparrow_map_t *map = &sparrow->map; /*rows in sparrow->out */
  guint8 *mask = sparrow->screenmask; /*mask in sparrow->in */
  sparrow_corner_t *mesh = fl->mesh;   /*maps regular points in ->out to points in ->in */

  int mesh_w = fl->n_vlines;
  int mesh_h = fl->n_hlines;
  int in_w = sparrow->in.width;
  int mcy, mmy, mcx; /*Mesh Corner|Modulus X|Y*/

  int x;
  sparrow_map_row_t *row = map->rows;
  sparrow_map_point_t *p = map->point_mem;
  sparrow_corner_t *mesh_row = mesh;
  for(mcy = 0; mcy < mesh_h; mcy++){
    for (mmy = 0; mmy < LINE_PERIOD; mmy++){
      sparrow_corner_t *mesh_square = mesh_row;
      row->points = p;
      row->start = 0;
      row->end = 0;
      for(mcx = 0; mcx < mesh_w; mcx++){
        if (mesh_square->used){
          int iy = mesh_square->in_y + mmy * mesh_square->dyv;
          int ix = mesh_square->in_x + mmy * mesh_square->dxv;
          int ii = OFFSET(ix, iy, in_w);
          int ii_end = OFFSET(ix + (LINE_PERIOD - 1) * mesh_square->dxh,
              iy + (LINE_PERIOD - 1) * mesh_square->dyh, in_w);
          int start_on = mask[ii];
          int end_on = mask[ii_end];
          if(start_on && end_on){
            /*add the point, maybe switch on */
            if (row->start == row->end){/* if both are 0 */
              row->start = mcx * LINE_PERIOD;
            }
            p->x = ix;
            p->y = iy;
            p->dx = mesh_square->dxh;
            p->dy = mesh_square->dyh;
            p++;
          }
          else if (start_on){
            /*add the point, switch off somewhere in the middle*/
            for (x = 1; x < LINE_PERIOD; x++){
              iy += mesh_square->dyh;
              ix += mesh_square->dxh;
              ii = OFFSET(ix, iy, in_w);
              if (mask[ii]){
                /*point is not in the same column with the others,
                  but sparrow knows this because the row->start says so */
                row->start = mcx + x;
                p->x = ix;
                p->y = iy;
                p->dx = mesh_square->dxh;
                p->dy = mesh_square->dyh;
                p++;
                break;
              }
            }
          }
          else if (end_on){
            /* add some, switch off */
            for (x = 1; x < LINE_PERIOD; x++){
              iy += mesh_square->dyh;
              ix += mesh_square->dxh;
              ii = OFFSET(ix, iy, in_w);
              if (! mask[ii]){
                row->end = mcx + x;
                break;
              }
            }
          }
          else {
            /*3 cases:
              start > end: this is first off pixel.
              start == end: row hasn't started (both 0)
              start < end: both are set -- row is done
            */
            if (row->start > row->end){
              row->end = mcx * LINE_PERIOD;
            }
            else if (row->start < row->end){
              break;
            }
          }
        }
        mesh_square++;
      }
      row++;
    }
    mesh_row += mesh_w;
  }
}

UNUSED static void
corners_to_full_lut(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  sparrow_corner_t *mesh = fl->mesh;   /*maps regular points in ->out to points in ->in */
  sparrow_map_lut_t *map_lut = sparrow->map_lut;
  int mesh_w = fl->n_vlines;
  int mesh_h = fl->n_hlines;
  int mcy, mmy, mcx, mmx; /*Mesh Corner|Modulus X|Y*/
  int i = 0;
  sparrow_corner_t *mesh_row = mesh;
  for(mcy = 0; mcy < mesh_h; mcy++){
    for (mmy = 0; mmy < LINE_PERIOD; mmy++){
      sparrow_corner_t *mesh_square = mesh_row;
      for(mcx = 0; mcx < mesh_w; mcx++){
        int iy = mesh_square->in_y + mmy * mesh_square->dyv;
        int ix = mesh_square->in_x + mmy * mesh_square->dxv;
        for (mmx = 0; mmx < LINE_PERIOD; mmx++, i++){
          map_lut[i].x = ix >> SPARROW_FP_2_LUT;
          map_lut[i].y = iy >> SPARROW_FP_2_LUT;
          ix += mesh_square->dxh;
          iy += mesh_square->dyh;
        }
        mesh_square++;
      }
    }
    mesh_row += mesh_w;
  }
  sparrow->map_lut = map_lut;
}



/*create the mesh */

static void
find_corners(GstSparrow *sparrow, guint8 *in, sparrow_find_lines_t *fl){
  int i;
  int width = fl->n_vlines;
  int height = fl->n_hlines;
  sparrow_cluster_t *clusters = fl->clusters;
  sparrow_corner_t *corners = fl->corners;
  gint x, y;

  for (y = 0; y < sparrow->in.height; y++){
    for (x = 0; x < sparrow->in.width; x++){
      sparrow_intersect_t *p = &fl->map[y * sparrow->in.width + x];
      /*remembering that 0 is valid as a line no, but not as a signal */
      if (! p->signal[SPARROW_HORIZONTAL] ||
          ! p->signal[SPARROW_VERTICAL]){
        continue;
      }
      /*This one is lobbying for the position of a corner.*/

      /*XXX what to do in the case that there is no intersection?  often cases
        this will happen in the dark bits and be OK. But if it happens in the
        light?*/
      /*linearise the xy coordinates*/
      int vline = p->lines[SPARROW_VERTICAL];
      int hline = p->lines[SPARROW_HORIZONTAL];
      sparrow_cluster_t *cluster = &clusters[vline * height + hline];

      int n = cluster->n;
      if (n < 8){
        cluster->voters[n].x = x << SPARROW_FIXED_POINT;
        cluster->voters[n].y = y << SPARROW_FIXED_POINT;
        cluster->voters[n].signal = (SIG_WEIGHT + p->signal[SPARROW_HORIZONTAL]) *
          (SIG_WEIGHT + p->signal[SPARROW_VERTICAL]);
        cluster->n++;
        /*these next two could of course be computed from the offset */
      }
      else {
        GST_DEBUG("more than 8 pixels at cluster for corner %d, %d\n",
            vline, hline);
        /*if this message ever turns up, replace the weakest signals or add
          more slots.*/
      }
    }
  }

  i = 0;
  for (y = 0; y < height; y++){
    for (x = 0; x < width; x++, i++){
      /* how to do this?
         1. centre of gravity (x,y, weighted average)
         2. discard outliers? look for connectedness? but if 2 are outliers?
      */
      sparrow_cluster_t *cluster = clusters + i;
      if (cluster->n == 0){
        continue;
      }
      int xsum, ysum;
      int xmean, ymean;
      int votes;
      while(1) {
        int j;
        xsum = 0;
        ysum = 0;
        votes = 0;
        for (j = 0; j < cluster->n; j++){
          votes += cluster->voters[j].signal;
          ysum += cluster->voters[j].y * cluster->voters[j].signal;
          xsum += cluster->voters[j].x * cluster->voters[j].signal;
        }
        if (votes == 0){
          /* don't diminish signal altogether. The previous iteration's means
             will be used. */
          break;
        }
        xmean = xsum / votes;
        ymean = ysum / votes;
        int worst = -1;
        int worstn;
        int devsum = 0;
        for (j = 0; j < cluster->n; j++){
          int xdiff = abs(cluster->voters[j].x - xmean);
          int ydiff = abs(cluster->voters[j].y - ymean);
          devsum += xdiff + ydiff;
          if (xdiff + ydiff > worst){
            worst = xdiff + ydiff;
            worstn = j;
          }
        }
        /*a bad outlier has significantly greater than average deviation
          (but how much is bad? median deviation would be more useful)*/
        if (worst > 3 * devsum / cluster->n){
          /* reduce the worst ones weight. it is a silly aberration. */
          cluster->voters[worstn].signal /= OUTLIER_PENALTY;
          GST_DEBUG("dropping outlier at %s,%s (mean %s,%s)\n",
              cluster->voters[worstn].x, cluster->voters[worstn].y, xmean, ymean);
          continue;
        }
        break;
      }
      corners[i].out_x = x * LINE_PERIOD;
      corners[i].out_y = y * LINE_PERIOD;
      corners[i].in_x = xmean;
      corners[i].in_y = ymean;
      corners[i].used = TRUE;
      double div = (double)(1 << SPARROW_FIXED_POINT); /*for printf only*/
      GST_DEBUG("found corner %d (%d,%d) at (%3f, %3f)\n",
          i, corners[i].out_x, corners[i].out_y,
          xmean / div, ymean / div);
    }
  }
  free(clusters);

  /* calculate deltas toward adjacent corners */
  /* try to extrapolate left and up, if possible, so need to go backwards. */
  for (y = height - 2; y >= 0; y--){
    for (x = width - 2; x >= 0; x--){
      i = y * width + x;
      if (corners[i].used){
        corners[i].dxh = (corners[i + 1].in_x - corners[i].in_x) / LINE_PERIOD;
        corners[i].dyh = (corners[i + 1].in_y - corners[i].in_y) / LINE_PERIOD;
        corners[i].dxv = (corners[i + width].in_x - corners[i].in_x) / LINE_PERIOD;
        corners[i].dyv = (corners[i + width].in_y - corners[i].in_y) / LINE_PERIOD;
      }
      else {
          /*prefer copy from left unless it is itself reconstructed,
            (for no great reason)
            A mixed copy would be possible and better */
        if(corners[i + 1].used &&
            (corners[i + 1].used < corners[i + width].used)){
          corners[i].dxh = corners[i + 1].dxh;
          corners[i].dyh = corners[i + 1].dyh;
          corners[i].dxv = corners[i + 1].dxv;
          corners[i].dyv = corners[i + 1].dyv;
          corners[i].in_x = corners[i + 1].in_x - corners[i + 1].dxh * LINE_PERIOD;
          corners[i].in_y = corners[i + 1].in_y - corners[i + 1].dyh * LINE_PERIOD;
          corners[i].out_x = corners[i + 1].out_x - 1;
          corners[i].out_y = corners[i + 1].out_y;
          corners[i].used = corners[i + 1].used + 1;
        }
        else if(corners[i + width].used){
          corners[i].dxh = corners[i + width].dxh;
          corners[i].dyh = corners[i + width].dyh;
          corners[i].dxv = corners[i + width].dxv;
          corners[i].dyv = corners[i + width].dyv;
          corners[i].in_x = corners[i + width].in_x - corners[i + width].dxv * LINE_PERIOD;
          corners[i].in_y = corners[i + width].in_y - corners[i + width].dyv * LINE_PERIOD;
          corners[i].out_x = corners[i + width].out_x;
          corners[i].out_y = corners[i + width].out_y - 1;
          corners[i].used = corners[i + width].used + 1;
        }
      }
    }
  }

  fl->mesh = corners;
}


/* With no line drawn (in our colour) look at the background noise.  Any real
   signal has to be stringer than this.

   XXX looking for simple maximum -- maybe heap or histogram might be better,
   so as to be less susceptible to wierd outliers (e.g., bad pixels).  */
static void
look_for_threshold(GstSparrow *sparrow, guint8 *in, sparrow_find_lines_t *fl){
  int i;
  guint32 colour;
  guint32 signal;
  guint32 *in32 = (guint32 *)in;
  guint32 highest = 0;
  for (i = 0;  i < (int)sparrow->in.size; i++){
    colour = in32[i] & sparrow->colour;
    signal = ((colour >> fl->shift1) +
        (colour >> fl->shift2)) & 0x1ff;
    if (signal > highest){
      highest = signal;
    }
  }
  fl->threshold = highest + 10;
  GST_DEBUG("found maximum noise of %s, using threshold %s\n", highest, fl->threshold);
}


static void
look_for_line(GstSparrow *sparrow, guint8 *in, sparrow_find_lines_t *fl,
    sparrow_line_t *line){
  guint i;
  guint32 colour;
  int signal;
  guint32 *in32 = (guint32 *)in;
  for (i = 0; i < sparrow->in.size; i++){
    colour = in32[i] & sparrow->colour;
    signal = ((colour >> fl->shift1) +
        (colour >> fl->shift2)) & 0x1ff;
    if (signal > fl->threshold){
      if (fl->map[i].lines[line->dir]){
        GST_DEBUG("HEY, expected point %d to be in line %d (dir %d)"
            "and thus empty, but it is also in line %d\n",
            i, line->index, line->dir, fl->map[i].lines[line->dir]);
      }
      fl->map[i].lines[line->dir] = line->index;
      fl->map[i].signal[line->dir] = signal;
    }
  }
}


/* draw the line (in sparrow->colour) */
static inline void
draw_line(GstSparrow * sparrow, sparrow_line_t *line, guint8 *out){
  guint32 *p = (guint32 *)out;
  int i;
  if (line->dir == SPARROW_HORIZONTAL){
    p += line->offset * sparrow->out.width;
    for (i = 0; i < sparrow->out.width; i++){
      p[i] = sparrow->colour;
    }
  }
  else {
    guint32 *p = (guint32 *)out;
    p += line->offset;
    for(i = 0; i < sparrow->out.height; i++){
      *p = sparrow->colour;
      p += sparrow->out.width;
    }
  }
}


/* show each line for 2 frames, then wait sparrow->lag frames, leaving line on
   until last one.
 */

INVISIBLE sparrow_state
mode_find_edges(GstSparrow *sparrow, guint8 *in, guint8 *out){
  sparrow_find_lines_t *fl = (sparrow_find_lines_t *)&sparrow->helper_struct;
  sparrow_line_t *line = fl->shuffled_lines[fl->current];
  sparrow->countdown--;
  memset(out, 0, sparrow->out.size);
  if (sparrow->countdown){
    /* show the line except on the first round, when we find a threshold*/
    if (fl->threshold){
      draw_line(sparrow, line, out);
    }
  }
  else{
      /*show nothing, look for result */
    if (fl->threshold){
      if (fl->current == fl->n_lines){
        goto done;
      }
      look_for_line(sparrow, in, fl, line);
      fl->current++;
    }
    else {
      look_for_threshold(sparrow, in, fl);
    }
    sparrow->countdown = sparrow->lag + 2;
  }
  return SPARROW_STATUS_QUO;
 done:
  /*match up lines and find corners */
  find_corners(sparrow, in, fl);
  corners_to_lut(sparrow, fl);

  /*free stuff!, including fl, and reset pointer to NULL*/

  return SPARROW_NEXT_STATE;
}

static void
finalise_find_edges(GstSparrow *sparrow){
  sparrow_find_lines_t *fl = (sparrow_find_lines_t *)&sparrow->helper_struct;
  free(fl->h_lines);
  free(fl->shuffled_lines);
  free(fl->map);
  free(fl->mesh);
  free(fl->clusters);
  free(fl->corners);
  free(fl);
}


static void
setup_colour_shifts(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  switch (sparrow->colour){
  case SPARROW_WHITE:
  case SPARROW_GREEN:
    fl->shift1 = sparrow->in.gshift;
    fl->shift2 = sparrow->in.gshift;
    break;
  case SPARROW_MAGENTA:
    fl->shift1 = sparrow->in.rshift;
    fl->shift2 = sparrow->in.bshift;
    break;
  }
}

INVISIBLE void
init_find_edges(GstSparrow *sparrow){
  gint32 w = sparrow->out.width;
  gint32 h = sparrow->out.height;
  gint i;
  sparrow_find_lines_t *fl = zalloc_aligned_or_die(sizeof(sparrow_find_lines_t));
  sparrow->helper_struct = (void *)fl;

  gint h_lines = (h + LINE_PERIOD - 1) / LINE_PERIOD;
  gint v_lines = (w + LINE_PERIOD - 1) / LINE_PERIOD;
  gint n_lines = (h_lines + v_lines);
  gint n_corners = (h_lines * v_lines);
  fl->n_hlines = h_lines;
  fl->n_vlines = v_lines;
  fl->n_lines = n_lines;

  fl->h_lines = malloc_aligned_or_die(sizeof(sparrow_line_t) * n_lines);
  fl->shuffled_lines = malloc_aligned_or_die(sizeof(sparrow_line_t*) * n_lines);
  fl->map = zalloc_aligned_or_die(sizeof(sparrow_intersect_t) * sparrow->in.pixcount);
  fl->clusters = malloc_or_die(n_corners * sizeof(sparrow_cluster_t));
  fl->corners = zalloc_aligned_or_die(n_corners * sizeof(sparrow_corner_t));
  fl->mesh = malloc_aligned_or_die(sizeof(sparrow_corner_t) * h_lines * v_lines);

  sparrow_line_t *line = fl->h_lines;
  sparrow_line_t **sline = fl->shuffled_lines;
  int offset = LINE_PERIOD / 2;

  for (i = 0, offset = LINE_PERIOD / 2; offset < h;
       i++, offset += LINE_PERIOD){
    line->offset = offset;
    line->dir = SPARROW_HORIZONTAL;
    line->index = i;
    *sline = line;
    line++;
    sline++;
  }

  /*now add the vertical lines */
  fl->v_lines = line;
  for (i = 0, offset = LINE_PERIOD / 2; offset < w;
       i++, offset += LINE_PERIOD){
    line->offset = offset;
    line->dir = SPARROW_VERTICAL;
    line->index = i;
    *sline = line;
    line++;
    sline++;
  }

  GST_DEBUG("allocated %s lines, used %s\n", n_lines, line - fl->h_lines);

  /*now shuffle (triangluar, to no particular advantage) */
  for (i = 0; i < n_lines - 1; i++){
    int j = RANDINT(sparrow, i + 1, n_lines);
    sparrow_line_t *tmp = fl->shuffled_lines[j];
    fl->shuffled_lines[j] = fl->shuffled_lines[i];
    fl->shuffled_lines[i] = tmp;
  }

  setup_colour_shifts(sparrow, fl);
  sparrow->countdown = sparrow->lag + 2;
}
