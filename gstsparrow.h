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
#include <sys/time.h>

G_BEGIN_DECLS
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

/*XXX could be run time option */
#define USE_FULL_LUT 1

#define SPARROW_PPM_DEBUG 1

#define TIMER_LOG_FILE "/tmp/timer.log"

#include "sparrowconfig.h"
#include "dSFMT/dSFMT.h"
#include "cv.h"

#ifndef UNUSED
#define UNUSED __attribute__ ((unused))
#else
#warning UNUSED is set
#endif

/* the common recommendation for function visibility is to default to 'hidden'
   and specifically mark the unhidden ('default') ones, but this might muck
   with gstreamer macros, some of which declare functions, and most sparrow
   functions are static anyway, so it is simpler to whitelist visibility.

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

#define SPARROW_N_IPL_IN 3

//#define FAKE_OTHER_PROJECTION 1


#define LINE_PERIOD 32
#define H_LINE_OFFSET (LINE_PERIOD / 2)
#define V_LINE_OFFSET (LINE_PERIOD / 2)


typedef enum {
  SPARROW_STATUS_QUO = 0,
  SPARROW_INIT,
  SPARROW_FIND_SELF,
  SPARROW_FIND_SCREEN,
  SPARROW_FIND_EDGES,
  SPARROW_PLAY,


  SPARROW_NEXT_STATE /*magical last state: alias for next in sequence */
} sparrow_state;

typedef enum {
  SPARROW_WHITE = 0,
  SPARROW_GREEN,
  SPARROW_MAGENTA,

  SPARROW_LAST_COLOUR
} sparrow_colour;


typedef struct sparrow_format_s {
  gint32 width;
  gint32 height;
  guint32 pixcount;
  guint32 size;

  guint32 rshift;
  guint32 gshift;
  guint32 bshift;
  guint32 rbyte;
  guint32 gbyte;
  guint32 bbyte;
  guint32 rmask;
  guint32 gmask;
  guint32 bmask;
  guint32 colours[3];
} sparrow_format;


typedef enum sparrow_axis_s {
  SPARROW_HORIZONTAL,
  SPARROW_VERTICAL,
} sparrow_axis_t;


typedef struct sparrow_map_path_s {
  int dx;
  int dy;
  int n;
}sparrow_map_path_t;


typedef struct sparrow_frame_s {
  gint32 offset;
  guint32 jpeg_size;
  guint8 summary[48];
  gint32 successors[8];
} sparrow_frame_t;

typedef struct sparrow_shared_s {
  guint8 *jpeg_blob;
  guint32 blob_size;
  sparrow_frame_t *index;
  guint32 image_count;
} sparrow_shared_t;


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
  sparrow_shared_t *shared; /* images, shared between the vaious instances */
  sparrow_format in;
  sparrow_format out;

  /*some calibration modes have big unwieldy structs that attach here */
  void *helper_struct;

  /* properties / command line options */
  gboolean debug;
  gboolean use_timer;
  guint32 rng_seed;

  /* misc */
  dsfmt_t *dsfmt;  /*rng*/

  /*state */
  sparrow_state state;
  gint32 countdown; /*intra-state timing*/

  guint8 *debug_frame;    /* for constructing debug images */
  GstBuffer *in_buffer;

  guint32 colour;
  guint32 frame_count;

  const char *reload;
  const char *save;
  gboolean serial;

  /*debug timer */
  struct timeval timer_start;
  struct timeval timer_stop;
  FILE * timer_log;

  /*calibration results */
  guint32 lag;
  guint8 *screenmask;
  /*full sized LUT */
  guint32 *map_lut;
  /*for jpeg decompression*/
  struct jpeg_decompress_struct *cinfo;
  int jpeg_colourspace;
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
  PROP_DEBUG,
  PROP_TIMER,
  PROP_RNG_SEED,
  PROP_COLOUR,
  PROP_RELOAD,
  PROP_SAVE,
  PROP_SERIAL
};

#define DEFAULT_PROP_CALIBRATE TRUE
#define DEFAULT_PROP_DEBUG FALSE
#define DEFAULT_PROP_TIMER FALSE
#define DEFAULT_PROP_RNG_SEED -1
#define DEFAULT_PROP_COLOUR SPARROW_GREEN
#define DEFAULT_PROP_RELOAD ""
#define DEFAULT_PROP_SAVE ""
#define DEFAULT_PROP_SERIAL FALSE

#define QUOTE_(x) #x
#define QUOTE(x) QUOTE_(x)

/*timing utility code */
#define TIME_TRANSFORM 1

#define TIMER_START(sparrow) do{                        \
  if (sparrow->timer_log){                              \
    if ((sparrow)->timer_start.tv_sec){                 \
      GST_DEBUG("timer already running!\n");            \
    }                                                   \
    else {                                              \
      gettimeofday(&((sparrow)->timer_start), NULL);    \
    }                                                   \
  }                                                     \
  } while (0)

static inline void
TIMER_STOP(GstSparrow *sparrow)
{
  if (sparrow->timer_log){
    struct timeval *start = &(sparrow->timer_start);
    struct timeval *stop = &(sparrow->timer_stop);
    if (start->tv_sec == 0){
      GST_DEBUG("the timer isn't running!\n");
      return;
    }
    gettimeofday(stop, NULL);
    guint32 t = ((stop->tv_sec - start->tv_sec) * 1000000 +
        stop->tv_usec - start->tv_usec);

#if SPARROW_NOISY_DEBUG
    GST_DEBUG("took %u microseconds (%0.5f of a frame)\n",
        t, (double)t * (25.0 / 1000000.0));
#endif

    fprintf(sparrow->timer_log, "%d %6d\n", sparrow->state, t);
    fflush(sparrow->timer_log);
    start->tv_sec = 0; /* mark it as unused */
  }
}

/* GST_DISABLE_GST_DEBUG is set in gstreamer compilation. If it is set, we
   need our own debug channel. */
#ifdef GST_DISABLE_GST_DEBUG

#undef GST_DEBUG

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
