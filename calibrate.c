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


/*drawing*/

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
  int y, x;
  guint stride = sparrow->out.width;
  guint32 *line = ((guint32 *)out) + shape->y * stride + shape->x;
  for (x = 0; x < shape->w; x++){
    line[x] = sparrow->calibrate.outcolour;
  }
  guint32 *line2 = line + stride;
  for(y = 1; y < shape->h; y++){
    memcpy(line2, line, shape->w * PIXSIZE);
    line2 += stride;
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
init_one_square(GstSparrow *sparrow, sparrow_shape_t* shape){
    shape->shape = RECTANGLE;
    shape->w = CALIBRATE_SELF_SIZE;
    shape->h = CALIBRATE_SELF_SIZE;
    shape->x  = RANDINT(sparrow, sparrow->out.width / 8,
        sparrow->out.width * 7 / 8 - shape->w);
    shape->y  = RANDINT(sparrow, sparrow->out.height / 8,
        sparrow->out.height * 7 / 8 - shape->h);
}


/*fake other projection */
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


UNUSED
static void
init_lines(GstSparrow *sparrow){
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

static inline void
colour_coded_pixel(guint32* pixel, guint32 lag, guint32 shift){
#define CCP_SCALE 2
  if (shift < 9 * CCP_SCALE){
    shift /= CCP_SCALE;
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
      guint64 r = sparrow->lag_table[i].record;
      GST_DEBUG("Best now: lag  %u! error %u pixel %u\n"
          "record:  %s %llx\n"
          "pattern: %s %llx\n",
          overall_lag, overall_best, i,
          int64_to_binary_string(pattern_debug, r), r,
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

static inline void
record_calibration(GstSparrow *sparrow, gint32 offset, int signal){
  //signal = (signal != 0);
  sparrow->lag_table[offset].record <<= 1;
  sparrow->lag_table[offset].record |= signal;
}


INVISIBLE sparrow_state
mode_find_self(GstSparrow *sparrow, guint8 *in, guint8 *out){
  int ret = SPARROW_STATUS_QUO;
  guint32 i;
  guint32 *frame = (guint32 *)in;
  /* record the current signal */
  for (i = 0; i < sparrow->in.pixcount; i++){
    int signal = (((frame[i] >> sparrow->in.gshift) & 255) > CALIBRATE_SIGNAL_THRESHOLD);
    record_calibration(sparrow, i, signal);
  }
  if (sparrow->countdown == 0){
    /* analyse the signal */
    int r = find_lag(sparrow);
    if (r){
      GST_DEBUG("lag is set at %u! after %u cycles\n", sparrow->lag, sparrow->frame_count);
      ret = SPARROW_NEXT_STATE;
    }
    else {
      init_find_self(sparrow);
    }
  }

  memset(out, 0, sparrow->out.size);
  gboolean on = cycle_pattern(sparrow);
  if (on){
    draw_shapes(sparrow, out);
  }
#if FAKE_OTHER_PROJECTION
  add_random_signal(sparrow, out);
#endif
  sparrow->countdown--;
  return ret;
}


static inline void
abs_diff(GstSparrow *sparrow, guint8 *a, guint8 *b, guint8 *target){
  sparrow->in_ipl[0]->imageData = (char*) a;
  sparrow->in_ipl[1]->imageData = (char*) b;
  sparrow->in_ipl[2]->imageData = (char*) target;
  cvAbsDiff(sparrow->in_ipl[0], sparrow->in_ipl[1], sparrow->in_ipl[2]);
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
see_screen(GstSparrow *sparrow, guint8 *in){
  /* there is a big flash of white. or not.  look for the largest area of
     light.

     perhaps, threshold at one or two levels.  or more. if they agree they are
     probably right.

     or floodfill (and fill in)

     canny edge detection:
     http://opencv.willowgarage.com/documentation/c/feature_detection.html#canny

 */
  guint i;
  for (i = 0; i < sparrow->in.pixcount; i++){

    guint32 signal = sparrow->work_frame[i * PIXSIZE + 2];
    if (signal > CALIBRATE_WAIT_SIGNAL_THRESHOLD){
      sparrow->countdown = WAIT_COUNTDOWN;
      break;
    }
  }
}

INVISIBLE sparrow_state
mode_find_edges(GstSparrow *sparrow, guint8 *in, guint8 *out){
  return SPARROW_STATUS_QUO;
}

INVISIBLE sparrow_state
mode_find_screen(GstSparrow *sparrow, guint8 *in, guint8 *out){
  see_screen(sparrow, in);
  int on = cycle_pattern(sparrow);
  if (on){
    memset(out, 255, sparrow->out.size);
  }
  else {
    memset(out, 0, sparrow->out.size);
  }
  return SPARROW_STATUS_QUO;
}

/* wait for the other projector to stop changing, for as many frames as are
   set in sparrow->countdown.  When sparrow->countdown reaches 0, return 1.

   If sparrow->countdown is already 0, do nothing.

   If something happens, reset sparrow->countdown to <blanktime>.
*/
static int
wait_for_blank(GstSparrow *sparrow, guint8 *in, guint8 *out, int blanktime){
  if (sparrow->countdown){
    guint32 i;
    abs_diff(sparrow, in, sparrow->prev_frame, sparrow->work_frame);
    //Use the green channel
    for (i = (sparrow->in.gshift >> 8);
         i < sparrow->in.pixcount * PIXSIZE;
         i += 4){
      guint8 signal = sparrow->work_frame[i];
      if (signal > CALIBRATE_WAIT_SIGNAL_THRESHOLD){
        sparrow->countdown = blanktime;
        break;
      }
    }
    memset(out, 0, sparrow->out.size);
    sparrow->countdown--;
  }
  return (sparrow->countdown == 0);
}


static inline void
new_calibration_colour(GstSparrow *sparrow){
  int c = RANDINT(sparrow, 1, 3);
  sparrow->calibrate.incolour = sparrow->in.colours[c];
  sparrow->calibrate.outcolour = sparrow->out.colours[c];
  //sparrow->calibrate.wait == 0;
}

/* Choose between green or magenta, randomly and iteratively, until the
   other one chooses something else.  But first wait for blank? */

INVISIBLE sparrow_state
mode_pick_colour(GstSparrow *sparrow, guint8 *in, guint8 *out){
  if(wait_for_blank(sparrow, in, out, WAIT_COUNTDOWN)){
    new_calibration_colour(sparrow);
    sparrow->calibrate.wait = sparrow->lag + 2;
  }
  sparrow->calibrate.wait--;
  return SPARROW_STATUS_QUO;
}


INVISIBLE sparrow_state
mode_wait_for_grid(GstSparrow *sparrow, guint8 *in, guint8 *out){
  if (wait_for_blank(sparrow, in, out, WAIT_COUNTDOWN)){
    return SPARROW_NEXT_STATE;
  }
  return SPARROW_STATUS_QUO;
}

/*init functions */

INVISIBLE void
init_wait_for_grid(GstSparrow *sparrow){}

INVISIBLE void
init_find_grid(GstSparrow *sparrow){}

void INVISIBLE
init_find_self(GstSparrow *sparrow){
  sparrow->calibrate.incolour = sparrow->in.colours[SPARROW_WHITE];
  sparrow->calibrate.outcolour = sparrow->out.colours[SPARROW_WHITE];
  if (! sparrow->n_shapes){
    int i;
    for (i = 0; i < MAX_CALIBRATE_SHAPES; i++){
      init_one_square(sparrow, &(sparrow->shapes[i]));
    }
    sparrow->n_shapes = MAX_CALIBRATE_SHAPES;
    sparrow->countdown = CALIBRATE_INITIAL_WAIT;
  }
  else {
    sparrow->countdown = CALIBRATE_RETRY_WAIT;
  }
}

INVISIBLE void
init_pick_colour(GstSparrow *sparrow)
{
  sparrow->countdown = WAIT_COUNTDOWN;
}

INVISIBLE void
init_find_screen(GstSparrow *sparrow){
  //reset_pattern(GstSparrow *sparrow);
}

INVISIBLE void
init_find_edges(GstSparrow *sparrow){}
