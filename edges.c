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

/* for discarding outliers */
#define OUTLIER_FIXED_POINT 4
#define OUTLIER_RADIUS 7
#define OUTLIER_THRESHOLD ((OUTLIER_RADIUS * OUTLIER_RADIUS) << (OUTLIER_FIXED_POINT * 2))

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


static inline int sort_median(int *a, guint n)
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

debug_lut(GstSparrow *sparrow, sparrow_find_lines_t *fl){
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
        if (mesh_square->status != CORNER_UNUSED){
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
  debug_lut(sparrow, fl);
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
  guint32 colours[4] = {0xff0000ff, 0x00ff0000, 0x0000ff00, 0xcccccccc};
  for (int i = 0; i < fl->n_vlines * fl->n_hlines; i++){
    sparrow_corner_t *c = &mesh[i];
    int x = c->in_x;
    int y = c->in_y;
    GST_DEBUG("i %d status %d x: %f, y: %f  dxr %f dyr %f dxd %f dyd %f\n"
        "int x, y %d,%d (raw %d,%d) data %p\n",
        i, c->status, FLOATXY(x), FLOATXY(y),
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
    data[INTXY(y) * w + INTXY(x)] = colours[c->status];
  }
  MAYBE_DEBUG_IPL(fl->debug);
}


static void
debug_clusters(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  guint32 *data = (guint32*)fl->debug->imageData;
  memset(data, 0, sparrow->in.size);
  int width = fl->n_vlines;
  int height = fl->n_hlines;
  sparrow_cluster_t *clusters = fl->clusters;
  int i, j;
  guint32 colour;
  guint32 colours[4] = {0xff0000ff, 0x0000ff00, 0x00ff0000,
                       0x00ff00ff};
  for (i = 0; i < width * height; i++){
    colour = colours[i % 5];
    sparrow_voter_t *v = clusters[i].voters;
    for (j = 0; j < clusters[i].n; j++){
      data[(v[j].y >> SPARROW_FIXED_POINT) * sparrow->in.width +
          (v[j].x >> SPARROW_FIXED_POINT)] = (colour * (v[j].signal / 2)) / 256;
    }
  }
  MAYBE_DEBUG_IPL(fl->debug);
}

/*signal product is close to 18 bits. reduce to 4 */
#define SIGNAL_QUANT (1 << 14)

/*maximum number of pixels in a cluster */
#define CLUSTER_SIZE 8


/*create the mesh */
static void
make_clusters(GstSparrow *sparrow, sparrow_find_lines_t *fl){
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
      if (signal == 0){
        GST_WARNING("signal at %p (%d, %d) is %d following quantisation!\n",
            p, x, y, signal);
      }

      if (n < CLUSTER_SIZE){
        voters[n].x = xfp;
        voters[n].y = yfp;
        voters[n].signal = signal;
        cluster->n++;
      }
      else {
        guint tmp_s;
        for (int j = 0; j < CLUSTER_SIZE; j++){
          if (voters[j].signal < signal){
            tmp_s = voters[j].signal;
            int tmp_x = voters[j].x;
            int tmp_y = voters[j].y;
            voters[j].signal = signal;
            voters[j].x = xfp;
            voters[j].y = yfp;
            signal = tmp_s;
            xfp = tmp_x;
            yfp = tmp_y;
          }
        }
        GST_DEBUG("more than %d pixels at cluster for corner %d, %d."
            "Dropped %u for %u\n",
            CLUSTER_SIZE, vline, hline, signal, tmp_s, signal);
      }
    }
  }
  if (sparrow->debug){
    debug_clusters(sparrow, fl);
  }
}

/* look for connected group. if there is more than one connected group,
 despair.*/

static inline void
drop_cluster_voter(sparrow_cluster_t *cluster, int n)
{
  if (n < cluster->n){
    for (int i = n; i < cluster->n - 1; i++){
      cluster->voters[i] = cluster->voters[i + 1];
    }
    cluster->n--;
  }
}

static inline void
median_discard_cluster_outliers(sparrow_cluster_t *cluster)
{
  int xvals[CLUSTER_SIZE];
  int yvals[CLUSTER_SIZE];
  int i;
  for (i = 0; i < cluster->n; i++){
    /*XXX could sort here*/
    xvals[i] = cluster->voters[i].x;
    yvals[i] = cluster->voters[i].y;
  }
  const int xmed = sort_median(xvals, cluster->n);
  const int ymed = sort_median(yvals, cluster->n);

  for (i = 0; i < cluster->n; i++){
    /*dx, dy can be as  much as 1024 << 8 ==  1 << 18. squared = 1 << 36.
     shift it back to  1<< 14*/
    int dx = abs(cluster->voters[i].x - xmed) >> (SPARROW_FIXED_POINT - OUTLIER_FIXED_POINT);
    int dy = abs(cluster->voters[i].y - ymed) >> (SPARROW_FIXED_POINT - OUTLIER_FIXED_POINT);
    if (dx *dx + dy * dy > OUTLIER_THRESHOLD){
      drop_cluster_voter(cluster, i);
    }
  }
}

/*create the mesh */
static inline void
make_corners(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  //DEBUG_FIND_LINES(fl);
  int width = fl->n_vlines;
  int height = fl->n_hlines;
  sparrow_cluster_t *clusters = fl->clusters;
  sparrow_corner_t *mesh = fl->mesh;
  int x, y, i;

  i = 0;
  for (y = 0; y < height; y++){
    for (x = 0; x < width; x++, i++){
      sparrow_cluster_t *cluster = clusters + i;
      if (cluster->n == 0){
        continue;
      }

      /*the good points should all be adjacent; distant ones are spurious, so
        are discarded. */
      median_discard_cluster_outliers(cluster);

      /* now find a weighted average position */
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
      if (votes){
        xmean = xsum / votes;
        ymean = ysum / votes;
      }
      else {
        GST_WARNING("corner %d, %d voters, sum %d,%d, somehow has no votes\n",
            i, cluster->n, xsum, ysum);
      }

      GST_DEBUG("corner %d: %d voters, %d votes, sum %d,%d, mean %d,%d\n",
          i, cluster->n, votes, xsum, ysum, xmean, ymean);

      mesh[i].in_x = xmean;
      mesh[i].in_y = ymean;
      mesh[i].status = CORNER_EXACT;
      GST_DEBUG("found corner %d at (%3f, %3f)\n",
          i, FLOATXY(xmean), FLOATXY(ymean));
    }
  }
}

static inline void
make_map(GstSparrow *sparrow, sparrow_find_lines_t *fl){
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
      /* calculate the delta to next corner. If this corner is on edge, delta is
       0 and next is this.*/
      sparrow_corner_t *right = (x >= width - 1) ? corner : corner + 1;
      sparrow_corner_t *down =  (y >= height - 1) ? corner : corner + width;
      GST_DEBUG("i %d xy %d,%d width %d. in_xy %d,%d; down in_xy %d,%d; right in_xy %d,%d\n",
          i, x, y, width, corner->in_x, corner->in_y, down->in_x,
          down->in_y, right->in_x,  right->in_y);
      if (corner->status != CORNER_UNUSED){
        corner->dxr = (right->in_x - corner->in_x) / LINE_PERIOD;
        corner->dyr = (right->in_y - corner->in_y) / LINE_PERIOD;
        corner->dxd = (down->in_x -  corner->in_x) / LINE_PERIOD;
        corner->dyd = (down->in_y -  corner->in_y) / LINE_PERIOD;
      }
      else {
          /*copy from both right and down, if they both exist. */
        struct {
          int dxr;
          int dyr;
          int dxd;
          int dyd;
        } dividends = {0, 0, 0, 0};
        struct {
          int r;
          int d;
        } divisors = {0, 0};

        if (right != corner){
          if (right->dxr || right->dyr){
            dividends.dxr += right->dxr;
            dividends.dyr += right->dyr;
            divisors.r++;
          }
          if (right->dxd || right->dyd){
            dividends.dxd += right->dxd;
            dividends.dyd += right->dyd;
            divisors.d++;
          }
        }
        if (down != corner){
          if (down->dxr || down->dyr){
            dividends.dxr += down->dxr;
            dividends.dyr += down->dyr;
            divisors.r++;
          }
          if (down->dxd || down->dyd){
            dividends.dxd += down->dxd;
            dividends.dyd += down->dyd;
            divisors.d++;
          }
        }
        corner->dxr = divisors.r ? dividends.dxr / divisors.r : 0;
        corner->dyr = divisors.r ? dividends.dyr / divisors.r : 0;
        corner->dxd = divisors.d ? dividends.dxd / divisors.d : 0;
        corner->dyd = divisors.d ? dividends.dyd / divisors.d : 0;

        /*now extrapolate position, preferably from both left and right */
        if (right == corner){
          if (down != corner){ /*use down only */
            corner->in_x = down->in_x - corner->dxd * LINE_PERIOD;
            corner->in_y = down->in_y - corner->dyd * LINE_PERIOD;
          }
          else {/*oh no*/
            GST_DEBUG("can't reconstruct corner %d, %d: no useable neighbours\n", x, y);
            /*it would be easy enough to look further, but hopefully of no
              practical use */
          }
        }
        else if (down == corner){ /*use right only */
          corner->in_x = right->in_x - corner->dxr * LINE_PERIOD;
          corner->in_y = right->in_y - corner->dyr * LINE_PERIOD;
        }
        else { /* use both */
          corner->in_x = right->in_x - corner->dxr * LINE_PERIOD;
          corner->in_y = right->in_y - corner->dyr * LINE_PERIOD;
          corner->in_x += down->in_x - corner->dxd * LINE_PERIOD;
          corner->in_y += down->in_y - corner->dyd * LINE_PERIOD;
          corner->in_x >>= 1;
          corner->in_y >>= 1;
        }
        corner->status = CORNER_PROJECTED;
      }
    }
  }
  if (sparrow->debug){
    DEBUG_FIND_LINES(fl);
    debug_corners_image(sparrow, fl);
  }
}



static void
look_for_line(GstSparrow *sparrow, guint8 *in, sparrow_find_lines_t *fl,
    sparrow_line_t *line){
  guint i;
  guint32 colour;
  guint32 cmask = sparrow->out.colours[sparrow->colour];
  int signal;

  /* subtract background noise */
  fl->input->imageData = (char *)in;
  cvSub(fl->input, fl->threshold, fl->working, NULL);
  guint32 *in32 = (guint32 *)fl->working->imageData;

  for (i = 0; i < sparrow->in.pixcount; i++){
    colour = in32[i] & cmask;
    signal = ((colour >> fl->shift1) +
        (colour >> fl->shift2)) & 0x1ff;
    if (signal){
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
    data[i] |= (fl->map[i].signal[SPARROW_HORIZONTAL] >> 1) << sparrow->in.gshift;
    data[i] |= (fl->map[i].signal[SPARROW_VERTICAL] >> 1) << sparrow->in.rshift;
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
static inline void
draw_lines(GstSparrow *sparrow, sparrow_find_lines_t *fl, guint8 *in, guint8 *out)
{
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
    look_for_line(sparrow, in, fl, line);
    fl->current++;
  }
  sparrow->countdown = sparrow->lag + 2;
  if (sparrow->debug){
    debug_map_image(sparrow, fl);
  }
}

#define LINE_THRESHOLD 10

static inline void
find_threshold(GstSparrow *sparrow, sparrow_find_lines_t *fl, guint8 *in, guint8 *out)
{
  memset(out, 0, sparrow->out.size);
  /*XXX should average/median over a range of frames */
  if (fl->counter == -1){
    memcpy(fl->threshold->imageData, in, sparrow->in.size);
    /*add a constant, and smooth */
    cvAddS(fl->threshold, cvScalarAll(LINE_THRESHOLD), fl->working, NULL);
    //cvSmooth(tmp, fl->threshold_im, CV_GAUSSIAN, 3, 0, 0, 0);
    cvSmooth(fl->working, fl->threshold, CV_MEDIAN, 3, 0, 0, 0);
  }
}

INVISIBLE sparrow_state
mode_find_edges(GstSparrow *sparrow, guint8 *in, guint8 *out){
  sparrow_find_lines_t *fl = (sparrow_find_lines_t *)sparrow->helper_struct;
  //DEBUG_FIND_LINES(fl);
  if (fl->counter < 0){
    find_threshold(sparrow, fl, in, out);
    fl->counter++;
    goto OK;
  }
  if (fl->current < fl->n_lines){
    draw_lines(sparrow, fl, in, out);
    goto OK;
  }
  /*match up lines and find corners */
  switch(fl->counter){
  case 0:
    make_clusters(sparrow, fl);
    break;
  case 1:
    make_corners(sparrow, fl);
    break;
  case 2:
    make_map(sparrow, fl);
    break;
  case 3:
    corners_to_lut(sparrow, fl);
    goto finished;
  }
  fl->counter++;
 OK:
  return SPARROW_STATUS_QUO;
 finished:
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
  cvReleaseImage(&fl->threshold);
  cvReleaseImage(&fl->working);
  cvReleaseImageHeader(&fl->input);
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

  GST_DEBUG("allocated %d lines, status %d\n", n_lines, line - fl->h_lines);

  /*now shuffle */
  for (i = 0; i < n_lines - 1; i++){
    int j = RANDINT(sparrow, 0, n_lines);
    sparrow_line_t *tmp = fl->shuffled_lines[j];
    fl->shuffled_lines[j] = fl->shuffled_lines[i];
    fl->shuffled_lines[i] = tmp;
  }

  setup_colour_shifts(sparrow, fl);

  if (sparrow->reload && *(sparrow->reload)){
    GST_DEBUG("sparrow>reload is %s\n", sparrow->reload);
    read_edges_info(sparrow, fl, sparrow->reload);
    memset(fl->map, 0, sizeof(sparrow_intersect_t) * sparrow->in.pixcount);
    //memset(fl->clusters, 0, n_corners * sizeof(sparrow_cluster_t));
    memset(fl->mesh, 0, n_corners * sizeof(sparrow_corner_t));

    fl->current = fl->n_lines;
    fl->counter = 0;
  }
  else {
    sparrow->countdown = sparrow->lag + 2;
    fl->counter = -(sparrow->lag + 2);
  }

  /* opencv images for threshold finding */
  CvSize size = {sparrow->in.width, sparrow->in.height};
  fl->working = cvCreateImage(size, IPL_DEPTH_8U, PIXSIZE);
  fl->threshold = cvCreateImage(size, IPL_DEPTH_8U, PIXSIZE);

  /*input has no data allocated -- it uses latest frame*/
  fl->input = init_ipl_image(&sparrow->in, PIXSIZE);

  //DEBUG_FIND_LINES(fl);
  if (sparrow->debug){
    fl->debug = cvCreateImage(size, IPL_DEPTH_8U, PIXSIZE);
  }
}

