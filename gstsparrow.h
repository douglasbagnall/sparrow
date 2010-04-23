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
#endif

typedef guint32 pix_t;
#define PIXSIZE (sizeof(pix_t))


#define CALIBRATE_MIN_T 2
#define CALIBRATE_MAX_T 10
#define CALIBRATE_PATTERN_L 100
#define CALIBRATE_SELF_SIZE 10

#define IPL_IMAGE_COUNT 5

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

#define MAX_CALIBRATION_LAG 20
typedef struct lag_times_s {
  guint32 hits;
  guint16 lag[MAX_CALIBRATION_LAG];
} lag_times_t;




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
  /* format */
  gint width;
  gint height;
  gint size;  /* bytes, not pixels */

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
  gint calibrate_pattern[CALIBRATE_PATTERN_L];
  gint calibrate_index;

  lag_times_t * lag_table;
  guint32 lag_record;

  /*buffer pointers for previous frames */
  guint8 *prev_frame;
  guint8 *work_frame;
  guint8 *debug_frame;

  gboolean debug;

  guint32 rng_seed;

  /*array of IPL image headers that get allocated as necessary */
  IplImage ipl_images[IPL_IMAGE_COUNT] __attribute__((aligned));

#if SPARROW_PPM_DEBUG
  guint32 debug_count;
#endif

  gint rshift;
  gint gshift;
  gint bshift;
  pix_t rmask;
  pix_t gmask;
  pix_t bmask;
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
GST_DEBUG(char *msg, ...){
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
