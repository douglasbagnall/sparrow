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
#define CALIBRATE_ON_MAX_T 4
#define CALIBRATE_OFF_MIN_T 2
#define CALIBRATE_OFF_MAX_T 10
#define CALIBRATE_PATTERN_L 100
#define CALIBRATE_SELF_SIZE 20


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

#define CALIBRATION_MIN_HITS 5

#define MAX_CALIBRATION_LAG 16
typedef struct lag_times_s {
  guint32 centre;
  guint32 confidence;
  guint32 hits;
  guint16 lag[MAX_CALIBRATION_LAG];
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


typedef struct _GstSparrow GstSparrow;
typedef struct _GstSparrowClass GstSparrowClass;


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

  /* properties */
  gint calibrate;  /*whether to calibrate */

  /* misc */
  dsfmt_t *dsfmt;  /*rng*/

  /*state */
  gint state;
  gint next_state;

  /*calibration state */
  gint calibrate_x;          /* for inital square */
  gint calibrate_y;
  gint calibrate_size;       /* for inital square */
  gint calibrate_on;         /*for calibration pattern */
  gint calibrate_wait;
  guint32 calibrate_pattern[CALIBRATE_PATTERN_L];
  guint32 calibrate_index;

  lag_times_t *lag_table;
  guint32 lag_record;

  /*buffer pointers for previous frames */
  guint8 *in_frame;
  guint8 *prev_frame;
  guint8 *work_frame;
  guint8 *debug_frame;

  GstBuffer *in_buffer;
  GstBuffer *prev_buffer;
  /*don't need work_buffer */

  IplImage* in_ipl;
  IplImage* prev_ipl;
  IplImage* work_ipl;

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
