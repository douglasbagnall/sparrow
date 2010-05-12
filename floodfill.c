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



/* Floodfill for find screen */
static inline void
expand_one_mono(int x, int y, int c,
    CvPoint *nexts, int *n_nexts, guint8 *im, guint8 *mask, int w, int h){
  guint8 p = im[y * w + x];
  guint8 *m = &mask[y * w + x];
  if (*m && (p == c)){
    *m = 0;
    nexts[*n_nexts].x = x;
    nexts[*n_nexts].y = y;
    (*n_nexts)++;
  }
}

/*
  im: the image to be analysed
  mim: the mask image to be written
  start: a point of the right colour.
*/

INVISIBLE IplImage*
floodfill_mono_superfast(IplImage *im, IplImage *mim, CvPoint start)
{
  guint8 * data = (guint8 *)im->imageData;
  guint8 * mdata = (guint8 *)mim->imageData;
  int w = im->width;
  int h = im->height;
  int n_starts;
  int n_nexts = 0;
  CvPoint *starts;
  CvPoint *nexts;

  //malloc 2 lists of points. These *could* be as large as the image (but never should be)
  void * mem = malloc(w * h * 2 * sizeof(CvPoint));
  starts = mem;
  nexts = starts + w * h;

  n_starts = 1;
  starts[0] = start;

  while(n_starts){
    n_nexts = 0;
    int i;
    for (i = 0; i < n_starts; i++){
      int x = starts[i].x;
      int y = starts[i].y;
      int c = data[y * w + x];
      if (x > 0){
        expand_one_mono(x - 1, y, c, nexts, &n_nexts, data, mdata, w, h);
      }
      if (x < w - 1){
        expand_one_mono(x + 1, y, c, nexts, &n_nexts, data, mdata, w, h);
      }
      if (y > 0){
        expand_one_mono(x, y - 1, c, nexts, &n_nexts, data, mdata, w, h);
      }
      if (y < h - 1){
        expand_one_mono(x, y + 1, c, nexts, &n_nexts, data, mdata, w, h);
      }
    }
    CvPoint *tmp = starts;
    starts = nexts;
    nexts = tmp;
    n_starts = n_nexts;
  }
  free(mem);
  return im;
}






/* find a suitable threshold level by looking at the histogram of a monochrome
   image */
INVISIBLE int
find_edges_threshold(IplImage *im)
{
  int w = im->width;
  int h = im->height;
  CvSize small_size = {w / 8, h / 8};
  IplImage *small = cvCreateImage(small_size, IPL_DEPTH_8U, 1); /*for quicker histogram (stupid, perhaps?)*/
  cvResize(im, small, CV_INTER_NN);
  int hist_size[] = {255};
  float range[] = {0, 255};
  float *ranges[] = {range};
  CvHistogram* hist = cvCreateHist(1, hist_size, CV_HIST_ARRAY, ranges, 1);
  cvCalcHist(&small, hist, 0, NULL);

  int pixels = small->width * small->height;
  int min_black = pixels / 16;
  int max_black = pixels / 2;
  int totals[256] = {0};

  int best_d = pixels + 1;
  int best_t = 0;

  /* look for a low region in the histogram between the two peaks.
     (big assumption: two peaks, with most in whiter peak) */
  int total = 0;
  for (int i = 0; i < 255; i++){
    int v = (int)cvQueryHistValue_1D(hist, i);
    total += v;
    totals[i] = total;
    if (total >= min_black){
      if (i >= 5){
        int diff = total - totals[i - 5];
        if (diff < best_d){
          best_d = diff;
          best_t = i - 2;
        }
        if (total >= max_black){
          break;
        }
      }
    }
  }
  GST_DEBUG("found best threshold %d -- %d pixel change at %d/%d pixels\n",
      best_t, best_d, totals[best_t], pixels);

  cvReleaseImage(&small);
  cvReleaseHist(&hist);

  return best_t;
}

