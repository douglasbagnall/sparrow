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

#include "sparrowconfig.h"
#include "dSFMT/dSFMT.h"
#include "cv.h"

#ifndef UNUSED
#define UNUSED __attribute__ ((unused))
#endif

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


    //#define LOG(format, ...) fprintf (stderr, (format),## __VA_ARGS__); fflush(stderr);
#define LOG_LINENO() GST_DEBUG("%-25s  line %4d \n", __func__, __LINE__ );

#define PIXSIZE 4
typedef guint32 pix_t;

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
typedef guint16 lag_times[MAX_CALIBRATION_LAG];

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

  lag_times * lag_table;
  guint32 lag_record;

  /*buffer pointers for previous frames */
  guint8 *prev_frame;
  guint8 *work_frame;
  size_t prev_frame_size;

  gboolean debug;
  
  guint32 rng_seed;

  /*array of IPL image headers that get allocated as necessary */
  IplImage ipl_images[IPL_IMAGE_COUNT] __attribute__((aligned));

};

struct _GstSparrowClass
{
  GstVideoFilterClass parent_class;
};

GType gst_sparrow_get_type(void);

#define SPARROW_CALIBRATE_ON  1

typedef enum {
  SPARROW_INIT,
  SPARROW_FIND_SELF,
  SPARROW_FIND_EDGES,
  SPARROW_FIND_GRID,
  SPARROW_PLAY,
} sparrow_states;

#define CALIBRATE_SIGNAL_THRESHOLD 16


#define DISASTEROUS_CRASH(msg) GST_ERROR("DISASTER: %s\n%-25s  line %4d \n", (msg), __func__, __LINE__);



static UNUSED void * malloc_or_die(size_t size){
  void *p = malloc(size);
  if (!p){
    GST_ERROR("malloc would not allocate %u bytes! seriously!\n", size);
    exit(1);
  }
  return p;
}

#define ALIGNMENT 16
static UNUSED void * malloc_aligned_or_die(size_t size){
  void *mem;
  int err = posix_memalign(&mem, ALIGNMENT, size);
  if (err){
    GST_ERROR("posix_memalign returned %d trying to allocate %u bytes aligned on %u byte boundaries\n",
        err, size, ALIGNMENT);
    exit(1);
  }
  return mem;
}

static UNUSED void memalign_or_die(void **memptr, size_t alignment, size_t size){
  int err = posix_memalign(memptr, alignment, size);
  if (err){
    GST_ERROR("posix_memalign returned %d trying to allocate %u bytes alligned on %u byte boundaries\n",
        err, size, alignment);
    exit(1);
  }
}








G_END_DECLS
#endif /* __GST_VIDEO_SPARROW_H__ */
