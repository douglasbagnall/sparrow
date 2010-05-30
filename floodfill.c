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

#define STUPID_DEBUG_TRICK 1

typedef struct sparrow_find_screen_s {
  IplImage *green;
  IplImage *working;
  IplImage *mask;
  IplImage *im;
} sparrow_find_screen_t;


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

static IplImage*
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
  void * mem = malloc_or_die(w * h * 2 * sizeof(CvPoint));
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
static int
find_edges_threshold(IplImage *im)
{
  int w = im->width;
  int h = im->height;
  CvSize small_size = {w / 4, h / 4};
  IplImage *small = cvCreateImage(small_size, IPL_DEPTH_8U, 1); /*for quicker histogram (stupid, perhaps?)*/
  cvResize(im, small, CV_INTER_NN);
  int hist_size[] = {255};
  float range[] = {0, 255};
  float *ranges[] = {range};
  CvHistogram* hist = cvCreateHist(1, hist_size, CV_HIST_ARRAY, ranges, 1);
  cvCalcHist(&small, hist, 0, NULL);

  int pixels = small->width * small->height;
  int min_black = pixels / 16;
  int max_black = pixels * 3 / 4;
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
  //MAYBE_DEBUG_IPL(small);
  cvReleaseImage(&small);
  cvReleaseHist(&hist);

  return best_t;
}


/* a minature state progression within this one, in case the processing is too
   much for one frame.*/
INVISIBLE sparrow_state
mode_find_screen(GstSparrow *sparrow, guint8 *in, guint8 *out){
  sparrow->countdown--;
  GST_DEBUG("in find_screen with countdown %d\n", sparrow->countdown);
  sparrow_find_screen_t *finder = (sparrow_find_screen_t *)sparrow->helper_struct;
  IplImage *im = finder->im;
  IplImage *green = finder->green;
  IplImage *working = finder->working;
  IplImage *mask = finder->mask;
  /* size is 1 byte per pixel, not 4! */
  size_t size = sparrow->in.pixcount;
  CvPoint middle, corner;
  switch (sparrow->countdown){
  case 2:
    /* time to look and see if the screen is there.
       Look at the histogram of a single channel. */
    im->imageData = (char*)in;
    guint32 gshift = sparrow->in.gshift;
    cvSplit(im,
        (gshift == 24) ? green : NULL,
        (gshift == 16) ? green : NULL,
        (gshift ==  8) ? green : NULL,
        (gshift ==  0) ? green : NULL);
    //int best_t = find_edges_threshold(green);
    /*XXX if best_t is wrong, add to sparrow->countdown: probably the light is
      not really on.  But what counts as wrong? */
    //
    //cvCmpS(green, best_t, mask, CV_CMP_GT);
    cvCanny(green, mask, 100, 170, 3);
    MAYBE_DEBUG_IPL(mask);
    goto black;
  case 1:
    /* floodfill where the screen is, removing outlying bright spots*/
    middle = (CvPoint){sparrow->in.width / 2, sparrow->in.height / 2};
    memset(working->imageData, 255, size);
    floodfill_mono_superfast(mask, working, middle);
    MAYBE_DEBUG_IPL(working);
    goto black;
  case 0:
    /* floodfill the border, removing onscreen dirt.*/
    corner = (CvPoint){0, 0};
    memset(mask->imageData, 255, size);
    floodfill_mono_superfast(working, mask, corner);
#if STUPID_DEBUG_TRICK
    cvErode(mask, mask, NULL, 9);
#endif
    MAYBE_DEBUG_IPL(mask);
    goto finish;
  default:
    /*send white and wait for the picture to arrive back. */
    memset(out, 255, sparrow->out.size);
    return SPARROW_STATUS_QUO;
  }
 black:
  memset(out, 0, sparrow->out.size);
  return SPARROW_STATUS_QUO;
 finish:
  memset(out, 0, sparrow->out.size);
  return SPARROW_NEXT_STATE;
}

INVISIBLE void
finalise_find_screen(GstSparrow *sparrow){
  sparrow_find_screen_t *finder = (sparrow_find_screen_t *)sparrow->helper_struct;
  GST_DEBUG("finalise_find_screen: green %p, working %p, mask %p, im %p finder %p\n",
      finder->green, finder->working, finder->mask, finder->im, finder);
  cvReleaseImage(&finder->green);
  cvReleaseImage(&finder->working);
  cvReleaseImageHeader(&finder->mask);
  cvReleaseImageHeader(&finder->im);
  free(finder);
}

INVISIBLE void
init_find_screen(GstSparrow *sparrow){
  sparrow_find_screen_t *finder = zalloc_aligned_or_die(sizeof(sparrow_find_screen_t));
  sparrow->helper_struct = (void *)finder;
  sparrow->countdown = sparrow->lag + 5;
  CvSize size = {sparrow->in.width, sparrow->in.height};
  finder->green = cvCreateImage(size, IPL_DEPTH_8U, 1);
  finder->working = cvCreateImage(size, IPL_DEPTH_8U, 1);

  finder->im = init_ipl_image(&sparrow->in, PIXSIZE);
  finder->mask  = init_ipl_image(&sparrow->in, 1);

  finder->mask->imageData = (char *)sparrow->screenmask;
  GST_DEBUG("init_find_screen: green %p, working %p, mask %p, im %p finder %p\n",
      finder->green, finder->working, finder->mask, finder->im, finder);
}

