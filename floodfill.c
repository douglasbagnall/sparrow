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

#define STUPID_DEBUG_TRICK 0
#define WAIT_TIME CALIBRATE_MAX_T + 5

typedef struct sparrow_find_screen_s {
  IplImage *green;
  IplImage *working;
  IplImage *mask;
  IplImage *im;
  gboolean waiting;
  IplImage *signal;
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

static inline IplImage *
extract_green_channel(GstSparrow *sparrow, sparrow_find_screen_t *finder, guint8 *in)
{
  IplImage *im = finder->im;
  IplImage *green = finder->green;
  im->imageData = (char*)in;
  guint32 gshift = sparrow->in.gshift;
  GST_DEBUG("gshift is %d, green is %p, data is %p, im data is %p",
      gshift, green, green->imageData, im->imageData);
  cvSplit(im,
      (gshift == 24) ? green : NULL,
      (gshift == 16) ? green : NULL,
      (gshift ==  8) ? green : NULL,
      (gshift ==  0) ? green : NULL);
  GST_DEBUG("returning green %p, data %p",
      green, green->imageData);
  return green;
}


#define SIGNAL_THRESHOLD 100
/*see whether there seems to be activity:  */
gboolean INVISIBLE
check_for_signal(GstSparrow *sparrow, sparrow_find_screen_t *finder, guint8 *in){
  IplImage *green = extract_green_channel(sparrow, finder, in);
  IplImage *working = finder->working;
  guint i;
  gboolean answer = FALSE;
  cvAbsDiff(green, finder->working, finder->signal);
  for (i = 0; i < sparrow->in.pixcount; i++){
    if (finder->signal->imageData[i] > SIGNAL_THRESHOLD){
      answer = TRUE;
      break;
    }
  }
  memcpy(working->imageData, green->imageData, sparrow->in.pixcount);
  //char *tmp = working->imageData;
  //working->imageData = green->imageData;
  //green->imageData = tmp;
  GST_DEBUG("answering %d", answer);
  return answer;
}


/* a minature state progression within this one, in case the processing is too
   much for one frame.*/
INVISIBLE sparrow_state
mode_find_screen(GstSparrow *sparrow, guint8 *in, guint8 *out){
  sparrow->countdown--;
  GST_DEBUG("in find_screen with countdown %d\n", sparrow->countdown);
  sparrow_find_screen_t *finder = (sparrow_find_screen_t *)sparrow->helper_struct;
  IplImage *green;
  IplImage *working = finder->working;
  IplImage *mask = finder->mask;
  /* size is 1 byte per pixel, not 4! */
  size_t size = sparrow->in.pixcount;
  CvPoint middle, corner;
  switch (sparrow->countdown){
  case 6:
  case 5:
  case 4:
  case 3:
    /*send white and wait for the picture to arrive back. */
    goto white;
  case 2:
    /* time to look and see if the screen is there.
       Look at the histogram of a single channel. */
    green = extract_green_channel(sparrow, finder, in);
    MAYBE_DEBUG_IPL(green);
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
    GST_DEBUG("checking for signal. sparrow countdown is %d", sparrow->countdown);
    if (check_for_signal(sparrow, finder, in)){
      sparrow->countdown = sparrow->lag + WAIT_TIME;
    }
    goto black;
  }
 white:
  memset(out, 255, sparrow->out.size);
  return SPARROW_STATUS_QUO;
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
  cvReleaseImage(&finder->signal);
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
  sparrow->countdown = sparrow->lag + WAIT_TIME;
  finder->waiting = TRUE;
  CvSize size = {sparrow->in.width, sparrow->in.height};
  finder->green = cvCreateImage(size, IPL_DEPTH_8U, 1);
  finder->working = cvCreateImage(size, IPL_DEPTH_8U, 1);
  finder->signal = cvCreateImage(size, IPL_DEPTH_8U, 1);
  finder->im = init_ipl_image(&sparrow->in, PIXSIZE);
  finder->mask  = init_ipl_image(&sparrow->in, 1);

  finder->mask->imageData = (char *)sparrow->screenmask;
  GST_DEBUG("init_find_screen: green %p, working %p, mask %p, im %p finder %p\n",
      finder->green, finder->working, finder->mask, finder->im, finder);
}

