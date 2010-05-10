#include "sparrow.h"
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <stdio.h>

#include "cv.h"
#include "highgui.h"

#define debug(format, ...) fprintf (stderr, (format),## __VA_ARGS__); fflush(stderr)
#define debug_lineno() debug("%-25s  line %4d \n", __func__, __LINE__ )


#define IMG_IN_NAME "images/test-image.png"
#define IMG_OUT_NAME "images/test-image-%s.png"


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



static UNUSED IplImage*
test_find_edges_hist(IplImage *im)
{
  int w = im->width;
  int h = im->height;
  CvSize small_size = {w / 8, h / 8};
  CvPoint middle = {w/2, h/2};

  IplImage *small = cvCreateImage(small_size, IPL_DEPTH_8U, 1); /*for quicker histogram */
  IplImage *mask = cvCreateImage(cvGetSize(im), IPL_DEPTH_8U, 1);
  IplImage *green = cvCreateImage(cvGetSize(im), IPL_DEPTH_8U, 1);
  cvSplit(im, NULL, green, NULL, NULL);

  cvResize(green, small, CV_INTER_NN);
  //small = green;

  int hist_size[] = {255};
  float range[] = {0, 255};
  float *ranges[] = {range};
  CvHistogram* hist = cvCreateHist(1, hist_size, CV_HIST_ARRAY, ranges, 1);
  cvCalcHist(&small, hist, 0, NULL);

  int pixels = small->width * small->height;
  int min_black = pixels / 8;
  int max_black = pixels / 2;
  int totals[256] = {0};

  int best_d = pixels + 1;
  int best_t = 0;

  int total = 0;
  for (int i = 0; i < 255; i++){
    int v = (int)cvQueryHistValue_1D(hist, i + 2);
    total += v;
    totals[i] = total;
    if (total > min_black){
      if (i > 5){
        int diff = totals[i] - totals[i - 5];
        if (diff < best_d){
          best_d = diff;
          best_t = i;
        }
        if (total >= max_black){
          break;
        }
      }
    }
  }
  best_t -= 2;
  printf("found best threshold %d -- %d pixel change at %d/%d pixels\n",
      best_t, best_d, totals[best_t], pixels);

  cvCmpS(green, best_t, mask, CV_CMP_GT);
  IplImage *mask2 = cvCreateImage(cvGetSize(im), IPL_DEPTH_8U, 1);
  memset(mask2->imageData, 255, w*h);
  floodfill_mono_superfast(mask, mask2, middle);
  return mask2;
}



int main(int argc, char **argv)
{
  struct timeval tv1, tv2;
  guint32 t;

  IplImage *im_in = cvLoadImage(IMG_IN_NAME, CV_LOAD_IMAGE_UNCHANGED);
  //IplImage *im_out = cvCreateImage(CvSize size, int depth, int channels);
  //IplImage *im_out = cvCloneImage(im);

  gettimeofday(&tv1, NULL);


  IplImage *im_out = test_find_edges_hist(im_in);
  //IplImage *im_out = test_find_edges(im_in);


  gettimeofday(&tv2, NULL);
  t = ((tv2.tv_sec - tv1.tv_sec) * 1000000 +
      tv2.tv_usec - tv1.tv_usec);
  printf("took %u microseconds (%0.5f of a frame)\n",
      t, (double)t * (25.0 / 1000000.0));


  char *filename;
  if(asprintf(&filename, IMG_OUT_NAME, "final") == -1){};
  cvSaveImage(filename, im_out, 0);
  return EXIT_SUCCESS;
}
