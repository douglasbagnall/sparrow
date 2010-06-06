#include "gstsparrow.h"
#include "sparrow.h"
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <stdio.h>

//#include "jpeg_src.c"

static const int INSIZE = 100000;
static const int OUTSIZE = 800 * 600 * 4;
static const char *FN_IN = "test.jpg";
static const char *FN_OUT = "test.ppm";
static const int cycles = 100;

int main(int argc, char **argv)
{
  guint8* inbuffer = malloc_aligned_or_die(INSIZE);
  guint8* outbuffer = malloc_aligned_or_die(OUTSIZE);

  FILE *in = fopen(FN_IN, "r");
  int size = fread(inbuffer, INSIZE, 1, in);

  struct timeval tv1, tv2;
  guint32 t;
  int width, height;

  GstSparrow sparrow;
  init_jpeg_src(&sparrow);

  gettimeofday(&tv1, NULL);

  for (int i = 0; i < cycles; i++){
    decompress_buffer(sparrow.cinfo, inbuffer, size, outbuffer,
        &width, &height);
  }

  gettimeofday(&tv2, NULL);
  t = ((tv2.tv_sec - tv1.tv_sec) * 1000000 +
      tv2.tv_usec - tv1.tv_usec);
  printf("average of %d took %u microseconds (%0.5f of a frame)\n",
      cycles, t / cycles, (double)t * (25.0 / 1000000.0 / cycles));


  sparrow_format rgb;
  rgb.rshift = 24;
  rgb.gshift = 16;
  rgb.bshift = 8;

  ppm_dump(&rgb, outbuffer, width, height, FN_OUT);

  finalise_jpeg_src(&sparrow);

}

