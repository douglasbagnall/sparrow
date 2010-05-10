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

/* if point (x,y) is close enough to colour (r,g,b) in Euclidean RGB space,
   add it to the nexts list. otherwise, not.*/

static inline int
expand_one(int x, int y, int r, int g, int b,
    int threshold, CvPoint *nexts, int *n_nexts, guint32 *im, guint8 *mask, int w, int h){
  guint8 *p = (guint8*) &im[y * w + x];
  guint8 *m = &mask[y * w + x];
  //printf("looking at point %d, %d. rgb is %x.%x.%x, threshold %d, p %x %x %x mask %x\n", 
  //    x, y, r, g, b, threshold, p[0], p[1], p[2], *m);
  if (*m){
    int diff = ((p[0] - r) * (p[0] - r) +
        (p[1] - g) * (p[1] - g) +
        (p[2] - b) * (p[2] - b));
    //debug("diff %d\n", diff);
    if (diff  <= threshold){
      *m = 0;
      nexts[*n_nexts].x = x;
      nexts[*n_nexts].y = y;
      (*n_nexts)++;
      return 1;
    }
    else{
      *m = 128;
      //if (diff <= 255) /*for obviousness calculations, later */
      //  p[3] = diff;
    }
  }
  return 0;
}

static IplImage*
floodfill(IplImage *im, IplImage *mim, CvPoint start, int threshold)
{
  guint32 * data = (guint32 *)im->imageData;
  guint8 * mdata = (guint8 *)mim->imageData;
  int w = im->width;
  int h = im->height;
  int n_starts;
  int n_nexts = 0;
  CvPoint *starts;
  CvPoint *nexts;

  //malloc 2 lists of points. These *could* be as large as the image (but never should be)
  starts = malloc(w * h * 2 * sizeof(CvPoint));
  nexts = starts + w * h;

  n_starts = 1;
  starts[0] = start;

  /* expand with a wavefront, each point consuming its neighbours that
     differ from it by less than the threshold. If the neighbour is too
     different, it tries jumping ahead in that direction, to see if there is
     more on the other side. ("wavefront with sparks")*/
  while(n_starts){    
    n_nexts = 0;
    int i;
    int r, g, b;
    for (i = 0; i < n_starts; i++){
      int x = starts[i].x;
      int y = starts[i].y;
      //printf("looking at point %d, %d\n", x, y);
      guint8 *p = (guint8 *)&data[y * w + x];
      r = (int)p[0];
      g = (int)p[1];
      b = (int)p[2];
#define JUMPAHEAD 10
#define JA_THRESHOLD (threshold - 2)
      if (x > 0){
        if (! expand_one(x - 1, y, r, g, b, threshold, nexts, &n_nexts, data, mdata, w, h) &&
            x > JUMPAHEAD - 1)
          expand_one(x - JUMPAHEAD, y, r, g, b, JA_THRESHOLD, nexts, &n_nexts, data, mdata, w, h);
      }
      if (x < w - 1){
        if (! expand_one(x + 1, y, r, g, b, threshold, nexts, &n_nexts, data, mdata, w, h) &&
            x < w - JUMPAHEAD - 1)
          expand_one(x + JUMPAHEAD, y, r, g, b, JA_THRESHOLD, nexts, &n_nexts, data, mdata, w, h);
      }
      if (y > 0){
        if (! expand_one(x, y - 1, r, g, b, threshold, nexts, &n_nexts, data, mdata, w, h) &&
            y > JUMPAHEAD - 1)
          expand_one(x, y - JUMPAHEAD, r, g, b, JA_THRESHOLD, nexts, &n_nexts, data, mdata, w, h);
      }
      if (y < h - 1){
        if (! expand_one(x, y + 1, r, g, b, threshold, nexts, &n_nexts, data, mdata, w, h) &&
            y < h - JUMPAHEAD - 1)
          expand_one(x, y + JUMPAHEAD, r, g, b, JA_THRESHOLD, nexts, &n_nexts, data, mdata, w, h);
      }
    }
    CvPoint *tmp = starts;
    starts = nexts;
    nexts = tmp;
    n_starts = n_nexts;
  }
  free(starts < nexts ? starts : nexts);

  return im;
}





static IplImage* UNUSED
test_find_edges_gcg(IplImage *im)
{
  /* find the colour of the centre. it gives an approximate value to use as
     threshold
   */

  int w = im->width;
  int h = im->height;
  CvSize size = {w, h};
  CvPoint middle = {w/2, h/2};
  CvScalar paint = cvScalarAll(99);
  CvScalar margin = cvScalarAll(2);

  IplImage *green = cvCreateImage(size, IPL_DEPTH_8U, 1);
  IplImage *mask_simple = cvCreateImage(size, IPL_DEPTH_8U, 1);
  IplImage *out = cvCreateImage(size, IPL_DEPTH_8U, 1);
  cvSplit(im, NULL, green, NULL, NULL);
  
  cvSmooth(green, mask_simple, CV_GAUSSIAN, 3, 0, 0, 0);
  
  cvCanny(mask_simple, out, 70, 190, 3);
  cvSmooth(out, mask_simple, CV_GAUSSIAN, 3, 0, 0, 0);
  cvFloodFill(mask_simple, middle, paint, margin, margin, NULL, 
      4, NULL);
  return mask_simple;
  //return out;
}

static IplImage* UNUSED
test_find_edges_close_cmp(IplImage *im)
{
  /* find the colour of the centre. it gives an approximate value to use as
     threshold
   */ 
  int w = im->width;
  int h = im->height;
  CvSize size = {w, h};
  IplImage *green = cvCreateImage(size, IPL_DEPTH_8U, 1);
  IplImage *mask = cvCreateImage(size, IPL_DEPTH_8U, 1);
  //IplImage *out = cvCreateImage(size, IPL_DEPTH_8U, 1);
  cvSplit(im, NULL, green, NULL, NULL);  

  cvMorphologyEx(green, mask, NULL, NULL, CV_MOP_OPEN, 1);
  cvCmpS(mask, 88, mask, CV_CMP_GT);
  return mask;
}


static IplImage *sharpen(IplImage *im)
{
  IplImage* imf = cvCreateImage(cvGetSize(im), IPL_DEPTH_32F, 1);  
  IplImage* lapl = cvCreateImage(cvGetSize(im), IPL_DEPTH_32F, 1);  
  IplImage *out = cvCreateImage(cvGetSize(im), IPL_DEPTH_8U, 1);

  cvConvert(im, imf);
  
  CvMat* kernel = cvCreateMat(3, 3, CV_32FC1);
  cvSet(kernel, cvScalarAll(-1.0), NULL);
  cvSet2D(kernel, 1, 1, cvScalarAll(15.0));

  cvSmooth(imf, imf, CV_GAUSSIAN, 3, 0, 0, 0);  
  //cvFilter2D(imf, lapl, kernel, cvPoint(-1, -1));

  cvLaplace(imf, lapl, 3);
  
  double maxv = 0.0;
  double minv = DBL_MAX;
  CvPoint minl = {0, 0};
  CvPoint maxl = {0, 0};

  cvMinMaxLoc(lapl, &minv, &maxv, &minl, &maxl, NULL);
  double range = maxv - minv;
  double scale = 255.0 / range;

  printf("max %f at %d,%d; min %f at %d,%d.\n",
      maxv, maxl.x, maxl.y, minv, minl.x, minl.y);
  printf("range %f. scale %f\n", range, scale);

  cvConvertScale(lapl, out, scale, scale * -minv);


  cvMinMaxLoc(im, &minv, &maxv, &minl, &maxl, NULL);
  range = maxv - minv;
  scale = 255.0 / range;

  cvSub(im, out, im, NULL);

  printf("im max %f at %d,%d; min %f at %d,%d.\n",
      maxv, maxl.x, maxl.y, minv, minl.x, minl.y);
  printf("range %f. scale %f\n", range, scale);



  return im;
}

static IplImage* UNUSED
test_find_edges(IplImage *im)
{
  /* find the colour of the centre. it gives an approximate value to use as
     threshold
   */ 
  int w = im->width;
  int h = im->height;
  CvSize size = {w, h};
  IplImage *green = cvCreateImage(size, IPL_DEPTH_8U, 1);
  //IplImage *mask = cvCreateImage(size, IPL_DEPTH_8U, 1);
  //IplImage *out = cvCreateImage(size, IPL_DEPTH_8U, 1);
  cvSplit(im, NULL, green, NULL, NULL);  

  sharpen(green);
  //cvMorphologyEx(green, mask, NULL, NULL, CV_MOP_OPEN, 1);
  //cvCmpS(mask, 88, mask, CV_CMP_GT);
  //cvLaplace(green, mask, 3);

  //cvSmooth(green, mask, CV_GAUSSIAN, 3, 0, 0, 0);  
  //cvErode(mask, out, NULL, 2);  
  //cvDilate(out, mask, NULL, 2);

  //cvSmooth(green, out, CV_GAUSSIAN, 5, 0, 0, 0);
  //cvSub(out, mask, green, NULL);
  //cvCanny(mask, out, 70, 190, 3);
  //cvSmooth(out, mask, CV_GAUSSIAN, 3, 0, 0, 0);
  //cvDistTransform(mask, out, CV_DIST_L2, 3, NULL, NULL);
  //CvPoint middle = {w/2, h/2};

  //CvScalar paint = cvScalarAll(99);
  //CvScalar margin = cvScalarAll(2);
  //cvThreshold(mask, green, 50, 255, CV_THRESH_BINARY | CV_THRESH_OTSU);
  //cvFloodFill(mask, middle, paint, margin, margin, NULL, 4, NULL);
  return green;
  //return mask;
  //return out;
}

static IplImage*
test_find_edges_floodfill(IplImage *im){
  int w = im->width;
  int h = im->height;
  CvPoint centre = {w/2, h/2};
  CvSize size = {w, h};
  IplImage *rgba = cvCreateImage(size, IPL_DEPTH_8U, 4);
  cvCvtColor(im, rgba, CV_RGB2RGBA);

  IplImage *mask = cvCreateImage(size, IPL_DEPTH_8U, 1);
  debug_lineno();
  debug("%p %p\n",&(mask->imageData), mask->imageData);
  memset(mask->imageData, 255, w*h);
  debug_lineno();
  //cvSmooth(rgba, rgba, CV_GAUSSIAN, 3, 0, 0, 0);  

  floodfill(rgba, mask, centre, 32);


  //cvSplit(im, NULL, NULL, NULL, alpha);  
  //cvSplit(im, alpha, NULL, NULL, NULL);  
  
  return mask;
}

int main(int argc, char **argv)
{
  struct timeval tv1, tv2;
  guint32 t;

  IplImage *im_in = cvLoadImage(IMG_IN_NAME, CV_LOAD_IMAGE_UNCHANGED);
  //IplImage *im_out = cvCreateImage(CvSize size, int depth, int channels);
  //IplImage *im_out = cvCloneImage(im);
  
  gettimeofday(&tv1, NULL);


  IplImage *im_out = test_find_edges_floodfill(im_in);


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
