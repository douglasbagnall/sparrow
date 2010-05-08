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
static void change_state(GstSparrow *sparrow, sparrow_state state);
static __inline__ gint mask_to_shift(guint32 mask);
static guint32 get_mask(GstStructure *s, char *mask_name);
static void init_debug(GstSparrow *sparrow);
static void rng_init(GstSparrow *sparrow, guint32 seed);
static void simple_negation(guint8 *bytes, guint size);
static void gamma_negation(GstSparrow *sparrow, guint8 *in, guint8 *out);
static __inline__ void init_one_square(GstSparrow *sparrow, sparrow_shape_t *shape);
static void calibrate_init_squares(GstSparrow *sparrow);
static void add_random_signal(GstSparrow *sparrow, guint8 *out);
static void calibrate_init_lines(GstSparrow *sparrow);
static __inline__ void horizontal_line(GstSparrow *sparrow, guint8 *out, guint32 y);
static __inline__ void vertical_line(GstSparrow *sparrow, guint8 *out, guint32 x);
static __inline__ void rectangle(GstSparrow *sparrow, guint8 *out, sparrow_shape_t *shape);
static void draw_shapes(GstSparrow *sparrow, guint8 *out);
static gboolean cycle_pattern(GstSparrow *sparrow);

static void debug_frame(GstSparrow *sparrow, guint8 *data, guint32 width, guint32 height);
static void ppm_dump(sparrow_format *rgb, guint8 *data, guint32 width, guint32 height, char *name);

static void see_grid(GstSparrow *sparrow, guint8 *in);
static void find_grid(GstSparrow *sparrow, guint8 *in, guint8 *out);
static void find_self(GstSparrow *sparrow, guint8 *in, guint8 *out);
static void extract_caps(sparrow_format *im, GstCaps *caps);
static __inline__ IplImage *init_ipl_image(sparrow_format *dim);


/*
#ifdef HAVE_LIBOIL
#include <liboil/liboil.h>
#include <liboil/liboilcpu.h>
#include <liboil/liboilfunction.h>
#endif
*/

static inline gint
mask_to_shift(guint32 mask){
  /*mask is big-endian, so these numbers are reversed */
  switch(mask){
  case 0x000000ff:
    return 24;
  case 0x0000ff00:
    return 16;
  case 0x00ff0000:
    return 8;
  case 0xff000000:
    return 0;
  }
  GST_WARNING("mask not byte aligned: %x\n", mask);
  return 0;
}

static guint32 get_mask(GstStructure *s, char *mask_name){
  gint32 mask;
  int res = gst_structure_get_int(s, mask_name, &mask);
  if (!res){
    GST_WARNING("No mask for '%s' !\n", mask_name);
  }
  return (guint32)mask;
}



static void
init_debug(GstSparrow *sparrow){
  if (!sparrow->debug_frame){
    sparrow->debug_frame = malloc_aligned_or_die(MAX(sparrow->in.size, sparrow->out.size));
  }
}

/*RNG code */

/*seed with -1 for automatic seed choice */
static void rng_init(GstSparrow *sparrow, guint32 seed){
  GST_DEBUG("in RNG init\n");
  if (seed == (guint32)-1){
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
gamma_negation(GstSparrow *sparrow, guint8 *in, guint8 *out){
  //guint i;
  //XXX  could try oil_tablelookup_u8
  //for (i = 0; i < size; i++){
  //  out[i] = sparrow_rgb_gamma_full_range_REVERSE[in[i]];
  // }
}


static inline void
init_one_square(GstSparrow *sparrow, sparrow_shape_t* shape){
    shape->shape = RECTANGLE;
    shape->w = CALIBRATE_SELF_SIZE;
    shape->h = CALIBRATE_SELF_SIZE;
    shape->x  = RANDINT(sparrow, sparrow->out.width / 8,
        sparrow->out.width * 7 / 8 - shape->w);
    shape->y  = RANDINT(sparrow, sparrow->out.height / 8,
        sparrow->out.height * 7 / 8 - shape->h);
}

static void calibrate_init_squares(GstSparrow *sparrow){
  int i;
  for (i = 0; i < MAX_CALIBRATE_SHAPES; i++){
    init_one_square(sparrow, &(sparrow->shapes[i]));
  }
}


static void add_random_signal(GstSparrow *sparrow, guint8 *out){
  int i;
  static sparrow_shape_t shapes[MAX_CALIBRATE_SHAPES];
  static int been_here = 0;
  static int countdown = 0;
  static int on = 0;
  if (! been_here){
    for (i = 0; i < MAX_CALIBRATE_SHAPES; i++){
      init_one_square(sparrow, &shapes[i]);
    }
    been_here = 1;
  }
  if (! countdown){
    on = ! on;
    countdown = on ? RANDINT(sparrow, CALIBRATE_ON_MIN_T, CALIBRATE_ON_MAX_T) :
      RANDINT(sparrow, CALIBRATE_ON_MIN_T, CALIBRATE_ON_MAX_T);
  }
  if (on){
    for (i = 0; i < MAX_CALIBRATE_SHAPES; i++){
      rectangle(sparrow, out, &shapes[i]);
    }
  }
  countdown--;
}


static void calibrate_init_lines(GstSparrow *sparrow){
  int i;
  sparrow_shape_t* shape = sparrow->shapes;
  for (i = 0; i < MAX_CALIBRATE_SHAPES; i++){
    shape[i].w = 1;
    shape[i].h = 1;
    shape[i].x = 0;
    shape[i].y = 0;
    shape[i].shape = NO_SHAPE;
  }
  /* shape[0] will be set to vertical or horizontal in due course */
}



/* in a noisy world, try to find the spot you control by stoping and watching
   for a while.
 */

static inline void
horizontal_line(GstSparrow *sparrow, guint8 *out, guint32 y){
  guint stride = sparrow->out.width * PIXSIZE;
  guint8 *line = out + y * stride;
  memset(line, 255, stride);
}

static inline void
vertical_line(GstSparrow *sparrow, guint8 *out, guint32 x){
  guint y;
  guint32 *p = (guint32 *)out;
  p += x;
  for(y = 0; y < (guint)(sparrow->out.height); y++){
    *p = -1;
    p += sparrow->out.width;
  }
}

static inline void
rectangle(GstSparrow *sparrow, guint8 *out, sparrow_shape_t *shape){
  guint y;
  guint stride = sparrow->out.width * PIXSIZE;
  guint8 *line = out + shape->y * stride + shape->x * PIXSIZE;
  for(y = 0; y < (guint)shape->h; y++){
    memset(line, 255, shape->w * PIXSIZE);
    line += stride;
  }
}



static void draw_shapes(GstSparrow *sparrow, guint8 *out){
  int i;
  sparrow_shape_t *shape;
  for (i = 0; i < MAX_CALIBRATE_SHAPES; i++){
    shape = sparrow->shapes + i;
    switch (shape->shape){
    case NO_SHAPE:
      goto done; /* an empty one ends the list */
    case VERTICAL_LINE:
      vertical_line(sparrow, out, shape->x);
      break;
    case HORIZONTAL_LINE:
      horizontal_line(sparrow, out, shape->x);
      break;
    case RECTANGLE:
      rectangle(sparrow, out, shape);
      break;
    case FULLSCREEN:
      memset(out, 255, sparrow->out.size);
      break;
    }
  }
 done:
  return;
}


static inline void
record_calibration(GstSparrow *sparrow, gint32 offset, int signal){
  //signal = (signal != 0);
  sparrow->lag_table[offset].record <<= 1;
  sparrow->lag_table[offset].record |= signal;
}

static inline void
colour_coded_pixel(guint32* pixel, guint32 lag, guint32 shift){
  if (shift < 18){
    shift >>= 1;
    if (shift == 0){
      *pixel = (guint32)-1;
    }
    else{
      shift--;
      guint32 c = lag_false_colour[lag];
      guint32 mask = (1 << (8 - shift)) - 1;
      mask |= (mask << 8);
      mask |= (mask << 16); //XXX LUT would be quicker
      c >>= shift;
      c &= mask;
      *pixel = c;
    }
  }
}


static inline char *
int64_to_binary_string(char *s, guint64 n){
  /* s should be a *65* byte array */
  int i;
  for (i = 0; i < 64; i++){
    s[i] = (n & (1ULL << (63 - i))) ? '*' : '.';
  }
  s[64] = 0;
  return s;
}


/*return 1 if a reasonably likely lag has been found */

static inline int
find_lag(GstSparrow *sparrow){
  int res = 0;
  guint i, j;
  guint32 *frame = (guint32 *)sparrow->debug_frame;
  if (sparrow->debug){
    memset(frame, 0, sparrow->in.size);
  }
  guint64 target_pattern = sparrow->lag_record;
  guint32 overall_best = (guint32)-1;
  guint32 overall_lag = 0;
  char pattern_debug[65];
  int votes[MAX_CALIBRATION_LAG] = {0};

  GST_DEBUG("pattern: %s %llx\n", int64_to_binary_string(pattern_debug, target_pattern),
      target_pattern);

  for (i = 0; i < sparrow->in.pixcount; i++){
    guint64 record = sparrow->lag_table[i].record;
    if (record == 0 || ~record == 0){
      /*ignore this one! it'll never usefully match. */
      //frame[i] = 0xffffffff;
      continue;
    }

    guint64 mask = ((guint64)-1) >> MAX_CALIBRATION_LAG;
    guint32 best = hamming_distance64(record, target_pattern, mask);
    guint32 lag = 0;


    for (j = 1; j < MAX_CALIBRATION_LAG; j++){
      /*latest frame is least significant bit
        >> pushes into future,
        << pushes into past
        record is presumed to be a few frames past
        relative to main record, so we push it back.
      */
      record <<= 1;
      mask <<= 1;
      guint32 d = hamming_distance64(record, target_pattern, mask);
      if (d < best){
        best = d;
        lag = j;
      }
    }
    if (sparrow->debug){
      colour_coded_pixel(&frame[i], lag, best);
    }

    if (best <= CALIBRATE_MAX_VOTE_ERROR){
      votes[lag] += 1 >> (CALIBRATE_MAX_VOTE_ERROR - best);
    }

    if (best < overall_best){
      overall_best = best;
      overall_lag = lag;
      char pattern_debug2[65];
      record = sparrow->lag_table[i].record;
      GST_DEBUG("Best now: lag  %u! error %u pixel %u\n"
          "record:  %s %llx\n"
          "pattern: %s %llx\n",
          overall_lag, overall_best, i,
          int64_to_binary_string(pattern_debug, record), record,
          int64_to_binary_string(pattern_debug2, target_pattern), target_pattern
      );
    }
  }

  if (sparrow->debug){
    debug_frame(sparrow, sparrow->debug_frame, sparrow->in.width, sparrow->in.height);
  }

  /*calculate votes winner, as a check for winner-takes-all */
  guint popular_lag;
  int popular_votes = -1;
  for (i = 0; i < MAX_CALIBRATION_LAG; i++){
    if(votes[i] > popular_votes){
      popular_votes = votes[i];
      popular_lag = i;
    }
    if (votes[i]){
      GST_DEBUG("%d votes for %d\n", votes[i], i);
    }
  }
  /*votes and best have to agree, and best has to be low */
  if (overall_best <= CALIBRATE_MAX_BEST_ERROR &&
      overall_lag == popular_lag){
    sparrow->lag = overall_lag;
    res = 1;
  }
  return res;
}



#define PPM_FILENAME_TEMPLATE "/tmp/sparrow_%05d.ppm"
#define PPM_FILENAME_LENGTH (sizeof(PPM_FILENAME_TEMPLATE) + 10)

static void
debug_frame(GstSparrow *sparrow, guint8 *data, guint32 width, guint32 height){
#if SPARROW_PPM_DEBUG
  char name[PPM_FILENAME_LENGTH];
  int res = snprintf(name, PPM_FILENAME_LENGTH, PPM_FILENAME_TEMPLATE, sparrow->frame_count);
  if (res > 0){
    ppm_dump(&(sparrow->in), data, width, height, name);
  }
#endif
}



static void
ppm_dump(sparrow_format *rgb, guint8 *data, guint32 width, guint32 height, char *name)
{
  guint i;
  FILE *fh = fopen(name, "w");
  guint32 size = width * height;
  fprintf(fh, "P6\n%u %u\n255\n", width, height);
  /* 4 cases: xBGR xRGB BGRx RGBx
     need to convert to 24bit R G B
     XX maybe could optimise some cases?
  */
  guint32 *p = (guint32 *)data;
  for (i = 0; i < size; i++){
    putc((*p >> rgb->rshift) & 255, fh);
    putc((*p >> rgb->gshift) & 255, fh);
    putc((*p >> rgb->bshift) & 255, fh);
    p++;
  }
  fflush(fh);
  fclose(fh);
}

static inline void
abs_diff(GstSparrow *sparrow, guint8 *a, guint8 *b, guint8 *target){
  sparrow->in_ipl[0]->imageData = (char*) a;
  sparrow->in_ipl[1]->imageData = (char*) b;
  sparrow->in_ipl[2]->imageData = (char*) target;
  cvAbsDiff(sparrow->in_ipl[0], sparrow->in_ipl[1], sparrow->in_ipl[2]);
}

static inline void
threshold(GstSparrow *sparrow, guint8 *frame, guint8 *target, guint threshold){
  sparrow->in_ipl[0]->imageData = (char*) frame;
  sparrow->in_ipl[1]->imageData = (char*) target;
  //cvAbsDiff(sparrow->in_ipl[0], sparrow->in_ipl[1], sparrow->in_ipl[2]);
  cvCmpS(sparrow->in_ipl[0], (double)threshold, sparrow->in_ipl[1], CV_CMP_GT);
}

static void
reset_find_self(GstSparrow *sparrow, gint first){
  if (first){
    calibrate_init_squares(sparrow);
    sparrow->countdown = CALIBRATE_INITIAL_WAIT;
  }
  else {
    sparrow->countdown = CALIBRATE_RETRY_WAIT;
  }
}

/*compare the frame to the new one. regions of change should indicate the
  square is about.
*/
static inline int
calibrate_find_square(GstSparrow *sparrow, guint8 *in){
  //GST_DEBUG("finding square\n");
  int res = 0;
  if(sparrow->prev_frame){
    //threshold(sparrow, in, sparrow->work_frame, 100);
    //debug_frame(sparrow, sparrow->in_frame, sparrow->in.width, sparrow->in.height);
    guint32 i;
    for (i = 0; i < sparrow->in.pixcount; i++){
      //possibly R, G, or B, but never A
      int signal = (in[i * PIXSIZE + 2] > CALIBRATE_SIGNAL_THRESHOLD);
      record_calibration(sparrow, i, signal);
    }
    if (sparrow->countdown == 0){
      res = find_lag(sparrow);
      if (res){
        GST_DEBUG("lag is set at %u! after %u cycles\n", sparrow->lag, sparrow->frame_count);
      }
      else {
        reset_find_self(sparrow, 0);
      }
    }
  }
  sparrow->countdown--;
  return res;
}


static gboolean cycle_pattern(GstSparrow *sparrow){
  gboolean on = sparrow->calibrate.on;
  if (sparrow->calibrate.wait == 0){
    on = !on;
    if (on){
      sparrow->calibrate.wait = RANDINT(sparrow, CALIBRATE_ON_MIN_T, CALIBRATE_ON_MAX_T);
    }
    else{
      sparrow->calibrate.wait = RANDINT(sparrow, CALIBRATE_OFF_MIN_T, CALIBRATE_OFF_MAX_T);
    }
    sparrow->calibrate.on = on;
    sparrow->calibrate.transitions++;
  }
  sparrow->calibrate.wait--;
  sparrow->lag_record = (sparrow->lag_record << 1) | on;
  //GST_DEBUG("lag record %llx, on %i\n", sparrow->lag_record, on);
  return on;
}

static void
see_grid(GstSparrow *sparrow, guint8 *in){
}

static void
find_grid(GstSparrow *sparrow, guint8 *in, guint8 *out){
  see_grid(sparrow, in);
  int on = cycle_pattern(sparrow);
  memset(out, 0, sparrow->out.size);
  if (on){
    draw_shapes(sparrow, out);
  }
}


static void
find_self(GstSparrow *sparrow, guint8 *in, guint8 *out){
  if(calibrate_find_square(sparrow, in)){
    change_state(sparrow, SPARROW_WAIT_FOR_GRID);
    return;
  }
  gboolean on = cycle_pattern(sparrow);
  memset(out, 0, sparrow->out.size);
  if (on){
    draw_shapes(sparrow, out);
  }
#if FAKE_OTHER_PROJECTION
  add_random_signal(sparrow, out);
#endif
}

/* wait for the other projector to stop changing: sufficient to look for no
   significant changes for as long as the longest pattern interval */

static int
wait_for_blank(GstSparrow *sparrow, guint8 *in, guint8 *out){
  guint32 i;
  abs_diff(sparrow, in, sparrow->prev_frame, sparrow->work_frame);
  for (i = 0; i < sparrow->in.pixcount; i++){
    guint32 signal = sparrow->work_frame[i * PIXSIZE + 2];  //possibly R, G, or B, but never A
    if (signal > CALIBRATE_SIGNAL_THRESHOLD){
      sparrow->countdown = WAIT_COUNTDOWN;
      break;
    }
  }
  memset(out, 0, sparrow->out.size);
  sparrow->countdown--;
  return (sparrow->countdown == 0);
}


static void
extract_caps(sparrow_format *im, GstCaps *caps)
{
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gst_structure_get_int(s, "width", &(im->width));
  gst_structure_get_int(s, "height", &(im->height));
  im->rmask = get_mask(s, "red_mask");
  im->rshift = mask_to_shift(im->rmask);
  im->gmask = get_mask(s, "green_mask");
  im->gshift = mask_to_shift(im->gmask);
  im->bmask = get_mask(s, "blue_mask");
  im->bshift = mask_to_shift(im->bmask);

  im->pixcount = im->width * im->height;
  im->size = im->pixcount * PIXSIZE;

  GST_DEBUG("\ncaps:\n%" GST_PTR_FORMAT, caps);
  GST_DEBUG("shifts: r %u g %u b %u\n", im->rshift, im->gshift, im->bshift);
  GST_DEBUG("dimensions: w %u h %u pix %u size %u\n", im->width, im->height,
      im->pixcount, im->size);
}

static inline IplImage *
init_ipl_image(sparrow_format *dim){
  CvSize size = {dim->width, dim->height};
  IplImage* im = cvCreateImageHeader(size, IPL_DEPTH_8U, PIXSIZE);
  return cvInitImageHeader(im, size, IPL_DEPTH_8U, PIXSIZE, 0, 8);
}

static void
calibrate_init_grid(GstSparrow *sparrow){}

static void
change_state(GstSparrow *sparrow, sparrow_state state)
{
  switch(state){
  case SPARROW_FIND_SELF:
    reset_find_self(sparrow, 1);
    break;
  case SPARROW_WAIT_FOR_GRID:
    break;
  case SPARROW_FIND_GRID:
    calibrate_init_grid(sparrow);
    break;
  case SPARROW_INIT:
  case SPARROW_FIND_EDGES:
  case SPARROW_PLAY:
    break;
  }
  sparrow->state = state;
}



/*Functions below here are NOT static */

void INVISIBLE
sparrow_rotate_history(GstSparrow *sparrow, GstBuffer *inbuf){
  if (sparrow->in_buffer){
    gst_buffer_unref(sparrow->prev_buffer);
    sparrow->prev_buffer = sparrow->in_buffer;
    sparrow->prev_frame = sparrow->in_frame;
  }
  gst_buffer_ref(inbuf);
  sparrow->in_buffer = inbuf;

  sparrow->in_frame = GST_BUFFER_DATA(inbuf);
}

/* called by gst_sparrow_init() */
void INVISIBLE
sparrow_pre_init(GstSparrow *sparrow){
}

/* called by gst_sparrow_set_caps() */
gboolean INVISIBLE
sparrow_init(GstSparrow *sparrow, GstCaps *incaps, GstCaps *outcaps){
  extract_caps(&(sparrow->in), incaps);
  extract_caps(&(sparrow->out), outcaps);
  sparrow_format *in = &(sparrow->in);

  GST_DEBUG("allocating %u * %u for lag_table\n", in->pixcount, sizeof(lag_times_t));
  sparrow->lag_table = zalloc_aligned_or_die(in->pixcount * sizeof(lag_times_t));
  sparrow->work_frame = zalloc_aligned_or_die(in->size);
  sparrow->dsfmt = zalloc_aligned_or_die(sizeof(dsfmt_t));

  sparrow->prev_buffer = gst_buffer_new_and_alloc(in->size);
  sparrow->prev_frame  = GST_BUFFER_DATA(sparrow->prev_buffer);
  memset(sparrow->prev_frame, 0, in->size);

  /*initialise IPL structs for openCV */
  for (int i = 0; i < 3; i++){
    sparrow->in_ipl[i] = init_ipl_image(&(sparrow->in));
  }

  rng_init(sparrow, sparrow->rng_seed);

  if (sparrow->debug){
    init_debug(sparrow);
  }

  change_state(sparrow, SPARROW_FIND_SELF);
  return TRUE;
}

/*called by gst_sparrow_transform_ip */
void INVISIBLE
sparrow_transform(GstSparrow *sparrow, guint8 *in, guint8 *out)
{
  switch(sparrow->state){
  case SPARROW_FIND_SELF:
    find_self(sparrow, in, out);
    break;
  case SPARROW_WAIT_FOR_GRID:
    if (wait_for_blank(sparrow, in, out)){
      change_state(sparrow, SPARROW_FIND_GRID);
    }
    break;
  case SPARROW_FIND_GRID:
    find_grid(sparrow, in, out);
    break;
  default:
    gamma_negation(sparrow, in, out);
  }
  sparrow->frame_count++;
}

void
INVISIBLE
sparrow_finalise(GstSparrow *sparrow)
{
  //free everything


  //cvReleaseImageHeader(IplImage** image)
}
