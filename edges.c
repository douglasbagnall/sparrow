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

#define SIG_WEIGHT 2

/*3 pixels manhatten distance makes you an outlier */
#define OUTLIER_THRESHOLD 3 << (SPARROW_FIXED_POINT)
#define OUTLIER_PENALTY 8

/*
*/

#define SPARROW_MAP_LUT_SHIFT 1
#define SPARROW_FP_2_LUT (SPARROW_FIXED_POINT - SPARROW_MAP_LUT_SHIFT)

#define OFFSET(x, y, w)((((y) * (w)) >> SPARROW_FIXED_POINT) + ((x) >> SPARROW_FIXED_POINT))


#define FL_DUMPFILE "/tmp/edges.dump"

static void dump_edges_info(GstSparrow *sparrow, sparrow_find_lines_t *fl, const char *filename){
  GST_DEBUG("about to save to %s\n", filename);
  FILE *f = fopen(filename, "w");
  /* simply write fl, map, clusters and mesh in sequence */
  GST_DEBUG("fl is %p, file is %p\n", fl, f);
  GST_DEBUG("fl: %d x %d\n", sizeof(sparrow_find_lines_t), 1);
  fwrite(fl, sizeof(sparrow_find_lines_t), 1, f);
  GST_DEBUG("fl->map %d x %d\n", sizeof(sparrow_intersect_t), sparrow->in.pixcount);
  fwrite(fl->map, sizeof(sparrow_intersect_t), sparrow->in.pixcount, f);
  GST_DEBUG("fl->clusters  %d x %d\n", sizeof(sparrow_cluster_t), fl->n_hlines * fl->n_vlines);
  fwrite(fl->clusters, sizeof(sparrow_cluster_t), fl->n_hlines * fl->n_vlines, f);
  GST_DEBUG("fl->mesh  %d x %d\n", sizeof(sparrow_corner_t), fl->n_hlines * fl->n_vlines);
  fwrite(fl->mesh, sizeof(sparrow_corner_t), fl->n_hlines * fl->n_vlines, f);
  fclose(f);
}

static void read_edges_info(GstSparrow *sparrow, sparrow_find_lines_t *fl, const char *filename){
  FILE *f = fopen(filename, "r");
  sparrow_find_lines_t fl2;
  size_t read = fread(&fl2, sizeof(sparrow_find_lines_t), 1, f);
  assert(fl2.n_hlines == fl->n_hlines);
  assert(fl2.n_vlines == fl->n_vlines);

  guint n_corners = fl->n_hlines * fl->n_vlines;
  read += fread(fl->map, sizeof(sparrow_intersect_t), sparrow->in.pixcount, f);
  read += fread(fl->clusters, sizeof(sparrow_cluster_t), n_corners, f);
  read += fread(fl->mesh, sizeof(sparrow_corner_t), n_corners, f);
  fclose(f);
}


int sort_median(int *a, guint n)
{
  guint i, j;
  /*stupid sort, but n is very small*/
  for (i = 0; i <  n; i++){
    for (j = i + 1; j < n; j++){
      if (a[i] > a[j]){
        int tmp = a[j];
        a[j] = a[i];
        a[i] = tmp;
      }
    }
  }
  guint middle = n / 2;
  int answer = a[middle];

  if ((n & 1) == 0){
    answer += a[middle - 1];
    answer /= 2;
  }
  return answer;
}





static void corners_to_lut(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  //DEBUG_FIND_LINES(fl);
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
          int iy = mesh_square->in_y + mmy * mesh_square->dyd;
          int ix = mesh_square->in_x + mmy * mesh_square->dxd;
          int ii = OFFSET(ix, iy, in_w);
          int ii_end = OFFSET(ix + (LINE_PERIOD - 1) * mesh_square->dxr,
              iy + (LINE_PERIOD - 1) * mesh_square->dyr, in_w);
          int start_on = mask[ii];
          int end_on = mask[ii_end];
          if(start_on && end_on){
            /*add the point, maybe switch on */
            if (row->start == row->end){/* if both are 0 */
              row->start = mcx * LINE_PERIOD;
            }
            p->x = ix;
            p->y = iy;
            p->dx = mesh_square->dxr;
            p->dy = mesh_square->dyr;
            p++;
          }
          else if (start_on){
            /*add the point, switch off somewhere in the middle*/
            for (x = 1; x < LINE_PERIOD; x++){
              iy += mesh_square->dyr;
              ix += mesh_square->dxr;
              ii = OFFSET(ix, iy, in_w);
              if (mask[ii]){
                /*point is not in the same column with the others,
                  but sparrow knows this because the row->start says so */
                row->start = mcx + x;
                p->x = ix;
                p->y = iy;
                p->dx = mesh_square->dxr;
                p->dy = mesh_square->dyr;
                p++;
                break;
              }
            }
          }
          else if (end_on){
            /* add some, switch off */
            for (x = 1; x < LINE_PERIOD; x++){
              iy += mesh_square->dyr;
              ix += mesh_square->dxr;
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
  //DEBUG_FIND_LINES(fl);
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
        int iy = mesh_square->in_y + mmy * mesh_square->dyd;
        int ix = mesh_square->in_x + mmy * mesh_square->dxd;
        for (mmx = 0; mmx < LINE_PERIOD; mmx++, i++){
          map_lut[i].x = ix >> SPARROW_FP_2_LUT;
          map_lut[i].y = iy >> SPARROW_FP_2_LUT;
          ix += mesh_square->dxr;
          iy += mesh_square->dyr;
        }
        mesh_square++;
      }
    }
    mesh_row += mesh_w;
  }
  sparrow->map_lut = map_lut;
}

#define DIV ((double)(1 << SPARROW_FIXED_POINT))
#define INTXY(x)((x) / (1 << SPARROW_FIXED_POINT))
#define FLOATXY(x)(((double)(x)) / (1 << SPARROW_FIXED_POINT))
static void
debug_corners_image(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  sparrow_corner_t *mesh = fl->mesh;
  guint32 *data = (guint32*)fl->debug->imageData;
  guint w = fl->debug->width;
  memset(data, 0, sparrow->in.size);
  guint32 colours[4] = {0xff0000ff, 0x0000ff00, 0x00ff0000, 0xcccccccc};
  for (int i = 0; i < fl->n_vlines * fl->n_hlines; i++){
    sparrow_corner_t *c = &mesh[i];
    int x = c->in_x;
    int y = c->in_y;
    GST_DEBUG("i %d used %d x: %f, y: %f  dxr %f dyr %f dxd %f dyd %f\n"
        "int x, y %d,%d (raw %d,%d) data %p\n",
        i, c->used, FLOATXY(x), FLOATXY(y),
        FLOATXY(c->dxr), FLOATXY(c->dyr), FLOATXY(c->dxd), FLOATXY(c->dyd),
        INTXY(x), INTXY(y), x, y, data);
    int txr = x;
    int txd = x;
    int tyr = y;
    int tyd = y;
    for (int j = 1; j < LINE_PERIOD; j++){
      txr += c->dxr;
      txd += c->dxd;
      tyr += c->dyr;
      tyd += c->dyd;
      data[INTXY(tyr) * w + INTXY(txr)] = 0x000088;
      data[INTXY(tyd) * w + INTXY(txd)] = 0x663300;
    }
#if 0
#define LP8 (LINE_PERIOD / 8)
#define LP4 (LINE_PERIOD / 4)
#define LP2 (LINE_PERIOD / 2)
    data[INTXY(y + c->dyr * LP8) * w + INTXY(x + c->dxr * LP8)] = 0xbbbbbbbb;
    data[INTXY(y + c->dyr * LP4) * w + INTXY(x + c->dxr * LP4)] = 0xaaaaaaaa;
    data[INTXY(y + c->dyr * LP2) * w + INTXY(x + c->dxr * LP2)] = 0x99999999;
    data[INTXY(y + c->dyd * LP8) * w + INTXY(x + c->dxd * LP8)] = 0xbb6666bb;
    data[INTXY(y + c->dyd * LP4) * w + INTXY(x + c->dxd * LP4)] = 0xaa5555aa;
    data[INTXY(y + c->dyd * LP2) * w + INTXY(x + c->dxd * LP2)] = 0x99444499;
#endif
    data[INTXY(y) * w + INTXY(x)] = colours[MIN(c->used, 2)];
  }
  MAYBE_DEBUG_IPL(fl->debug);
}


static void
debug_clusters(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  //sparrow_cluster_t *clusters = fl->clusters;
}

/*signal product is close to 18 bits. reduce to 4 */
#define SIGNAL_QUANT (1 << 14)

/*maximum number of pixels in a cluster */
#define CLUSTER_SIZE 8


/*create the mesh */
static void
find_corners_make_clusters(GstSparrow *sparrow, guint8 *in, sparrow_find_lines_t *fl){
  sparrow_cluster_t *clusters = fl->clusters;
  int x, y;
  /*each point in fl->map is in a vertical line, a horizontal line, both, or
    neither.  Only the "both" case matters. */
  for (y = 0; y < sparrow->in.height; y++){
    for (x = 0; x < sparrow->in.width; x++){
      sparrow_intersect_t *p = &fl->map[y * sparrow->in.width + x];
      guint vsig = p->signal[SPARROW_VERTICAL];
      guint hsig = p->signal[SPARROW_HORIZONTAL];
      /*remembering that 0 is valid as a line number, but not as a signal */
      if (! (vsig && hsig)){
        continue;
      }
      /*This one is lobbying for the position of a corner.*/
      int vline = p->lines[SPARROW_VERTICAL];
      int hline = p->lines[SPARROW_HORIZONTAL];

      sparrow_cluster_t *cluster = &clusters[hline * fl->n_vlines + vline];
      sparrow_voter_t *voters = cluster->voters;
      int n = cluster->n;
      guint signal = (vsig * hsig) / SIGNAL_QUANT;
      int xfp = x << SPARROW_FIXED_POINT;
      int yfp = y << SPARROW_FIXED_POINT;

      GST_DEBUG("signal at %p (%d, %d): %dv %dh, product %u, lines: %dv %dh\n"
          "cluster is %p, n is %d\n", p, x, y,
          vsig, hsig, signal, vline, hline, cluster, n);

      if (n < CLUSTER_SIZE){
        voters[n].x = xfp;
        voters[n].y = yfp;
        voters[n].signal = signal;
        cluster->n++;
      }
      else {
        for (int j = 0; j < CLUSTER_SIZE; j++){
          if (voters[j].signal < signal){
            guint tmp_s = voters[j].signal;
            int tmp_x = voters[j].x;
            int tmp_y = voters[j].y;
            voters[j].signal = signal;
            voters[j].x = xfp;
            voters[j].y = yfp;
            signal = tmp_s;
            xfp = tmp_x;
            yfp = tmp_y;
            GST_DEBUG("more than %d pixels at cluster for corner %d, %d.\n",
                CLUSTER_SIZE, vline, hline);
          }
        }
      }
    }
  }
}

/* look for connected group. if there is more than one connected group,
 despair.*/

static inline void
discard_cluster_outliers(sparrow_cluster_t *cluster)
{}


static inline void
x_discard_cluster_outliers(sparrow_cluster_t *cluster)
{
  sparrow_voter_t *v = cluster->voters;
  int i, j;

  guint32 touch[CLUSTER_SIZE];

  while (cluster->n){
    guint32 all = (1 << cluster->n) - 1;
    for (i = 0; i < cluster->n; i++){
      touch[i] = 1 << i;
    }

    for (i = 0; i <  cluster->n - 1; i++){
      for (j = i + 1; j < cluster->n; j++){
        if (((abs(v[i].x - v[j].x) <= 2) && (abs(v[i].y - v[j].y) <= 2)) ||
            (touch[i] & touch[j])){
          touch[i] |= touch[j];
          touch[j] = touch[i];
        }
      }
    }
    if (touch[cluster->n - 1] == all){
      break;
    }
    /* something is wrong! somebody is disconnected!  expel them!?
       backpropagate connectedness, find the maximum popcount, discard the
       others. */
    int bcount = 0;
    guint bmask = 0;

    for (i = cluster->n - 1; i >= 0; i++){
      if (bmask != touch[i] &&
          bcount < (int)popcount32(touch[i])){
        bmask = touch[i];
        bcount = popcount32(touch[i]);
      }
      for (j = 0; j < i; j++){
        touch[j] = (touch[j] & touch[i]) ? touch[i] : touch[j];
      }
    }
    if (bcount > cluster->n / 2){
      j = 0;
      for (i = 0; i < cluster->n; i++){
        if (touch[i] == bmask){
          v[j] = v[i];
          j++;
        }
      }
      cluster->n = j;
    }
  }
}



/*create the mesh */
static inline void
find_corners_make_corners(GstSparrow *sparrow, guint8 *in, sparrow_find_lines_t *fl){
  //DEBUG_FIND_LINES(fl);
  int width = fl->n_vlines;
  int height = fl->n_hlines;
  sparrow_cluster_t *clusters = fl->clusters;
  sparrow_corner_t *mesh = fl->mesh;
  int x, y, i;

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

      discard_cluster_outliers(cluster);

      int xsum, ysum;
      int xmean, ymean;
      int votes;
      int j;
      xsum = 0;
      ysum = 0;
      votes = 0;
      for (j = 0; j < cluster->n; j++){
        votes += cluster->voters[j].signal;
        ysum += cluster->voters[j].y * cluster->voters[j].signal;
        xsum += cluster->voters[j].x * cluster->voters[j].signal;
      }
      xmean = xsum / votes;
      ymean = ysum / votes;
      GST_DEBUG("corner %d: %d voters, %d votes, sum %d,%d, mean %d,%d\n",
          i, cluster->n, votes, xsum, ysum, xmean, ymean);

      mesh[i].in_x = xmean;
      mesh[i].in_y = ymean;
      mesh[i].used = TRUE;
      GST_DEBUG("found corner %d at (%3f, %3f)\n",
          i, FLOATXY(xmean), FLOATXY(ymean));
    }
  }
}

static inline void
find_corners_make_map(GstSparrow *sparrow, guint8 *in, sparrow_find_lines_t *fl){
  int i;
  int width = fl->n_vlines;
  int height = fl->n_hlines;
  sparrow_corner_t *mesh = fl->mesh;
  gint x, y;

  //DEBUG_FIND_LINES(fl);
  /* calculate deltas toward adjacent corners */
  /* try to extrapolate left and up, if possible, so need to go backwards. */
  i = width * height - 1;
  for (y = height - 1; y >= 0; y--){
    for (x = width - 1; x >= 0; x--, i--){
      sparrow_corner_t *corner = &mesh[i];
      /* edge deltas will always come out as zero */
      sparrow_corner_t *right = (x >= width - 1) ? corner : corner + 1;
      sparrow_corner_t *down =  (y >= height - 1) ? corner : corner + width;
      GST_DEBUG("i %d xy %d,%d width %d. in_xy %d,%d; down in_xy %d,%d; right in_xy %d,%d\n",
          i, x, y, width, corner->in_x, corner->in_y, down->in_x,
          down->in_y, right->in_x,  right->in_y);
      if (corner->used){
        corner->dxr = (right->in_x - corner->in_x) / LINE_PERIOD;
        corner->dyr = (right->in_y - corner->in_y) / LINE_PERIOD;
        corner->dxd = (down->in_x -  corner->in_x) / LINE_PERIOD;
        corner->dyd = (down->in_y -  corner->in_y) / LINE_PERIOD;
      }
      else {
          /*prefer copy from left unless it is itself reconstructed (for no
            great reason), or it has no dx/dy because it is an edge piece.
            A mixed copy would be possible and better */
        sparrow_corner_t *rsrc = (right->used &&
                (right->used <= down->used) &&
                (right != corner)) ? right : down;
        sparrow_corner_t *dsrc = (down->used &&
                (right->used >= down->used) &&
                (down != corner)) ? down : right;
          corner->dxr = rsrc->dxr;
          corner->dyr = rsrc->dyr;
          corner->dxd = dsrc->dxd;
          corner->dyd = dsrc->dyd;
          /*now extrapolate position, preferably from both left and right */
          int cx = 0, cy = 0, cc = 0;
          if (right != corner){
            cc = 1;
            cx = right->in_x - corner->dxr * LINE_PERIOD;
            cy = right->in_y - corner->dyr * LINE_PERIOD;
          }
          if (down != corner){
            cx += down->in_x - corner->dxd * LINE_PERIOD;
            cy += down->in_y - corner->dyd * LINE_PERIOD;
            cx >>= cc;
            cy >>= cc;
          }
          /* if neither right nor down are there, this
             corner can't be placed */
          corner->in_x = cx;
          corner->in_y = cy;
          corner->used = MAX(right->used, down->used) + 1;
      }
    }
  }
}

static void
find_corners(GstSparrow *sparrow, guint8 *in, sparrow_find_lines_t *fl){
  find_corners_make_clusters(sparrow, in, fl);
  if (sparrow->debug){
    debug_clusters(sparrow, fl);
  }
  find_corners_make_corners(sparrow, in, fl);
  find_corners_make_map(sparrow, in, fl);
  if (sparrow->debug){
    DEBUG_FIND_LINES(fl);
    debug_corners_image(sparrow, fl);
  }
}


/* With no line drawn (in our colour) look at the background noise.  Any real
   signal has to be stringer than this.

   XXX looking for simple maximum -- maybe heap or histogram might be better,
   so as to be less susceptible to wierd outliers (e.g., bad pixels).  */
static void
look_for_threshold(GstSparrow *sparrow, guint8 *in, sparrow_find_lines_t *fl){
  //DEBUG_FIND_LINES(fl);
  int i;
  guint32 colour;
  guint32 cmask = sparrow->out.colours[sparrow->colour];
  guint32 signal;
  guint32 *in32 = (guint32 *)in;
  guint32 highest = 0;
  for (i = 0;  i < (int)sparrow->in.pixcount; i++){
    colour = in32[i] & cmask;
    signal = ((colour >> fl->shift1) +
        (colour >> fl->shift2)) & 0x1ff;
    if (signal > highest){
      highest = signal;
    }
  }
  fl->threshold = highest + 1;
  GST_DEBUG("found maximum noise of %d, using threshold %d\n", highest, fl->threshold);
}


static void
look_for_line(GstSparrow *sparrow, guint8 *in, sparrow_find_lines_t *fl,
    sparrow_line_t *line){
  guint i;
  guint32 colour;
  guint32 cmask = sparrow->out.colours[sparrow->colour];
  int signal;
  guint32 *in32 = (guint32 *)in;
  for (i = 0; i < sparrow->in.pixcount; i++){
    colour = in32[i] & cmask;
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

static void
debug_map_image(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  guint32 *data = (guint32*)fl->debug->imageData;
  memset(data, 0, sparrow->in.size);
  for (guint i = 0; i < sparrow->in.pixcount; i++){
    data[i] |= fl->map[i].signal[SPARROW_HORIZONTAL] << sparrow->in.gshift;
    data[i] |= fl->map[i].signal[SPARROW_VERTICAL] << sparrow->in.rshift;
  }
  MAYBE_DEBUG_IPL(fl->debug);
}

/* draw the line (in sparrow->colour) */
static inline void
draw_line(GstSparrow * sparrow, sparrow_line_t *line, guint8 *out){
  guint32 *p = (guint32 *)out;
  guint32 colour = sparrow->out.colours[sparrow->colour];
  int i;
  if (line->dir == SPARROW_HORIZONTAL){
    p += line->offset * sparrow->out.width;
    for (i = 0; i < sparrow->out.width; i++){
      p[i] = colour;
    }
  }
  else {
    guint32 *p = (guint32 *)out;
    p += line->offset;
    for(i = 0; i < sparrow->out.height; i++){
      *p = colour;
      p += sparrow->out.width;
    }
  }
}


/* show each line for 2 frames, then wait sparrow->lag frames, leaving line on
   until last one.
 */

INVISIBLE sparrow_state
mode_find_edges(GstSparrow *sparrow, guint8 *in, guint8 *out){
  sparrow_find_lines_t *fl = (sparrow_find_lines_t *)sparrow->helper_struct;
  //DEBUG_FIND_LINES(fl);
  if (fl->current == fl->n_lines){
    goto done;
  }
  sparrow_line_t *line = fl->shuffled_lines[fl->current];

  sparrow->countdown--;
  memset(out, 0, sparrow->out.size);
  if (sparrow->countdown){
    /* show the line except on the first round, when we find a threshold*/
    if (fl->threshold){
      GST_DEBUG("current %d line %p\n", fl->current, line);
      draw_line(sparrow, line, out);
    }
  }
  else{
      /*show nothing, look for result */
    if (fl->threshold){
      look_for_line(sparrow, in, fl, line);
      fl->current++;
    }
    else {
      look_for_threshold(sparrow, in, fl);
    }
    sparrow->countdown = sparrow->lag + 2;
  }
  if (sparrow->debug){
    debug_map_image(sparrow, fl);
  }
  return SPARROW_STATUS_QUO;
 done:
  /*match up lines and find corners */
  find_corners(sparrow, in, fl);
  corners_to_lut(sparrow, fl);
  return SPARROW_NEXT_STATE;
}


INVISIBLE void
finalise_find_edges(GstSparrow *sparrow){
  sparrow_find_lines_t *fl = (sparrow_find_lines_t *)sparrow->helper_struct;
  //DEBUG_FIND_LINES(fl);
  if (sparrow->save && *(sparrow->save)){
    GST_DEBUG("about to save to %s\n", sparrow->save);
    dump_edges_info(sparrow, fl, sparrow->save);
  }
  if (sparrow->debug){
    cvReleaseImage(&fl->debug);
  }
  free(fl->h_lines);
  free(fl->shuffled_lines);
  free(fl->map);
  free(fl->mesh);
  free(fl->clusters);
  free(fl);
  sparrow->helper_struct = NULL;
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
  GST_DEBUG("shuffled lines, malloced %p\n", fl->shuffled_lines);

  fl->map = zalloc_aligned_or_die(sizeof(sparrow_intersect_t) * sparrow->in.pixcount);
  fl->clusters = zalloc_or_die(n_corners * sizeof(sparrow_cluster_t));
  fl->mesh = zalloc_aligned_or_die(n_corners * sizeof(sparrow_corner_t));

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
  //DEBUG_FIND_LINES(fl);

  GST_DEBUG("allocated %d lines, used %d\n", n_lines, line - fl->h_lines);

  /*now shuffle */
  for (i = 0; i < n_lines - 1; i++){
    int j = RANDINT(sparrow, 0, n_lines);
    sparrow_line_t *tmp = fl->shuffled_lines[j];
    fl->shuffled_lines[j] = fl->shuffled_lines[i];
    fl->shuffled_lines[i] = tmp;
  }

  setup_colour_shifts(sparrow, fl);

  if (sparrow->reload && *(sparrow->reload)){
    read_edges_info(sparrow, fl, sparrow->reload);
    sparrow->countdown = 2;
  }
  else {
    sparrow->countdown = sparrow->lag + 2;
  }

  //DEBUG_FIND_LINES(fl);
  if (sparrow->debug){
    CvSize size = {sparrow->in.width, sparrow->in.height};
    fl->debug = cvCreateImage(size, IPL_DEPTH_8U, PIXSIZE);
  }
}

