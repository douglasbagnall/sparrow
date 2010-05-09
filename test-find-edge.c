#include "sparrow.h"
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <stdio.h>

#include "cv.h"
#include "highgui.h"


#define IMG_IN_NAME "test-image.png"
#define IMG_OUT_NAME "test-image-%s.png"




static IplImage*
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

static IplImage*
test_find_edges(IplImage *im)
{
  /* find the colour of the centre. it gives an approximate value to use as
     threshold
   */ 
  int w = im->width;
  int h = im->height;
  CvSize size = {w, h};
  IplImage *green = cvCreateImage(size, IPL_DEPTH_8U, 1);
  IplImage *mask = cvCreateImage(size, IPL_DEPTH_8U, 1);
  IplImage *out = cvCreateImage(size, IPL_DEPTH_8U, 1);
  cvSplit(im, NULL, green, NULL, NULL);  
  cvSmooth(green, mask, CV_GAUSSIAN, 3, 0, 0, 0);
  cvSmooth(green, out, CV_GAUSSIAN, 5, 0, 0, 0);
  cvSub(out, mask, green, NULL);
  //cvErode(mask, out, NULL, 3);
  //cvDilate(out, mask, NULL, 3);

  //cvCanny(mask, out, 70, 190, 3);
  //cvSmooth(out, mask, CV_GAUSSIAN, 3, 0, 0, 0);
  //cvDistTransform(mask, out, CV_DIST_L2, 3, NULL, NULL);
  //CvPoint middle = {w/2, h/2};

  //CvScalar paint = cvScalarAll(99);
  //CvScalar margin = cvScalarAll(2);

  //cvFloodFill(mask, middle, paint, margin, margin, NULL, 4, NULL);
  return green;
  //return mask;
  //return out;

  //cvThreshold(mask, green, 50, 255, CV_THRESH_BINARY | CV_THRESH_OTSU);
}


int main(int argc, char **argv)
{
  struct timeval tv1, tv2;
  guint32 t;

  IplImage *im_in = cvLoadImage(IMG_IN_NAME, CV_LOAD_IMAGE_UNCHANGED);
  //IplImage *im_out = cvCreateImage(CvSize size, int depth, int channels);
  //IplImage *im_out = cvCloneImage(im);
  
  gettimeofday(&tv1, NULL);


  IplImage *im_out = test_find_edges(im_in);


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
