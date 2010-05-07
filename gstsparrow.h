/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) <2003> Arwed v. Merkatz <v.merkatz@gmx.net>
 * Copyright (C) <2006> Mark Nauwelaerts <manauw@skynet.be>
 * Copyright (C) <2010> Douglas Bagnall <douglas@halo.gen.nz>
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


#ifndef __GST_VIDEO_SPARROW_H__
#define __GST_VIDEO_SPARROW_H__

#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS

#define SPARROW_PPM_DEBUG 1


#include "sparrowconfig.h"
#include "dSFMT/dSFMT.h"
#include "cv.h"

#ifndef UNUSED
#define UNUSED __attribute__ ((unused))
#else
#warning UNUSED is set
#endif

/* the common recommendation is to default to 'hidden' and specifically mark
   the unhidden ('default') ones, but this might muck with gstreamer macros,
   some of which declare functions, and most sparrow functions are static
   anyway, so it is simpler to whitelist visibility.

   http://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html#index-fvisibility-2135

   (actually, it seems like all functions are invisible or static, except the
   ones that gstreamer makes in macros).
 */
#ifndef INVISIBLE
#define INVISIBLE __attribute__ ((visibility("hidden")))
#else
#warning INVISIBLE is set
#endif


typedef guint32 pix_t;
#define PIXSIZE (sizeof(pix_t))


#define CALIBRATE_ON_MIN_T 2
#define CALIBRATE_ON_MAX_T 7
#define CALIBRATE_OFF_MIN_T 2
#define CALIBRATE_OFF_MAX_T 9
#define CALIBRATE_PATTERN_L 100
#define CALIBRATE_SELF_SIZE 24


#define MAX_CALIBRATE_SHAPES 4

#define FAKE_OTHER_PROJECTION 0

#define WAIT_COUNTDOWN (MAX(CALIBRATE_OFF_MAX_T, CALIBRATE_ON_MAX_T) + 3)

#define GST_TYPE_SPARROW \
  (gst_sparrow_get_type())
#define GST_SPARROW(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SPARROW,GstSparrow))
#define GST_SPARROW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SPARROW,GstSparrowClass))
#define GST_IS_SPARROW(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SPARROW))
#define GST_IS_SPARROW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SPARROW))


#define MAX_CALIBRATION_LAG 16
typedef struct lag_times_s {
  //guint32 hits;
  guint64 record;
} lag_times_t;

typedef struct sparrow_format_s {
  gint32 width;
  gint32 height;
  guint32 pixcount;
  guint32 size;

  guint32 rshift;
  guint32 gshift;
  guint32 bshift;
  guint32 rmask;
  guint32 gmask;
  guint32 bmask;
} sparrow_format;

enum calibration_shape {
  NO_SHAPE = 0,
  VERTICAL_LINE,
  HORIZONTAL_LINE,
  FULLSCREEN,
  RECTANGLE,
};

typedef struct sparrow_shape_s {
  /*Calibration shape definition -- a rectangle or line.
   For lines, only one dimension is used.*/
  enum calibration_shape shape;
  gint x;
  gint y;
  gint w;
  gint h;
} sparrow_shape_t;

typedef struct sparrow_calibrate_s {
  /*calibration state, and shape and pattern definition */
  gboolean on;         /*for calibration pattern */
  gint wait;
  guint32 pattern[CALIBRATE_PATTERN_L];
  guint32 index;
  guint32 transitions;
} sparrow_calibrate_t;


typedef struct _GstSparrow GstSparrow;
typedef struct _GstSparrowClass GstSparrowClass;

typedef enum {
  SPARROW_INIT,
  SPARROW_FIND_SELF,
  SPARROW_WAIT_FOR_GRID,
  SPARROW_FIND_EDGES,
  SPARROW_FIND_GRID,
  SPARROW_PLAY,
} sparrow_state;


/**
 * GstSparrow:
 *
 * Opaque data structure.
 */
struct _GstSparrow
{
  GstVideoFilter videofilter;

  sparrow_format in;
  sparrow_format out;
  sparrow_shape_t shapes[MAX_CALIBRATE_SHAPES];
  sparrow_calibrate_t calibrate;

  /* properties */
  gint calibrate_flag;  /*whether to calibrate */

  /* misc */
  dsfmt_t *dsfmt;  /*rng*/

  /*state */
  sparrow_state state;
  sparrow_state next_state;

  lag_times_t *lag_table;
  guint64 lag_record;
  guint32 lag;

  gint32 countdown; /*intra-state timing*/

  /*buffer pointers for previous frames */
  guint8 *in_frame;
  guint8 *prev_frame;
  guint8 *work_frame;
  guint8 *debug_frame;

  GstBuffer *in_buffer;
  GstBuffer *prev_buffer;
  /*don't need work_buffer */

  IplImage *in_ipl[3];

  gboolean debug;

  guint32 rng_seed;

  guint32 frame_count;
};

struct _GstSparrowClass
{
  GstVideoFilterClass parent_class;
};

GType gst_sparrow_get_type(void);


GST_DEBUG_CATEGORY_EXTERN (sparrow_debug);
#define GST_CAT_DEFAULT sparrow_debug

/* GstSparrow signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_CALIBRATE,
  PROP_DEBUG,
  PROP_RNG_SEED
};

#define DEFAULT_PROP_CALIBRATE TRUE
#define DEFAULT_PROP_DEBUG FALSE
#define DEFAULT_PROP_RNG_SEED -1





/* GST_DISABLE_GST_DEBUG is set in gstreamer compilation. If it is set, we
   need our own debug channel. */
#ifdef GST_DISABLE_GST_DEBUG

#undef GEST_DEBUG

static FILE *_sparrow_bloody_debug_flags = NULL;
static void
GST_DEBUG(const char *msg, ...){
  if (! _sparrow_bloody_debug_flags){
    _sparrow_bloody_debug_flags = fopen("/tmp/sparrow.log", "wb");
    if (! _sparrow_bloody_debug_flags){
      exit(1);
    }
  }
  va_list argp;
  va_start(argp, msg);
  vfprintf(_sparrow_bloody_debug_flags, msg, argp);
  va_end(argp);
  fflush(_sparrow_bloody_debug_flags);
}

#define GST_ERROR        GST_DEBUG
#define GST_WARNING      GST_DEBUG
#define GST_INFO         GST_DEBUG
#define GST_LOG          GST_DEBUG
#define GST_FIXME        GST_DEBUG

#endif
#define LOG_LINENO() GST_DEBUG("%-25s  line %4d \n", __func__, __LINE__ );


G_END_DECLS
#endif /* __GST_VIDEO_SPARROW_H__ */
