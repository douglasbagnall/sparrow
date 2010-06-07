#include "sparrow.h"
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <stdio.h>

#include "cv.h"
#include "highgui.h"

#define debug(format, ...) fprintf (stderr, (format),## __VA_ARGS__); fflush(stderr)
#define debug_lineno() debug("%-25s  line %4d \n", __func__, __LINE__ )

#include "edge.c"



int main(int argc, char **argv)
{

  GstSparrow sparrow = {0};
  read_edges_info(&sparrow, argv[1]);

  sparrow_format in;
  sparrow_format out;
  


  return 0;
}

