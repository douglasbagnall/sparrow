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
static __inline__ gint mask_to_shift(guint32 mask);
static guint32 get_mask(GstStructure *s, char *mask_name);
static void init_debug(GstSparrow *sparrow);
static void rng_init(GstSparrow *sparrow, guint32 seed);
static void simple_negation(guint8 *bytes, guint size);
static void gamma_negation(GstSparrow *sparrow, guint8 *in, guint8 *out);
static void calibrate_new_pattern(GstSparrow *sparrow);
static __inline__ void init_one_square(GstSparrow *sparrow, sparrow_shape_t *shape);
static void calibrate_init_squares(GstSparrow *sparrow);
static void add_random_signal(GstSparrow *sparrow, guint8 *out);
static void calibrate_init_lines(GstSparrow *sparrow);
static __inline__ void horizontal_line(GstSparrow *sparrow, guint8 *out, guint32 y);
static __inline__ void vertical_line(GstSparrow *sparrow, guint8 *out, guint32 x);
static __inline__ void rectangle(GstSparrow *sparrow, guint8 *out, sparrow_shape_t *shape);
static void draw_shapes(GstSparrow *sparrow, guint8 *out);
static __inline__ void record_calibration(GstSparrow *sparrow, gint32 offset, guint32 signal);
static __inline__ void find_lag(GstSparrow *sparrow);
static __inline__ void debug_calibration_histogram(GstSparrow *sparrow);
static __inline__ void debug_calibration(GstSparrow *sparrow);
static void debug_frame(GstSparrow *sparrow, guint8 *data, guint32 width, guint32 height);
static void pgm_dump(sparrow_format *rgb, guint8 *data, guint32 width, guint32 height, char *name);
static __inline__ void calibrate_find_square(GstSparrow *sparrow, guint8 *in);
static int cycle_pattern(GstSparrow *sparrow, int repeat);
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

static void calibrate_new_pattern(GstSparrow *sparrow){
  int i;
  sparrow->calibrate.index = CALIBRATE_PATTERN_L;
  sparrow->calibrate.wait = 0;
  for (i = 0; i < CALIBRATE_PATTERN_L; i+=2){
    sparrow->calibrate.pattern[i] = RANDINT(sparrow, CALIBRATE_OFF_MIN_T, CALIBRATE_OFF_MAX_T);
    sparrow->calibrate.pattern[i + 1] = RANDINT(sparrow, CALIBRATE_ON_MIN_T, CALIBRATE_ON_MAX_T);
  }
  GST_DEBUG("New Pattern: wait %u, index %u\n", sparrow->calibrate.wait, sparrow->calibrate.index);
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
record_calibration(GstSparrow *sparrow, gint32 offset, guint32 signal){
  guint16 *t = sparrow->lag_table[offset].lag;
  sparrow->lag_table[offset].hits++;
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
find_lag(GstSparrow *sparrow){
  guint i, j;
  guint votes[MAX_CALIBRATION_LAG] = {0};

  if (sparrow->calibrate.transitions >= CALIBRATION_START_LAG_SEARCH){
    guint expected_hits_min = sparrow->calibrate.transitions * 3 / 4;
    guint expected_hits_max = sparrow->calibrate.transitions * 5 / 4;

    for (i = 0; i < sparrow->in.pixcount; i++){
      lag_times_t *lt = &(sparrow->lag_table[i]);
      if (lt->hits > CALIBRATION_MIN_HITS){
        guint32 sum = 0;
        guint16 peak = 0;
        int offset = 0;
        for(j = 0; j < MAX_CALIBRATION_LAG; j++){
          guint16 v = lt->lag[j];
          sum += v;
          if (v > peak){
            peak = v;
            offset = j;
          }
        }
        //XXX perhaps adjust centre according to neighbours, using sub-frame values (fixed point)
        lt->centre = offset;

        /* Heuristic for confidence:
           one point for each of:
           * the peak is twice average
           * the peak is 4/3 average
           * the number of hits is close to the number of transitions

           There of course is a proper way, but it might be slower.
        */
        guint32 peak2 = peak * MAX_CALIBRATION_LAG;
        lt->confidence = peak2 >> 1 >= sum;
        lt->confidence += ((peak2 * 3) >> 2) >= sum;
        if (lt->hits > expected_hits_min && lt->hits < expected_hits_max){
          lt->confidence *= 8;
        }
        votes[offset] += lt->confidence;
      }
    }
  }
  else {
    GST_DEBUG("transitions %u is less than threshold %u\n",
        sparrow->calibrate.transitions, CALIBRATION_START_LAG_SEARCH);
  }

  guint32 *frame = (guint32 *)sparrow->debug_frame;
  memset(frame, 0, sparrow->in.size);
  for (i = 0 ; i < sparrow->in.pixcount; i++){
    guint32 p = sparrow->lag_table[i].confidence;
    if (p){
      guint32 c = lag_false_colour[sparrow->lag_table[i].centre];
      if (p == 1){
        c >>= 2;
        c &= 0x3f3f3f3f;
      }
      if (p == 2){
        c >>= 1;
        c &= 0x7f7f7f7f;
      }
      frame[i] = c;
    }
  }
  /* find votes */
  guint max_vote;
  guint offset;
  for (j = 0; j < MAX_CALIBRATION_LAG; j++){
    if (votes[j] > max_vote){
      max_vote = votes[j];
      offset = j;
    }
  }
  if (max_vote){
    /*draw a histogram on the screen*/
    guint8 *row = sparrow->debug_frame;
    for (j = 0; j < MAX_CALIBRATION_LAG; j++){
      row += sparrow->in.width * PIXSIZE;
      memset(row, 255, votes[j] * sparrow->in.width * (PIXSIZE / 2) / max_vote);
    }
  }

  debug_frame(sparrow, sparrow->debug_frame, sparrow->in.width, sparrow->in.height);
}


static inline void
debug_calibration_histogram(GstSparrow *sparrow){
  int pixels = sparrow->in.pixcount;
  guint32 *frame = (guint32 *)sparrow->debug_frame;
  memcpy(sparrow->debug_frame, sparrow->in_frame, sparrow->in.size);
  int i, j;
  static guint32 high_peak = 0;
  static guint32 high_pix = 0;
  static guint32 high_offset = 0;
  for (i = 0; i < pixels; i++){
    guint16 peak = 0;
    int offset = 0;
    if (sparrow->lag_table[i].hits > CALIBRATION_MIN_HITS){
      for(j = 0; j < MAX_CALIBRATION_LAG; j++){
        if (sparrow->lag_table[i].lag[j] > peak){
          peak = sparrow->lag_table[i].lag[j];
          offset = j;
        }
      }
      if (peak > high_peak){
        high_peak = peak;
        high_pix = i;
        high_offset = offset;
      }
    }
  }
  if (high_peak){
    /*draw a histogram on the screen*/
    guint8 *row = sparrow->debug_frame;
    for (j = 0; j < MAX_CALIBRATION_LAG; j++){
      row += sparrow->in.width * PIXSIZE;
      memset(row, 255, sparrow->lag_table[high_pix].lag[j] * sparrow->in.width * (PIXSIZE / 2) / high_peak);
    }

    /*flicker most peaky pixel */
    if (sparrow->frame_count & 4){
      frame[high_pix] = (guint32)-1 & (sparrow->in.rmask);
    }
    else {
      frame[high_pix] = 0;
    }
  }
  debug_frame(sparrow, sparrow->debug_frame, sparrow->in.width, sparrow->in.height);
}


static inline void
debug_calibration(GstSparrow *sparrow){
  int pixels = sparrow->in.width * sparrow->in.height;
  guint32 *frame = (guint32 *)sparrow->debug_frame;
  int i, j;
  for (i = 0; i < pixels; i++){
    guint16 peak = 0;
    int offset = 0;
    if (sparrow->lag_table[i].hits > 5){
      for(j = 0; j < MAX_CALIBRATION_LAG; j++){
        if (sparrow->lag_table[i].lag[j] > peak){
          peak = sparrow->lag_table[i].lag[j];
          offset = j;
        }
      }
      guint32 c = lag_false_colour[offset];
      if (sparrow->lag_table[i].hits < 15){
        c >>= 2;
        c &= 0x3f3f3f3f;
      }
      else if (sparrow->lag_table[i].hits < 30){
        c >>= 1;
        c &= 0x7f7f7f7f;
      }
      if (sparrow->lag_table[i].hits > 90){
        c = (guint32)-1;
        //GST_DEBUG("%u: %u, ", i, sparrow->lag_table[i].hits);
      }
      frame[i] = c;
    }
    else{
      frame[i] = 0;
    }
  }
  debug_frame(sparrow, sparrow->debug_frame, sparrow->in.width, sparrow->in.height);
}

#define PPM_FILENAME_TEMPLATE "/tmp/sparrow_%05d.pgm"
#define PPM_FILENAME_LENGTH (sizeof(PPM_FILENAME_TEMPLATE) + 10)

static void
debug_frame(GstSparrow *sparrow, guint8 *data, guint32 width, guint32 height){
#if SPARROW_PPM_DEBUG
  char name[PPM_FILENAME_LENGTH];
  int res = snprintf(name, PPM_FILENAME_LENGTH, PPM_FILENAME_TEMPLATE, sparrow->frame_count);
  if (res > 0){
    pgm_dump(&(sparrow->in), data, width, height, name);
  }
#endif
}



static void
pgm_dump(sparrow_format *rgb, guint8 *data, guint32 width, guint32 height, char *name)
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


/*compare the frame to the new one. regions of change should indicate the
  square is about.
*/
static inline void
calibrate_find_square(GstSparrow *sparrow, guint8 *in){
  //GST_DEBUG("finding square\n");
  if(sparrow->prev_frame){
    IplImage* src1 = sparrow->in_ipl;
    IplImage* src2 = sparrow->prev_ipl;
    IplImage* dest = sparrow->work_ipl;

    src1->imageData = (char*) in;
    src2->imageData = (char*) sparrow->prev_frame;
    dest->imageData = (char*) sparrow->work_frame;

    cvAbsDiff(src1, src2, dest);

    guint32 i;
    //pix_t *changes = (pix_t *)sparrow->work_frame;
    for (i = 2; i < sparrow->in.pixcount; i++){ //possibly R, G, or B, but never A
      guint32 signal = sparrow->work_frame[i * PIXSIZE];
      if (signal > CALIBRATE_SIGNAL_THRESHOLD){
        record_calibration(sparrow, i, signal);
      }
    }
    if(sparrow->debug){
      //debug_calibration_histogram(sparrow);
      find_lag(sparrow);
    }
  }
}

static int cycle_pattern(GstSparrow *sparrow, int repeat){
  if (sparrow->calibrate.wait == 0){
    if(sparrow->calibrate.index == 0){
      //pattern has run out
      if (repeat){
        sparrow->calibrate.index = CALIBRATE_PATTERN_L;
      }
      else{
        calibrate_new_pattern(sparrow);
      }
    }
    sparrow->calibrate.index--;
    sparrow->calibrate.wait = sparrow->calibrate.pattern[sparrow->calibrate.index];
    //GST_DEBUG("cycle_wait %u, cycle_index %u\n", sparrow->calibrate.wait, sparrow->calibrate.index);
    sparrow->lag_record = (sparrow->lag_record << 1) | 1;
    sparrow->calibrate.transitions++;
  }
  else {
    sparrow->lag_record = (sparrow->lag_record << 1);
  }
  sparrow->lag_record &= ((1 << MAX_CALIBRATION_LAG) - 1);
  //XXX record the pattern in sparrow->lag_record
  sparrow->calibrate.wait--;
  return sparrow->calibrate.index & 1;
}

static void
see_grid(GstSparrow *sparrow, guint8 *in){
}

static void
find_grid(GstSparrow *sparrow, guint8 *in, guint8 *out){
  see_grid(sparrow, in);
  int on = cycle_pattern(sparrow, TRUE);
  memset(out, 0, sparrow->out.size);
  if (on){
    draw_shapes(sparrow, out);
  }
}


static void
find_self(GstSparrow *sparrow, guint8 *in, guint8 *out){
  calibrate_find_square(sparrow, in);
  int on = cycle_pattern(sparrow, TRUE);
  memset(out, 0, sparrow->out.size);
  if (on){
    draw_shapes(sparrow, out);
  }
#if 1
  add_random_signal(sparrow, out);
#endif
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
  sparrow->in_ipl   = init_ipl_image(&(sparrow->in));
  sparrow->prev_ipl = init_ipl_image(&(sparrow->in));
  sparrow->work_ipl = init_ipl_image(&(sparrow->in));

  rng_init(sparrow, sparrow->rng_seed);

  if (sparrow->debug){
    init_debug(sparrow);
  }

  sparrow->state = SPARROW_FIND_SELF;

  calibrate_new_pattern(sparrow);
  calibrate_init_squares(sparrow);
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
