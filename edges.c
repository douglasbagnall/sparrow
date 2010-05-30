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



static void
debug_lut(GstSparrow *sparrow, sparrow_find_lines_t *fl){
}

#define OFFSET(x, y, w)((((y) * (w)) >> SPARROW_FIXED_POINT) + ((x) >> SPARROW_FIXED_POINT))

#define QUANTISE_DELTA(d)(((d) + LINE_PERIOD / 2) / LINE_PERIOD)

/*tolerate up to 1/8 of a pixel drift */
#define MAX_DRIFT (1 << (SPARROW_FIXED_POINT - 3))


static inline sparrow_map_path_t*
possibly_new_point(sparrow_map_path_t *p, int dx, int dy){
  if (dx != p->dx && dy != p->dy){
    p++;
    p->dx = dx;
    p->dy = dy;
    p->n = 0;
  }
  return p;
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

  int y;
  sparrow_corner_t *mesh_row = mesh;

  for (y = 0; y < H_LINE_OFFSET; y++){
    map->rows[y].start = 0;
    map->rows[y].end = 0;
    map->rows[y].points = NULL;
  }

  sparrow_map_row_t *row = map->rows + H_LINE_OFFSET;
  row->points = map->point_mem;
  sparrow_map_path_t *p = row->points;

  for(mcy = 0; mcy < mesh_h; mcy++){ /* for each mesh row */
    for (mmy = 0; mmy < LINE_PERIOD; mmy++){ /* for each output line */
      int ix, iy; /* input x, y at mesh points, interpolated vertically  */
      int rx, ry; /* running x, y; approximates ix, iy */
      int dx, dy;
      int on = 0;
      sparrow_corner_t *mesh_square = mesh_row;
      row->points = NULL;
      row->start = 0;
      row->end = 0;
      for(mcx = 0; mcx < mesh_w - 1; mcx++){
        /*for each mesh block except the last, which has no dx,dy.
         Thus the mesh blocks are referenced in LINE_PERIOD passes.*/
        if (mesh_square->status == CORNER_UNUSED){
          if (! on){
            mesh_square++;
            continue;
          }
          /*lordy! continue with previous deltas*/
          ix = rx;
          iy = ry;
        }
        else {
          /* starting point for this row in this block. */
          iy = mesh_square->in_y + mmy * (mesh_square->dyd / 1);
          ix = mesh_square->in_x + mmy * (mesh_square->dxd / 1);
          /*incremental delta going left to right in this block */
          dy = (mesh_square->dyr / 1);
          dx = (mesh_square->dxr / 1);
        }

        /*index of the last point in this block
         NB: calculating from ix, iy, which may differ slightly from rx, ry*/
        int lasti = OFFSET(
          ix + (LINE_PERIOD - 1) * dx,
          iy + (LINE_PERIOD - 1) * dy,
          in_w);

        if (! on){
          if (! mask[lasti]){
            /*it doesn't turn on within this block (or it is of ignorably
              short length). */
            mesh_square++;
            continue;
          }
          /*it does turn on. so step through and find it. This happens once
            per line.*/
          rx = ix;
          ry = iy;
          int j;
          for (j = 0; j < LINE_PERIOD; j++){
            if (mask[OFFSET(rx, ry, in_w)]){
              break;
            }
            rx += dx;
            ry += dy;
          }
          row->start = mcx * LINE_PERIOD + j;
          row->in_x = rx;
          row->in_y = ry;
          p = possibly_new_point(p, dx, dy);
          row->points = p;
          p->n = LINE_PERIOD - j;
          on = 1;
          mesh_square++;
          continue;
        }
        /*it is on. */
        /*maybe rx, ry are drifting badly, in which case, we need to recalculate dx, dy*/
        if (abs(rx - ix) > MAX_DRIFT ||
            abs(ry - iy) > MAX_DRIFT){
          int y = mcy * LINE_PERIOD + mmy;
          int x = mcx * LINE_PERIOD;
          GST_DEBUG("output point %d %d, rx, ry %d, %d have got %d, %d away from target %d, %d."
              " dx, dy is %d, %d\n",
              x, y, rx, ry, rx - ix, ry - iy, ix, iy, dx, dy);
          sparrow_corner_t *next = mesh_square + 1;
          if(next->status != CORNER_UNUSED){
            int niy = next->in_y + mmy * (next->dyd / 1);
            int nix = next->in_x + mmy * (next->dxd / 1);
            dx = QUANTISE_DELTA(nix - ix);
            dy = QUANTISE_DELTA(niy - iy);
            GST_DEBUG("new dx, dy is %d, %d\n", dx, dy);
          }
          else{
            GST_DEBUG("next corner is UNUSED. dx, dy unchanged\n");
          }
        }

        /*Probably dx/dy are different, so we need a new point */
        p = possibly_new_point(p, dx, dy);

        /*does it end it this one? */
        if (! mask[lasti]){
          int j;
          for (j = 0; j < LINE_PERIOD; j++){
            if (! mask[OFFSET(rx, ry, in_w)]){
              break;
            }
            rx += dx;
            ry += dy;
          }
          p->n += j;
          row->end = mcx * LINE_PERIOD + j;
          /*this row is done! */
          break;
        }
        p->n += LINE_PERIOD;
        rx += LINE_PERIOD * dx;
        ry += LINE_PERIOD * dy;
        mesh_square++;
      }
      row++;
    }
    mesh_row += mesh_w;
  }

  /*blank lines for the last few */
  for (y = sparrow->out.height - H_LINE_OFFSET; y < sparrow->out.height; y++){
    map->rows[y].start = 0;
    map->rows[y].end = 0;
    map->rows[y].points = NULL;
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
    for (int j = 1; j < LINE_PERIOD; j+= 2){
      txr += c->dxr * 2;
      txd += c->dxd * 2;
      tyr += c->dyr * 2;
      tyd += c->dyd * 2;
      data[INTXY(tyr) * w + INTXY(txr)] = 0x000088;
      data[INTXY(tyd) * w + INTXY(txd)] = 0x663300;
    }
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
      data[v[j].y * sparrow->in.width +
          v[j].x] = (colour * (v[j].signal / 2)) / 256;
    }
  }
  MAYBE_DEBUG_IPL(fl->debug);
}


#define SIGNAL_QUANT 1

/*maximum number of pixels in a cluster */
#define CLUSTER_SIZE 8


/*create the mesh */
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
        voters[n].x = x;
        voters[n].y = y;
        voters[n].signal = signal;
        cluster->n++;
      }
      else {
        /*duplicate x, y, signal, so they aren't mucked up */
        guint ts = signal;
        int tx = x;
        int ty = y;
        /*replaced one ends up here */
        int ts2;
        int tx2;
        int ty2;
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

#define EUCLIDEAN_D2(ax, ay, bx, by)((ax - bx) * (ax - bx) + (ay - by) * (ay - by))
#define NEIGHBOURLY_THRESHOLD (LINE_PERIOD * 4)

static inline void
neighbourly_discard_cluster_outliers(GstSparrow *sparrow, sparrow_cluster_t *cluster,
    sparrow_corner_t *neighbour)
{
  /* assuming the output mesh entirely fits in the input window (which is
     required for sparrow to work) the neighbours should be at most
     LINE_PERIOD * input resolution / output resolution apart. But set the
     threshold higher, just in case. */
  const int threshold = NEIGHBOURLY_THRESHOLD * sparrow->in.height / sparrow->out.height;
  int i;
  int neighbour_d[CLUSTER_SIZE];
  int close = 0;
  for (i = 0; i < cluster->n; i++){
    int d = EUCLIDEAN_D2(neighbour->in_x, neighbour->in_y,
        cluster->voters[i].x, cluster->voters[i].y);
    int pass = d > threshold;
    neighbour_d[i] = pass;
    close += pass;
    GST_DEBUG("failing point %d, distance sq %d, threshold %d\n", i, d, threshold);
  }
  if (close > 1){
    for (i = 0; i < cluster->n; i++){
      if (! neighbour_d[i]){
        drop_cluster_voter(cluster, i);
      }
    }
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
    int dx = cluster->voters[i].x - xmed;
    int dy = cluster->voters[i].y - ymed;
    if (dx * dx + dy * dy > OUTLIER_THRESHOLD){
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
        are discarded.

        This fails if all the cluster are way off. Obviously it would be good
        to include information about the grid in the decision, but that is not
        there yet. (needs iteration, really).

      Here's a slight attempt:*/
#if 0
      sparrow_corner_t *neighbour;
      if (x){
        neighbour = &mesh[i - 1];
        neighbourly_discard_cluster_outliers(sparrow, cluster, neighbour);
      }
      else if (y){
        neighbour = &mesh[i - width];
        neighbourly_discard_cluster_outliers(sparrow, cluster, neighbour);
      }
#endif
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
        xmean = (xsum << SPARROW_FIXED_POINT) / votes;
        ymean = (ysum << SPARROW_FIXED_POINT) / votes;
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
        corner->dxr = (right->in_x - corner->in_x);
        corner->dyr = (right->in_y - corner->in_y);
        corner->dxd = (down->in_x -  corner->in_x);
        corner->dyd = (down->in_y -  corner->in_y);
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
            corner->in_x = down->in_x - corner->dxd;
            corner->in_y = down->in_y - corner->dyd;
          }
          else {/*oh no*/
            GST_DEBUG("can't reconstruct corner %d, %d: no useable neighbours\n", x, y);
            /*it would be easy enough to look further, but hopefully of no
              practical use */
          }
        }
        else if (down == corner){ /*use right only */
          corner->in_x = right->in_x - corner->dxr;
          corner->in_y = right->in_y - corner->dyr;
        }
        else { /* use both */
          corner->in_x = right->in_x - corner->dxr;
          corner->in_y = right->in_y - corner->dyr;
          corner->in_x += down->in_x - corner->dxd;
          corner->in_y += down->in_y - corner->dyd;
          corner->in_x >>= 1;
          corner->in_y >>= 1;
        }
        corner->status = CORNER_PROJECTED;
      }
    }
  }
  /*now quantise delta values.  It would be wrong to do it earlier, when they
    are being used to calculate whole mesh jumps, but from now they are
    primarily going to used for pixel (mesh / LINE_PERIOD) jumps. To do this in
  corners_to_lut puts a whole lot of division in a tight loop.*/
  for (i = 0; i < width * height; i++){
    sparrow_corner_t *corner = &mesh[i];
    corner->dxr = QUANTISE_DELTA(corner->dxr);
    corner->dyr = QUANTISE_DELTA(corner->dyr);
    corner->dxd = QUANTISE_DELTA(corner->dxd);
    corner->dyd = QUANTISE_DELTA(corner->dyd);
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
    signal = (((colour >> fl->shift1) +
            (colour >> fl->shift2))) & 0xff;
    if (signal){
      if (fl->map[i].lines[line->dir]){
        GST_DEBUG("HEY, expected point %d to be in line %d (dir %d)"
            "and thus empty, but it is also in line %d\n",
            "old signal %d, new signal %d, ignoring weakest\n",
            i, line->index, line->dir, fl->map[i].lines[line->dir],
            fl->map[i].signal[line->dir], signal);
        if (signal < fl->map[i].signal[line->dir]){
          continue;
        }
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
    sparrow->countdown = sparrow->lag + 2;
    break;
  case EDGES_FIND_LINES:
    sparrow->countdown = sparrow->lag + 2;
    break;
  case EDGES_FIND_CORNERS:
    sparrow->countdown = 4;
  default:
    GST_DEBUG("jumped to state %d\n", fl->state);
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
    GST_DEBUG("current %d line %p\n", fl->current, line);
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
      sparrow->countdown = sparrow->lag + 2;
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
  case 3:
    make_clusters(sparrow, fl);
    break;
  case 2:
    make_corners(sparrow, fl);
    break;
  case 1:
    make_map(sparrow, fl);
    break;
  case 0:
    corners_to_lut(sparrow, fl);
    break;
  default:
    GST_DEBUG("how did sparrow->countdown get to be %d?", sparrow->countdown);
    sparrow->countdown = 4;
  }
  return sparrow->countdown;
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
    if (find_corners(sparrow, fl))
      break;
    return SPARROW_NEXT_STATE;
  case EDGES_NEXT_STATE:
    break; /*shush gcc */
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
  free(fl->mesh);
  free(fl->clusters);
  cvReleaseImage(&fl->threshold);
  cvReleaseImage(&fl->working);
  cvReleaseImageHeader(&fl->input);
  free(fl);
  sparrow->helper_struct = NULL;
}

/*reduce the signal a little bit more, avoiding overflow later */
#define COLOUR_QUANT  1

static void
setup_colour_shifts(GstSparrow *sparrow, sparrow_find_lines_t *fl){
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
  int offset;

  for (i = 0, offset = H_LINE_OFFSET; offset < h;
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
  for (i = 0, offset = V_LINE_OFFSET; offset < w;
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
    jump_state(sparrow, fl, EDGES_FIND_CORNERS);

  }
  else {
    jump_state(sparrow, fl, EDGES_FIND_NOISE);
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

