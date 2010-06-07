#include "sparrow.h"
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <stdio.h>

#include "cv.h"
#include "highgui.h"

#define debug(format, ...) fprintf (stderr, (format),## __VA_ARGS__); fflush(stderr)
#define debug_lineno() debug("%-25s  line %4d \n", __func__, __LINE__ )


#define IMG_IN_NAME "images/mask.png"
#define IMG_OUT_NAME "images/test-image-%s.png"























int main(int argc, char **argv)
{
  struct timeval tv1, tv2;
  guint32 t;

  IplImage *mask = cvLoadImage(IMG_IN_NAME, CV_LOAD_IMAGE_UNCHANGED);
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
