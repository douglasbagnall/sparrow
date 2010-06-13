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
#include <unistd.h>

#include "cv.h"
#include "median.h"

static int global_number_of_edge_finders = 0;

static void dump_edges_info(GstSparrow *sparrow, sparrow_find_lines_t *fl, const char *filename){
  GST_DEBUG("about to save to %s\n", filename);
  FILE *f = fopen(filename, "w");
  sparrow_fl_condensed_t condensed;
  condensed.n_vlines = fl->n_vlines;
  condensed.n_hlines = fl->n_hlines;

  /* simply write fl, map, clusters and mesh in sequence */
  GST_DEBUG("fl is %p, file is %p\n", fl, f);
  GST_DEBUG("fl: %d x %d\n", sizeof(sparrow_find_lines_t), 1);
  fwrite(&condensed, sizeof(sparrow_fl_condensed_t), 1, f);
  GST_DEBUG("fl->map %d x %d\n", sizeof(sparrow_intersect_t), sparrow->in.pixcount);
  fwrite(fl->map, sizeof(sparrow_intersect_t), sparrow->in.pixcount, f);
  GST_DEBUG("fl->clusters  %d x %d\n", sizeof(sparrow_cluster_t), fl->n_hlines * fl->n_vlines);
  fwrite(fl->clusters, sizeof(sparrow_cluster_t), fl->n_hlines * fl->n_vlines, f);
  GST_DEBUG("fl->mesh  %d x %d\n", sizeof(sparrow_corner_t), fl->n_hlines * fl->n_vlines);
  fwrite(fl->mesh, sizeof(sparrow_corner_t), fl->n_hlines * fl->n_vlines, f);
  /*and write the mask too */
  GST_DEBUG("sparrow->screenmask\n");
  fwrite(sparrow->screenmask, 1, sparrow->in.pixcount, f);
  fclose(f);
}

static void read_edges_info(GstSparrow *sparrow, sparrow_find_lines_t *fl, const char *filename){
  FILE *f = fopen(filename, "r");
  sparrow_fl_condensed_t condensed;
  size_t read = fread(&condensed, sizeof(sparrow_fl_condensed_t), 1, f);
  assert(condensed.n_hlines == fl->n_hlines);
  assert(condensed.n_vlines == fl->n_vlines);

  guint n_corners = fl->n_hlines * fl->n_vlines;
  read += fread(fl->map, sizeof(sparrow_intersect_t), sparrow->in.pixcount, f);
  read += fread(fl->clusters, sizeof(sparrow_cluster_t), n_corners, f);
  read += fread(fl->mesh, sizeof(sparrow_corner_t), n_corners, f);
  read += fread(sparrow->screenmask, 1, sparrow->in.pixcount, f);
  fclose(f);
}

static void
debug_map_lut(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  sparrow_map_lut_t *map_lut = sparrow->map_lut;
  if (sparrow->debug){
    debug_frame(sparrow, (guint8*)map_lut, sparrow->out.width, sparrow->out.height, PIXSIZE);
  }
}

#if USE_FLOAT_COORDS

#define COORD_TO_INT(x)((int)((x) + 0.5))
#define COORD_TO_FLOAT(x)((double)(x))
#define INT_TO_COORD(x)((coord_t)(x))

static inline int
coord_to_int_clamp(coord_t x, const int max_plus_one){
  if (x < 0)
    return 0;
  if (x >= max_plus_one - 1.5)
    return max_plus_one - 1;
  return (int)(x);
}

static inline int
coord_in_range(coord_t x, const int max_plus_one){
  return x >= 0 && (x + 0.5 < max_plus_one);
}

#else

#define COORD_TO_INT(x)((x) / (1 << SPARROW_FIXED_POINT))
#define COORD_TO_FLOAT(x)(((double)(x)) / (1 << SPARROW_FIXED_POINT))
#define INT_TO_COORD(x)((x) * (1 << SPARROW_FIXED_POINT))

static inline int
coord_to_int_clamp(coord_t x, const int max_plus_one){
  if (x < 0)
    return 0;
  x >>= SPARROW_FIXED_POINT;
  if (x >= max_plus_one)
    return max_plus_one - 1;
  return x;
}

static inline int
coord_in_range(coord_t x, const int max_plus_one){
  return x >= 0 && (x < max_plus_one << SPARROW_FIXED_POINT);
}

#endif

//these ones are common
static inline int
coords_to_index(coord_t x, coord_t y, int w, int h){
  int iy = coord_to_int_clamp(y, h);
  int ix = coord_to_int_clamp(x, w);
  return iy * w + ix;
}

#define C2I COORD_TO_INT

/********************************************/

static void
corners_to_full_lut(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  DEBUG_FIND_LINES(fl);
  sparrow_corner_t *mesh = fl->mesh;   /*maps regular points in ->out to points in ->in */
  sparrow_map_lut_t *map_lut = sparrow->map_lut;
  int mesh_w = fl->n_vlines;
  int mesh_h = fl->n_hlines;
  int mcy, mmy, mcx, mmx; /*Mesh Corner|Modulus X|Y*/
  int y = H_LINE_OFFSET;
  sparrow_corner_t *mesh_row = mesh;

  for(mcy = 0; mcy < mesh_h - 1; mcy++){
    for (mmy = 0; mmy < LINE_PERIOD; mmy++, y++){
      sparrow_corner_t *mesh_square = mesh_row;
      int i = y * sparrow->out.width + V_LINE_OFFSET;
      for(mcx = 0; mcx < mesh_w - 1; mcx++){
        coord_t iy = mesh_square->y + mmy * mesh_square->dyd;
        coord_t ix = mesh_square->x + mmy * mesh_square->dxd;
        for (mmx = 0; mmx < LINE_PERIOD; mmx++, i++){
          int ixx = coord_to_int_clamp(ix, sparrow->in.width);
          int iyy = coord_to_int_clamp(iy, sparrow->in.height);
          if(sparrow->screenmask[iyy * sparrow->in.width + ixx]){
            map_lut[i].x = ixx;
            map_lut[i].y = iyy;
          }
          ix += mesh_square->dxr;
          iy += mesh_square->dyr;
        }
        mesh_square++;
      }
    }
    mesh_row += mesh_w;
  }
  sparrow->map_lut = map_lut;
  debug_map_lut(sparrow, fl);
}

static void
debug_corners_image(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  sparrow_corner_t *mesh = fl->mesh;
  guint32 *data = (guint32*)fl->debug->imageData;
  guint w = fl->debug->width;
  guint h = fl->debug->height;
  memset(data, 0, sparrow->in.size);
  guint32 colours[4] = {0xff0000ff, 0x00ff0000, 0x0000ff00, 0xffffffff};
  for (int i = 0; i < fl->n_vlines * fl->n_hlines; i++){
    sparrow_corner_t *c = &mesh[i];
    coord_t x = c->x;
    coord_t y = c->y;
    coord_t txr = x;
    coord_t txd = x;
    coord_t tyr = y;
    coord_t tyd = y;
    for (int j = 1; j < LINE_PERIOD; j+= 2){
      txr += c->dxr * 2;
      txd += c->dxd * 2;
      tyr += c->dyr * 2;
      tyd += c->dyd * 2;
      guint hl = coords_to_index(txr, tyr, w, h);
      data[hl] = 0x88000088;
      guint vl = coords_to_index(txd, tyd, w, h);
      data[vl] = 0x00663300;
    }
    data[coords_to_index(x, y, w, h)] = colours[c->status];
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
      data[coords_to_index(v[j].x, v[j].y,
            sparrow->in.width, sparrow->in.height)] = (colour * (v[j].signal / 2)) / 256;
    }
  }
  MAYBE_DEBUG_IPL(fl->debug);
}


#define SIGNAL_QUANT 1

/*maximum number of pixels in a cluster */
#define CLUSTER_SIZE 8


/*find map points with common intersection data, and collect them into clusters */
static void
make_clusters(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  sparrow_cluster_t *clusters = fl->clusters;
  int x, y;
  /*special case: spurious values collect up at 0,0 */
  fl->map[0].signal[SPARROW_VERTICAL] = 0;
  fl->map[0].signal[SPARROW_HORIZONTAL] = 0;
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
      if (vline == BAD_PIXEL || hline == BAD_PIXEL){
        GST_DEBUG("ignoring bad pixel %d, %d\n", x, y);
        continue;
      }
      sparrow_cluster_t *cluster = &clusters[hline * fl->n_vlines + vline];
      sparrow_voter_t *voters = cluster->voters;
      int n = cluster->n;
      guint signal = (vsig * hsig) / SIGNAL_QUANT;
      GST_DEBUG("signal at %p (%d, %d): %dv %dh, product %u, lines: %dv %dh\n"
          "cluster is %p, n is %d\n", p, x, y,
          vsig, hsig, signal, vline, hline, cluster, n);
      if (signal == 0){
        GST_WARNING("signal at %p (%d, %d) is %d following quantisation!\n",
            p, x, y, signal);
      }

      if (n < CLUSTER_SIZE){
        voters[n].x = INT_TO_COORD(x);
        voters[n].y = INT_TO_COORD(y);
        voters[n].signal = signal;
        cluster->n++;
      }
      else {
        /*duplicate x, y, signal, so they aren't mucked up */
        guint ts = signal;
        coord_t tx = x;
        coord_t ty = y;
        /*replaced one ends up here */
        guint ts2;
        coord_t tx2;
        coord_t ty2;
        for (int j = 0; j < CLUSTER_SIZE; j++){
          if (voters[j].signal < ts){
            ts2 = voters[j].signal;
            tx2 = voters[j].x;
            ty2 = voters[j].y;
            voters[j].signal = ts;
            voters[j].x = tx;
            voters[j].y = ty;
            ts = ts2;
            tx = tx2;
            ty = ty2;
          }
        }
        GST_DEBUG("more than %d pixels at cluster for corner %d, %d."
            "Dropped %u for %u\n",
            CLUSTER_SIZE, vline, hline, ts2, signal);
      }
    }
  }
  if (sparrow->debug){
    debug_clusters(sparrow, fl);
  }
}


static inline int
drop_cluster_voter(sparrow_voter_t *voters, int n, int k)
{
  int i;
  if (k < n){
    n--;
    for (i = k; i < n; i++){
      voters[i] = voters[i + 1];
    }
  }
  return n;
}

static inline int sort_median(coord_t *a, guint n)
{
  guint i, j;
  /*stupid sort, but n is very small*/
  for (i = 0; i <  n; i++){
    for (j = i + 1; j < n; j++){
      if (a[i] > a[j]){
        coord_t tmp = a[j];
        a[j] = a[i];
        a[i] = tmp;
      }
    }
  }
  guint middle = n / 2;
  coord_t answer = a[middle];

  if ((n & 1) == 0){
    answer += a[middle - 1];
    answer /= 2;
  }
  return answer;
}

#define EUCLIDEAN_D2(ax, ay, bx, by)((ax - bx) * (ax - bx) + (ay - by) * (ay - by))
#define EUCLIDEAN_THRESHOLD 7

static inline int
euclidean_discard_cluster_outliers(sparrow_voter_t *voters, int n)
{
  /* Calculate distance between each pair.  Discard points with maximum sum,
     then recalculate until all are within threshold.
 */
  GST_DEBUG("cleansing a cluster of size %d using sum of distances", n);
  int i, j;
  coord_t dsums[n];
  for (i = 0; i < n; i++){
    dsums[i] = 0;
    for (j = i + 1; j < n; j++){
      coord_t d = EUCLIDEAN_D2(voters[i].x, voters[i].y,
          voters[j].x, voters[j].y);
      dsums[i] += d;
      dsums[j] += d;
    }
  }

  int worst_i;
  coord_t worst_d, threshold;
  while (n > 1){
    threshold = EUCLIDEAN_THRESHOLD * n;
    worst_i = 0;
    worst_d = 0;
    for (i = 0; i < n; i++){
      if (dsums[i] > worst_d){
        worst_d = dsums[i];
        worst_i = i;
      }
    }
    if (worst_d > threshold){
      GST_DEBUG("failing point %d, distance sq %d, threshold %d\n",
          worst_i, C2I(worst_d), C2I(threshold));
      //subtract this one from the sums, or they'll all go
      for (i = 0; i < n; i++){
        dsums[i] -= EUCLIDEAN_D2(voters[i].x, voters[i].y,
            voters[worst_i].x, voters[worst_i].y);
      }
      n = drop_cluster_voter(voters, n, worst_i);
    }
    else{
      GST_DEBUG("worst %d, was only %d, threshold %d\n",
          worst_i, C2I(worst_d), C2I(threshold));
      break;
    }
  }
  return n;
}

static inline int
median_discard_cluster_outliers(sparrow_voter_t *voters, int n)
{
  coord_t xvals[n];
  coord_t yvals[n];
  int i;
  for (i = 0; i < n; i++){
    /*XXX could sort here*/
    xvals[i] = voters[i].x;
    yvals[i] = voters[i].y;
  }
  const coord_t xmed = sort_median(xvals, n);
  const coord_t ymed = sort_median(yvals, n);

  for (i = 0; i < n; i++){
    coord_t dx = voters[i].x - xmed;
    coord_t dy = voters[i].y - ymed;
    if (dx * dx + dy * dy > OUTLIER_THRESHOLD){
      n = drop_cluster_voter(voters, n, i);
    }
  }
  return n;
}

/* */
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
#if 1
      /*discard outliers based on sum of squared distances: good points should
        be in a cluster, and have lowest sum*/
      cluster->n = euclidean_discard_cluster_outliers(cluster->voters, cluster->n);
#else
      /*discard values away from median x, y values.
       (each dimension is calculated independently)*/
      cluster->n = median_discard_cluster_outliers(cluster->voters, cluster->n);
#endif
      /* now find a weighted average position */
      /*With int coord_t, coord_sum_t is
        64 bit to avoid overflow -- should probably just use floating point
        (or reduce signal)*/
      coord_sum_t xsum, ysum;
      coord_t xmean, ymean;
      guint64 votes;
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
          i, cluster->n, votes, C2I(xsum), C2I(ysum), C2I(xmean), C2I(ymean));

      mesh[i].x = xmean;
      mesh[i].y = ymean;
      mesh[i].status = CORNER_EXACT;
      GST_DEBUG("found corner %d at (%3f, %3f)\n",
          i, COORD_TO_FLOAT(xmean), COORD_TO_FLOAT(ymean));
    }
  }
}

static sparrow_voter_t
median_centre(sparrow_voter_t *estimates, int n){
  /*X and Y arevcalculated independently, which is really not right.
    on the other hand, it probably works. */
  int i;
  sparrow_voter_t result;
  coord_t vals[n];
  for (i = 0; i < n; i++){
    vals[i] = estimates[i].x;
  }
  result.x = coord_median(vals, n);

  for (i = 0; i < n; i++){
    vals[i] = estimates[i].y;
  }
  result.y = coord_median(vals, n);
  return result;
}

static const sparrow_estimator_t base_estimators[] = {
  { 0, 1,     0, 2,    0, 3},
  { 0, 2,     0, 4,    0, 6},
  { 1, 0,     2, 0,    3, 0},
  { 1, 1,     2, 2,    3, 3},
  { 1, 2,     2, 4,    3, 6},
  { 1, 3,     2, 6,    3, 9},
  { 2, 0,     4, 0,    6, 0},
  { 2, 1,     4, 2,    6, 3},
  { 2, 2,     4, 4,    6, 6},
  { 2, 3,     4, 6,    6, 9},
  { 3, 1,     6, 2,    9, 3},
  { 3, 2,     6, 4,    9, 6},
};

#define BASE_ESTIMATORS (sizeof(base_estimators) / sizeof(sparrow_estimator_t))
#define ESTIMATORS  (BASE_ESTIMATORS * 4)

static inline void
calculate_estimator_tables(sparrow_estimator_t *estimators){
  guint i, j;
  sparrow_estimator_t *e = estimators;
  for (i = 0; i < BASE_ESTIMATORS; i++){
    for (j = 0; j < 4; j++){
      *e = base_estimators[i];
      if (j & 1){
        if (! e->x1){
          continue;
        }
        e->x1 = -e->x1;
        e->x2 = -e->x2;
        e->x3 = -e->x3;
      }
      if (j & 2){
        if (! e->y1){
          continue;
        }
        e->y1 = -e->y1;
        e->y2 = -e->y2;
        e->y3 = -e->y3;
      }
      GST_DEBUG("estimator: %-d,%-d  %-d,%-d  %-d,%-d",
          e->x1, e->y1, e->x2, e->y2, e->x3, e->y3);
      e++;
    }
  }
}

/* nice big word. acos(1.0 - MAX_NONCOLLINEARITY) = angle of deviation.
   0.005: 5.7 degrees, 0.01: 8.1, 0.02: 11.5, 0.04: 16.3, 0.08: 23.1
   1 pixel deviation in 32 -> ~ 1/33 == 0.03 (if I understand correctly)
*/
#define MAX_NONCOLLINEARITY 0.02

/*the map made above is likely to be full of errors. Fix them, and add in
  missing points */
static void
complete_map(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  sparrow_voter_t estimates[ESTIMATORS + 1];
  sparrow_estimator_t estimators[ESTIMATORS];
  calculate_estimator_tables(estimators);

  guint32 *debug = NULL;
  if (sparrow->debug){
    debug = (guint32*)fl->debug->imageData;
    memset(debug, 0, sparrow->in.size);
  }

  int x, y;
  int width = fl->n_vlines;
  int height = fl->n_hlines;
  int screen_width = sparrow->in.width;
  int screen_height = sparrow->in.height;
  sparrow_corner_t *mesh = fl->mesh;
  sparrow_corner_t *mesh_next = fl->mesh_next;

  memset(estimates, 0, sizeof(estimates)); /*just for clarity in debugging */
  int prev_settled = 0;
  while (1){
    memcpy(mesh_next, mesh, width * height * sizeof(sparrow_corner_t));
    int settled = 0;
    for (y = 0; y < height; y++){
      for (x = 0; x < width; x++){
        sparrow_corner_t *corner = &mesh[y * width + x];
        if (corner->status == CORNER_SETTLED){
          settled ++;
          GST_DEBUG("ignoring settled corner %d, %d", x, y);
          continue;
        }
        int k = 0;
        for (guint j = 0; j < ESTIMATORS; j++){
          sparrow_estimator_t *e = &estimators[j];
          int x3, y3, x2, y2, x1, y1;
          y3 = y + e->y3;
          x3 = x + e->x3;
          if (!(y3 >= 0 && y3 < height &&
                  x3 >= 0 && x3 < width &&
                  mesh[y3 * width + x3].status != CORNER_UNUSED
              )){
            GST_DEBUG("not using estimator %d because corners aren't used, or are off screen\n"
                "x3 %d, y3 %d", j, x3, y3);
            continue;
          }
          y2 = y + e->y2;
          x2 = x + e->x2;
          y1 = y + e->y1;
          x1 = x + e->x1;
          if (mesh[y2 * width + x2].status == CORNER_UNUSED ||
              mesh[y1 * width + x1].status == CORNER_UNUSED){
            GST_DEBUG("not using estimator %d because corners aren't used", j);
            continue;
          }
          /*there are 3 points, and the unknown one.
            They should all be in a line.
            The ratio of the p3-p2:p2-p1 sould be the same as
            p2-p1:p1:p0.

            This really has to be done in floating point.

            collinearity, no division, but no useful error metric
            x[0] * (y[1]-y[2]) + x[1] * (y[2]-y[0]) + x[2] * (y[0]-y[1])  == 0
            (at least not without further division)

            This way:

            cos angle = dot product / product of euclidean lengths

            (dx12 * dx23 + dy12 * dy23) /
            (sqrt(dx12 * dx12 + dy12 * dy12) * sqrt(dx23 * dx23 + dy23 * dy23))

            is costly up front (sqrt), but those distances need to be
            calculated anyway (or at least they are handy).  Not much gained by
            short-circuiting on bad collinearity, though.

            It also handlily catches all the division by zeros in one meaningful
            go.
          */
          sparrow_corner_t *c1 = &mesh[y1 * width + x1];
          sparrow_corner_t *c2 = &mesh[y2 * width + x2];
          sparrow_corner_t *c3 = &mesh[y3 * width + x3];

          double dx12 = c1->x - c2->x;
          double dy12 = c1->y - c2->y;
          double dx23 = c2->x - c3->x;
          double dy23 = c2->y - c3->y;
          double distance12 = sqrt(dx12 * dx12 + dy12 * dy12);
          double distance23 = sqrt(dx23 * dx23 + dy23 * dy23);

          double dp = dx12 * dx23 + dy12 * dy23;

          double distances = distance12 * distance23;
#if 0
          GST_DEBUG("mesh points: %d,%d, %d,%d, %d,%d\n"
              "map points: %d,%d, %d,%d,  %d,%d\n"
              "diffs: 12: %0.3f,%0.3f,  23: %0.3f,%0.3f, \n"
              "distances: 12: %0.3f,   32: %0.3f\n",
              x1, y1, x2, y2, x3, y3,
              C2I(c1->x), C2I(c1->y), C2I(c2->x), C2I(c2->y), C2I(c3->x), C2I(c3->y),
              dx12, dy12, dx23, dy23, distance12, distance23
          );


#endif

          if (distances == 0.0){
            GST_INFO("at least two points out of %d,%d, %d,%d, %d,%d are the same!",
                x1, y1, x2, y2, x3, y3);
            continue;
          }
          double line_error = 1.0 - dp / distances;
          if (line_error > MAX_NONCOLLINEARITY){
            GST_DEBUG("Points %d,%d, %d,%d, %d,%d are not in a line: non-collinearity: %3f",
                x1, y1, x2, y2, x3, y3, line_error);
            continue;
          }
          //GST_DEBUG("GOOD collinearity: %3f", line_error);


          double ratio = distance12 / distance23;
          /*so here's the estimate!*/
          coord_t dx = dx12 * ratio;
          coord_t dy = dy12 * ratio;
          coord_t ex = c1->x + dx;
          coord_t ey = c1->y + dy;

#if 0
          GST_DEBUG("dx, dy: %d,%d, ex, ey: %d,%d\n"
              "dx raw:  %0.3f,%0.3f,  x1, x2: %0.3f,%0.3f,\n"
              "distances: 12: %0.3f,   32: %0.3f\n"
              "ratio: %0.3f\n",
              C2I(dx), C2I(dy), C2I(ex), C2I(ey),
              dx, dy, ex, ey, ratio
          );
#endif

          if (! coord_in_range(ey, screen_height) ||
              ! coord_in_range(ex, screen_width)){
            GST_DEBUG("rejecting estimate for %d, %d, due to ex, ey being %d, %d",
                x, y, C2I(ex), C2I(ey));
            continue;
          }
          /*
          GST_DEBUG("estimator %d,%d SUCCESSFULLY estimated that %d, %d will be %d, %d",
              x1, x2, x, y, C2I(ex), C2I(ey));
          */
          estimates[k].x = ex;
          estimates[k].y = ey;
          if (sparrow->debug){
            debug[coords_to_index(ex, ey, sparrow->in.width, sparrow->in.height)] = 0x00aa7700;
          }
          k++;
        }
        /*now there is an array of estimates.
          The *_discard_cluster_outliers functions should fit here */
        GST_INFO("got %d estimates for %d,%d", k, x, y);
        if(! k){
          continue;
        }
        coord_t guess_x;
        coord_t guess_y;

#if 1
        /*now find median values.  If the number is even, add a copy of either
          the original value, or a random element. */
        if (! k & 1){
          if (corner->status != CORNER_UNUSED){
            estimates[k].x = corner->x;
            estimates[k].y = corner->y;
          }
          else {
            int r = RANDINT(sparrow, 0, r);
            estimates[k].x = estimates[r].x;
            estimates[k].y = estimates[r].y;
          }
          k++;
        }
        sparrow_voter_t centre = median_centre(estimates, k);
        guess_x = centre.x;
        guess_y = centre.y;

#else

        k = euclidean_discard_cluster_outliers(estimates, k);
        if (sparrow->debug){
          for (int j = 0; j < k; j++){
            debug[coords_to_index(estimates[j].x, estimates[j].y,
                  sparrow->in.width, sparrow->in.height)] = 0x00ffff00;
          }
        }
        GST_INFO("After discard, left with %d estimates", k);
        /*now what? the mean? yes.*/
        coord_t sumx = 0;
        coord_t sumy = 0;
        for (int j = 0; j < k; j++){
          sumx += estimates[j].x;
          sumy += estimates[j].y;
        }
        guess_x = sumx / k;
        guess_y = sumy / k;

#endif

        GST_INFO("estimating %d,%d", C2I(guess_x), C2I(guess_y));

        if (corner->status == CORNER_EXACT){
          GST_INFO("using exact reading %d,%d", C2I(corner->x), C2I(corner->y));
          if (sparrow->debug){
            debug[coords_to_index(corner->x, corner->y,
                  sparrow->in.width, sparrow->in.height)] = 0xffff3300;
          }
          if (abs(corner->x - guess_x) < 3){
            guess_x = corner->x;
          }
          if (abs(corner->y - guess_y) < 3){
            guess_y = corner->y;
          }
        }
        if (k < 5){
          GST_DEBUG("weak evidence, mark corner PROJECTED");
          corner->status = CORNER_PROJECTED;
          if (sparrow->debug){
            debug[coords_to_index(guess_x, guess_y,
                  sparrow->in.width, sparrow->in.height)] = 0xff0000ff;
          }
        }
        else{
          GST_DEBUG("corner is SETTLED");
          corner->status = CORNER_SETTLED;
          settled ++;
          if (sparrow->debug){
            debug[coords_to_index(guess_x, guess_y,
                  sparrow->in.width, sparrow->in.height)] = 0xffffffff;
          }
        }
        corner->x = guess_x;
        corner->y = guess_y;
      }
    }
    GST_INFO("settled %d in that round. %d left to go",
        settled - prev_settled, width * height - settled);
    if (settled == width * height || settled == prev_settled){
      break;
    }
    prev_settled = settled;
    sparrow_corner_t *tmp = mesh_next;
    mesh_next = mesh;
    mesh = tmp;
  }
  fl->mesh = mesh;
  fl->mesh_next = mesh_next;
  MAYBE_DEBUG_IPL(fl->debug);
}


static void
calculate_deltas(GstSparrow *sparrow, sparrow_find_lines_t *fl){
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
      sparrow_corner_t *right = (x == width - 1) ? corner : corner + 1;
      sparrow_corner_t *down =  (y == height - 1) ? corner : corner + width;
      GST_DEBUG("i %d xy %d,%d width %d. in_xy %d,%d; down in_xy %d,%d; right in_xy %d,%d\n",
          i, x, y, width, C2I(corner->x), C2I(corner->y), C2I(down->x),
          C2I(down->y), C2I(right->x),  C2I(right->y));
      if (corner->status != CORNER_UNUSED){
        if (right->status != CORNER_UNUSED){
          corner->dxr = QUANTISE_DELTA(right->x - corner->x);
          corner->dyr = QUANTISE_DELTA(right->y - corner->y);
        }
        if (down->status != CORNER_UNUSED){
          corner->dxd = QUANTISE_DELTA(down->x -  corner->x);
          corner->dyd = QUANTISE_DELTA(down->y -  corner->y);
        }
      }
    }
  }
  if (sparrow->debug){
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
    signal = (((colour >> fl->shift1) & COLOUR_MASK) +
            ((colour >> fl->shift2) & COLOUR_MASK));
    if (signal){
      if (fl->map[i].lines[line->dir]){
        /*assume the pixel is on for everyone and will just confuse
          matters. ignore it.
        */

        if (fl->map[i].lines[line->dir] != BAD_PIXEL){
          /*
          GST_DEBUG("HEY, expected point %d to be in line %d (dir %d) "
              "and thus empty, but it is also in line %d\n"
              "old signal %d, new signal %d, marking as BAD\n",
              i, line->index, line->dir, fl->map[i].lines[line->dir],
              fl->map[i].signal[line->dir], signal);
          */
          fl->map[i].lines[line->dir] = BAD_PIXEL;
          fl->map[i].signal[line->dir] = 0;
        }
      }
      else{
        fl->map[i].lines[line->dir] = line->index;
        fl->map[i].signal[line->dir] = signal;
      }
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
    data[i] |= ((fl->map[i].lines[SPARROW_VERTICAL] == BAD_PIXEL) ||
        (fl->map[i].lines[SPARROW_HORIZONTAL] == BAD_PIXEL)) ? 255 << sparrow->in.bshift : 0;
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

static void
jump_state(GstSparrow *sparrow, sparrow_find_lines_t *fl, edges_state_t state){
  if (state == EDGES_NEXT_STATE){
    fl->state++;
  }
  else {
    fl->state = state;
  }
  switch (fl->state){
  case EDGES_FIND_NOISE:
    sparrow->countdown = MAX(sparrow->lag, 1) + SAFETY_LAG;
    break;
  case EDGES_FIND_LINES:
    sparrow->countdown = MAX(sparrow->lag, 1) + SAFETY_LAG;
    break;
  case EDGES_FIND_CORNERS:
    sparrow->countdown = 7;
    break;
  case EDGES_WAIT_FOR_PLAY:
    global_number_of_edge_finders--;
    sparrow->countdown = 300;
    break;
  default:
    GST_DEBUG("jumped to non-existent state %d\n", fl->state);
    break;
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
    draw_line(sparrow, line, out);
  }
  else{
    /*show nothing, look for result */
    look_for_line(sparrow, in, fl, line);
    if (sparrow->debug){
      debug_map_image(sparrow, fl);
    }
    fl->current++;
    if (fl->current == fl->n_lines){
      jump_state(sparrow, fl, EDGES_NEXT_STATE);
    }
    else{
      sparrow->countdown = MAX(sparrow->lag, 1) + SAFETY_LAG;
    }
  }
}

#define LINE_THRESHOLD 32

static inline void
find_threshold(GstSparrow *sparrow, sparrow_find_lines_t *fl, guint8 *in, guint8 *out)
{
  memset(out, 0, sparrow->out.size);
  /*XXX should average/median over a range of frames */
  if (sparrow->countdown == 0){
    memcpy(fl->threshold->imageData, in, sparrow->in.size);
    /*add a constant, and smooth */
    cvAddS(fl->threshold, cvScalarAll(LINE_THRESHOLD), fl->working, NULL);
    cvSmooth(fl->working, fl->threshold, CV_GAUSSIAN, 3, 0, 0, 0);
    //cvSmooth(fl->working, fl->threshold, CV_MEDIAN, 3, 0, 0, 0);
    jump_state(sparrow, fl, EDGES_NEXT_STATE);
  }
  sparrow->countdown--;
}

/*match up lines and find corners */
static inline int
find_corners(GstSparrow *sparrow, sparrow_find_lines_t *fl)
{
  sparrow->countdown--;
  switch(sparrow->countdown){
  case 4:
    make_clusters(sparrow, fl);
    break;
  case 3:
    make_corners(sparrow, fl);
    break;
  case 2:
    complete_map(sparrow, fl);
    break;
  case 1:
    calculate_deltas(sparrow, fl);
    break;
  case 0:
#if USE_FULL_LUT
    corners_to_full_lut(sparrow, fl);
#else
    corners_to_lut(sparrow, fl);
#endif
    jump_state(sparrow, fl, EDGES_NEXT_STATE);
    break;
  default:
    GST_DEBUG("how did sparrow->countdown get to be %d?", sparrow->countdown);
    sparrow->countdown = 5;
  }
  return sparrow->countdown;
}

/*use a dirty shared variable*/
static gboolean
wait_for_play(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  if (global_number_of_edge_finders == 0 ||
      sparrow->countdown == 0){
    return TRUE;
  }
  sparrow->countdown--;
  return FALSE;
}

INVISIBLE sparrow_state
mode_find_edges(GstSparrow *sparrow, guint8 *in, guint8 *out){
  sparrow_find_lines_t *fl = (sparrow_find_lines_t *)sparrow->helper_struct;
  switch (fl->state){
  case EDGES_FIND_NOISE:
    find_threshold(sparrow, fl, in, out);
    break;
  case EDGES_FIND_LINES:
    draw_lines(sparrow, fl, in, out);
    break;
  case EDGES_FIND_CORNERS:
    memset(out, 0, sparrow->out.size);
    find_corners(sparrow, fl);
    break;
  case EDGES_WAIT_FOR_PLAY:
    memset(out, 0, sparrow->out.size);
    if (wait_for_play(sparrow, fl)){
      return SPARROW_NEXT_STATE;
    }
    break;
  default:
    GST_WARNING("strange state in mode_find_edges: %d", fl->state);
    memset(out, 0, sparrow->out.size);
  }
  return SPARROW_STATUS_QUO;
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
  free(fl->mesh_mem);
  free(fl->clusters);
  cvReleaseImage(&fl->threshold);
  cvReleaseImage(&fl->working);
  cvReleaseImageHeader(&fl->input);
  free(fl);
  GST_DEBUG("freed everything\n");
  sparrow->helper_struct = NULL;
}

static void
setup_colour_shifts(GstSparrow *sparrow, sparrow_find_lines_t *fl){
  /*COLOUR_QUANT reduces the signal a little bit more, avoiding overflow
    later */
  switch (sparrow->colour){
  case SPARROW_WHITE:
  case SPARROW_GREEN:
    fl->shift1 = sparrow->in.gshift + COLOUR_QUANT;
    fl->shift2 = sparrow->in.gshift + COLOUR_QUANT;
    break;
  case SPARROW_MAGENTA:
    fl->shift1 = sparrow->in.rshift + COLOUR_QUANT;
    fl->shift2 = sparrow->in.bshift + COLOUR_QUANT;
    break;
  }
}

INVISIBLE void
init_find_edges(GstSparrow *sparrow){
  gint i;
  sparrow_find_lines_t *fl = zalloc_aligned_or_die(sizeof(sparrow_find_lines_t));
  sparrow->helper_struct = (void *)fl;

  gint h_lines = (sparrow->out.height + LINE_PERIOD - 1) / LINE_PERIOD;
  gint v_lines = (sparrow->out.width + LINE_PERIOD - 1) / LINE_PERIOD;
  gint n_lines_max = (h_lines + v_lines);
  gint n_corners = (h_lines * v_lines);
  fl->n_hlines = h_lines;
  fl->n_vlines = v_lines;

  fl->h_lines = malloc_aligned_or_die(sizeof(sparrow_line_t) * n_lines_max);
  fl->shuffled_lines = malloc_aligned_or_die(sizeof(sparrow_line_t *) * n_lines_max);
  GST_DEBUG("shuffled lines, malloced %p\n", fl->shuffled_lines);

  GST_DEBUG("map is going to be %d * %d \n", sizeof(sparrow_intersect_t), sparrow->in.pixcount);
  fl->map = zalloc_aligned_or_die(sizeof(sparrow_intersect_t) * sparrow->in.pixcount);
  fl->clusters = zalloc_or_die(n_corners * sizeof(sparrow_cluster_t));
  fl->mesh_mem = zalloc_aligned_or_die(n_corners * sizeof(sparrow_corner_t) * 2);
  fl->mesh = fl->mesh_mem;
  fl->mesh_next = fl->mesh + n_corners;

  sparrow_line_t *line = fl->h_lines;
  sparrow_line_t **sline = fl->shuffled_lines;
  int offset;

  for (i = 0, offset = H_LINE_OFFSET; offset < sparrow->out.height;
       i++, offset += LINE_PERIOD){
    line->offset = offset;
    line->dir = SPARROW_HORIZONTAL;
    line->index = i;
    *sline = line;
    line++;
    sline++;
    //GST_DEBUG("line %d h has offset %d\n", i, offset);
  }

  /*now add the vertical lines */
  fl->v_lines = line;
  for (i = 0, offset = V_LINE_OFFSET; offset < sparrow->out.width;
       i++, offset += LINE_PERIOD){
    line->offset = offset;
    line->dir = SPARROW_VERTICAL;
    line->index = i;
    *sline = line;
    line++;
    sline++;
    //GST_DEBUG("line %d v has offset %d\n", i, offset);
  }
  //DEBUG_FIND_LINES(fl);
  fl->n_lines = line - fl->h_lines;
  GST_DEBUG("allocated %d lines, made %d\n", n_lines_max, fl->n_lines);

  /*now shuffle */
  for (i = 0; i < fl->n_lines; i++){
    int j = RANDINT(sparrow, 0, fl->n_lines);
    sparrow_line_t *tmp = fl->shuffled_lines[j];
    fl->shuffled_lines[j] = fl->shuffled_lines[i];
    fl->shuffled_lines[i] = tmp;
  }

  setup_colour_shifts(sparrow, fl);

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

  if (sparrow->reload){
    if (access(sparrow->reload, R_OK)){
      GST_DEBUG("sparrow->reload is '%s' and it is UNREADABLE\n", sparrow->reload);
      exit(1);
    }
    read_edges_info(sparrow, fl, sparrow->reload);
    memset(fl->map, 0, sizeof(sparrow_intersect_t) * sparrow->in.pixcount);
    //memset(fl->clusters, 0, n_corners * sizeof(sparrow_cluster_t));
    memset(fl->mesh, 0, n_corners * sizeof(sparrow_corner_t));
    jump_state(sparrow, fl, EDGES_FIND_CORNERS);
  }
  else {
    jump_state(sparrow, fl, EDGES_FIND_NOISE);
  }

  global_number_of_edge_finders++;
}

