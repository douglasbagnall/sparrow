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

/* static functions (via `make cproto`) */
static void init_debug(GstSparrow *sparrow);
static void rng_init(GstSparrow *sparrow, guint32 seed);
static void simple_negation(guint8 *bytes, guint size);
static void gamma_negation(guint8 *bytes, guint size);
static void calibrate_new_pattern(GstSparrow *sparrow);
static void calibrate_new_state(GstSparrow *sparrow);
static void debug_frame(GstSparrow *sparrow, guint8 *data);
static guint32 get_mask(GstStructure *s, char *mask_name);
static void pgm_dump(GstSparrow *sparrow, guint8 *data, char *name);
static int cycle_pattern(GstSparrow *sparrow, int repeat);
static void see_grid(GstSparrow *sparrow, guint8 *bytes);

/*
#ifdef HAVE_LIBOIL
#include <liboil/liboil.h>
#include <liboil/liboilcpu.h>
#include <liboil/liboilfunction.h>
#endif
*/

static void
init_debug(GstSparrow *sparrow){
  sparrow->debug_frame = malloc_aligned_or_die(sparrow->size);
}

/*RNG code */

/*seed with -1 for automatic seed choice */
static void rng_init(GstSparrow *sparrow, guint32 seed){
  GST_DEBUG("in RNG init\n");
  if (seed == -1){
    /* XXX should really use /dev/urandom */
    seed = rand();
    GST_DEBUG("Real seed %u\n", seed);
  }
  if (seed == 0)
    seed = 12345;

  dsfmt_init_gen_rand(sparrow->dsfmt, seed);
  dsfmt_gen_rand_all(sparrow->dsfmt);
  GST_DEBUG("RNG seeded with %u\n", seed);
}

/* here we go */

UNUSED
static void
simple_negation(guint8 * bytes, guint size){
  guint i;
  guint32 * data = (guint32 *)bytes;
  //could use sse for superspeed
  for (i = 0; i < size / 4; i++){
    data[i] = ~data[i];
  }
}

static void
gamma_negation(guint8 * bytes, guint size){
  guint i;
  //XXX  could try oil_tablelookup_u8
  for (i = 0; i < size; i++){
    bytes[i] = sparrow_rgb_gamma_full_range_REVERSE[bytes[i]];
  }
}

static void calibrate_new_pattern(GstSparrow *sparrow){
  int i;
  sparrow->calibrate_index = CALIBRATE_PATTERN_L;
  sparrow->calibrate_wait = 0;
  for (i = 0; i < CALIBRATE_PATTERN_L; i++){
    sparrow->calibrate_pattern[i] = RANDINT(sparrow, CALIBRATE_MIN_T, CALIBRATE_MAX_T);
  }
  GST_DEBUG("New Pattern: wait %u, index %u\n", sparrow->calibrate_wait, sparrow->calibrate_index);
}

static void calibrate_new_state(GstSparrow *sparrow){
  int edge_state = (sparrow->state == SPARROW_FIND_EDGES);

  if (edge_state){
    sparrow->calibrate_size = RANDINT(sparrow, 1, 8);
    sparrow->calibrate_x  = RANDINT(sparrow, 0, sparrow->width - sparrow->calibrate_size);
    sparrow->calibrate_y  = RANDINT(sparrow, 0, sparrow->height - sparrow->calibrate_size);
  }
  else {
    sparrow->calibrate_size = CALIBRATE_SELF_SIZE;
    sparrow->calibrate_x  = RANDINT(sparrow, sparrow->width / 4,
        sparrow->width * 3 / 4 - sparrow->calibrate_size);
    sparrow->calibrate_y  = RANDINT(sparrow, sparrow->height / 4,
        sparrow->height * 3 / 4 - sparrow->calibrate_size);
  }
}



/* in a noisy world, try to find the spot you control by stoping and watching
   for a while.
 */

static inline void
horizontal_line(GstSparrow *sparrow, guint8 *bytes, guint32 y){
  guint stride = sparrow->width * PIXSIZE;
  guint8 * line = bytes + y * stride;
  memset(line, 255, stride);
}

static inline void
vertical_line(GstSparrow *sparrow, guint8 *bytes, guint32 x){
  guint y;
  guint32 *p = (guint32 *)bytes;
  p += x;
  for(y = 0; y < sparrow->height; y++){
    *p = -1;
    p += sparrow->width;
  }
}

static inline void
draw_first_square(GstSparrow *sparrow, guint8 *bytes){
  guint y;
  guint stride = sparrow->width * PIXSIZE;
  guint8 * line = bytes + sparrow->calibrate_y * stride + sparrow->calibrate_x * PIXSIZE;
  for(y = 0; y < sparrow->calibrate_size; y++){
    memset(line, 255, sparrow->calibrate_size * PIXSIZE);
    line += stride;
  }
}


static inline void
record_calibration(GstSparrow *sparrow, gint32 offset, guint32 signal){
  guint16 *t = sparrow->lag_table[offset];
  guint32 r = sparrow->lag_record;
  while(r){
    if(r & 1){
      *t += signal;
    }
    r >>= 1;
    t++;
  }
}

static inline void
debug_calibration(GstSparrow *sparrow){
  int pixels = sparrow->width * sparrow->height;
  guint32 *frame = (guint32 *)sparrow->debug_frame;
  int i, j;
  for (i = 0; i < pixels; i++){
    guint16 peak = 0;
    int offset = 0;
    for(j = 0; j < MAX_CALIBRATION_LAG; j++){
      if (sparrow->lag_table[i][j] > peak){
        peak = sparrow->lag_table[i][j];
        offset = j;
      }
    }
    frame[i] = lag_false_colour[offset];
  }
  debug_frame(sparrow, sparrow->debug_frame);
}

#define PPM_FILENAME_TEMPLATE "/tmp/sparrow_%05d.pgm"
#define PPM_FILENAME_LENGTH (sizeof(PPM_FILENAME_TEMPLATE) + 10)

static void
debug_frame(GstSparrow *sparrow, guint8 *data){
#if SPARROW_PPM_DEBUG
  char name[PPM_FILENAME_LENGTH];
  int res = snprintf(name, PPM_FILENAME_LENGTH, PPM_FILENAME_TEMPLATE, sparrow->debug_count);
  if (res > 0){
    pgm_dump(sparrow, data, name);
  }
  sparrow->debug_count++;
#endif
}

#define NEWLINE 10

static inline guint32
trailing_zeros(guint32 mask){
  if (!mask){
    return 32;
  }
  guint32 shift = 0;
  //while(! ((mask >> shift) & 1)){
  while(! ((mask << shift) & 1 << 31)){
    shift ++;
  }
  return shift;
}

static guint32 get_mask(GstStructure *s, char *mask_name){
  gint32 mask;
  int res = gst_structure_get_int(s, mask_name, &mask);
  if (!res){
    GST_WARNING("No mask for '%s' !\n", mask_name);
  }
  return mask;
}



static void
pgm_dump(GstSparrow *sparrow, guint8 *data, char *name)
{
  int x, y;
  FILE *fh = fopen(name, "w");
  fprintf(fh, "P6\n%u %u\n255\n", sparrow->width, sparrow->height);


  GstPad *pad = GST_BASE_TRANSFORM_SINK_PAD(sparrow);
  GstCaps *caps = GST_PAD_CAPS(pad);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  guint32 rmask = get_mask(s, "red_mask");
  guint32 rshift = trailing_zeros(rmask);
  guint32 gmask = get_mask(s, "green_mask");
  guint32 gshift = trailing_zeros(gmask);
  guint32 bmask = get_mask(s, "blue_mask");
  guint32 bshift = trailing_zeros(bmask);
  //GST_DEBUG("r %u g %u b %u\n", rshift, gshift, bshift);

  /* 4 cases: xBGR xRGB BGRx RGBx
     it needs to be converted to 24bit R G B
  */
  guint32 *p = (guint32 *)data;
  for (y=0; y < sparrow->height; y++){
    for (x = 0; x < sparrow->width; x++){
      //putc((x & rmask) >> rshift, fh);
      //putc((x & gmask) >> gshift, fh);
      //putc((x & bmask) >> bshift, fh);
      putc((*p >> rshift) & 255, fh);
      putc((*p >> gshift) & 255, fh);
      putc((*p >> bshift) & 255, fh);
      p++;
    }
    //putc(NEWLINE, fh);
  }
  fflush(fh);
  fclose(fh);
}



static inline IplImage*
ipl_wrap_frame(GstSparrow *sparrow, guint8 *data){
  /*XXX could keep a cache of IPL headers */
  CvSize size = {sparrow->width, sparrow->height};
  IplImage* ipl = cvCreateImageHeader(size, IPL_DEPTH_8U, PIXSIZE);
  int i;
  for (i = 0; i < IPL_IMAGE_COUNT; i++){
    ipl = sparrow->ipl_images + i;
    if (ipl->imageData == NULL){
      cvInitImageHeader(ipl, size, IPL_DEPTH_8U, PIXSIZE, 0, 8);
      ipl->imageData = (char*)data;
      return ipl;
    }
  }
  DISASTEROUS_CRASH("no more ipl images! leaking somewhere?\n");
  return NULL; //never reached, but shuts up warning.
}

static inline void
ipl_free(IplImage *ipl){
  ipl->imageData = NULL;
}

/*compare the frame to the new one. regions of change should indicate the
  square is about.
*/
static inline void
calibrate_find_square(GstSparrow *sparrow, guint8 *bytes){
  //GST_DEBUG("finding square\n");
  if(sparrow->prev_frame){
    IplImage* src1 = ipl_wrap_frame(sparrow, sparrow->prev_frame);
    IplImage* src2 = ipl_wrap_frame(sparrow, bytes);
    IplImage* dest = ipl_wrap_frame(sparrow, sparrow->work_frame);

    cvAbsDiff(src1, src2, dest);

    gint32 i;
    pix_t *changes = (pix_t *)sparrow->work_frame;
    for (i = 0; i < sparrow->height * sparrow->width; i++){
      pix_t p = changes[i];
      guint32 signal = (p >> 8) & 255; //possibly R, G, or B, but never A
      if (signal > CALIBRATE_SIGNAL_THRESHOLD){
        record_calibration(sparrow, i, signal);
      }
    }
    memcpy(sparrow->prev_frame, bytes, sparrow->size);
    if(sparrow->debug){
      debug_calibration(sparrow);
      //debug_frame(sparrow, sparrow->work_frame);
    }
    ipl_free(src1);
    ipl_free(src2);
    ipl_free(dest);
  }
}

static int cycle_pattern(GstSparrow *sparrow, int repeat){
  if (sparrow->calibrate_wait == 0){
    if(sparrow->calibrate_index == 0){
      //pattern has run out
      if (repeat){
        sparrow->calibrate_index = CALIBRATE_PATTERN_L;
      }
      else{
        calibrate_new_pattern(sparrow);
      }
    }
    sparrow->calibrate_index--;
    sparrow->calibrate_wait = sparrow->calibrate_pattern[sparrow->calibrate_index];
    //GST_DEBUG("cycle_wait %u, cycle_index %u\n", sparrow->calibrate_wait, sparrow->calibrate_index);
    sparrow->lag_record = (sparrow->lag_record << 1) | 1;
  }
  else {
    sparrow->lag_record = (sparrow->lag_record << 1);
  }
  //XXX record the pattern in sparrow->lag_record
  //GST_DEBUG("lag_record %x calibrate_wait %x\n", sparrow->lag_record, sparrow->calibrate_wait);
  //GST_DEBUG("cycle_wait %u, cycle_index %u\n", sparrow->calibrate_wait, sparrow->calibrate_index);

  sparrow->calibrate_wait--;
  return sparrow->calibrate_index & 1;
}

static void
see_grid(GstSparrow *sparrow, guint8 *bytes){
}

static inline void
find_grid(GstSparrow *sparrow, guint8 *bytes){
  see_grid(sparrow, bytes);
  int on = cycle_pattern(sparrow, TRUE);
  memset(bytes, 0, sparrow->size);
  if (on){
    horizontal_line(sparrow, bytes, sparrow->calibrate_y);
  }
}

static inline void
find_edges(GstSparrow *sparrow, guint8 *bytes){
  calibrate_find_square(sparrow, bytes);
  int on = cycle_pattern(sparrow, TRUE);
  memset(bytes, 0, sparrow->size);
  if (on){
    draw_first_square(sparrow, bytes);
  }
}

static inline void
find_self(GstSparrow * sparrow, guint8 * bytes){
  calibrate_find_square(sparrow, bytes);
  int on = cycle_pattern(sparrow, TRUE);
  memset(bytes, 0, sparrow->size);
  if (on){
    //vertical_line(sparrow, bytes, sparrow->calibrate_x);
    //horizontal_line(sparrow, bytes, sparrow->calibrate_y);
    draw_first_square(sparrow, bytes);
  }
}


/*Functions below here are NOT static */

/* called by gst_sparrow_init() */
void sparrow_pre_init(GstSparrow *sparrow){
}

/* called by gst_sparrow_set_caps() */
void sparrow_init(GstSparrow *sparrow){
  size_t pixcount = sparrow->width * sparrow->height;
  sparrow->size = pixcount * PIXSIZE;

  sparrow->lag_table = zalloc_aligned_or_die(pixcount * sizeof(lag_times));
  sparrow->prev_frame = zalloc_aligned_or_die(sparrow->size);
  sparrow->work_frame = zalloc_aligned_or_die(sparrow->size);
  sparrow->dsfmt = zalloc_aligned_or_die(sizeof(dsfmt_t));

  rng_init(sparrow, sparrow->rng_seed);

  if (sparrow->debug){
    init_debug(sparrow);
  }

  GST_DEBUG("allocating %u * *u for lag_table\n", pixcount, sizeof(lag_times));

  sparrow->state = SPARROW_FIND_SELF;

  calibrate_new_pattern(sparrow);
  calibrate_new_state(sparrow);
}

/*called by gst_sparrow_transform_ip */
void sparrow_transform(GstSparrow *sparrow, guint8 *bytes)
{
  switch(sparrow->state){
  case SPARROW_FIND_SELF:
    find_self(sparrow, bytes);
    break;
  case SPARROW_FIND_EDGES:
    find_edges(sparrow, bytes);
    break;
  case SPARROW_FIND_GRID:
    find_grid(sparrow, bytes);
    break;
  default:
    gamma_negation(bytes, sparrow->size);
  }
}

void sparrow_finalise(GstSparrow *sparrow)
{
  //free everything



}
