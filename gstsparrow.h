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
#define DSFMT_MEXP 19937
#include "dSFMT/dSFMT.h"


#define UNUSED __attribute__ ((unused))


#ifdef GST_DISABLE_GST_DEBUG

#undef GEST_DEBUG

static FILE *_sparrow_bloody_debug_flags = NULL;
static void
LOG(char *msg, ...){
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

#define GST_DEBUG LOG
#endif

    //#define LOG(format, ...) fprintf (stderr, (format),## __VA_ARGS__); fflush(stderr);
#define LOG_LINENO() GST_DEBUG("%-25s  line %4d \n", __func__, __LINE__ );

#define PIXSIZE 4

#define CALIBRATE_MIN_T 2
#define CALIBRATE_MAX_T 12
#define CALIBRATE_EDGE_PATTERN_L 2
#define CALIBRATE_SELF_PATTERN_L 10
#define CALIBRATE_SELF_SIZE 10

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
  gint size;

  guint32 calibrate_offset;

  /* properties */
  gint calibrate;
  /* tables */

  /* stuff */
  dsfmt_t *dsfmt;

  gint calibrate_x;
  gint calibrate_y;
  gint calibrate_size;
  gint calibrate_on;
  gint calibrate_wait;
  gint calibrate_pattern[CALIBRATE_SELF_PATTERN_L];
  gint calibrate_index;
  gint state;
  gint next_state;
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
  SPARROW_PLAY,
} sparrow_states;



static void * malloc_or_die(size_t size){
    void *p = malloc(size);
    if (!p){
	printf("malloc would not allocate %u bytes! seriously!\n", size);
	exit(1);
    }
    return p;
}








G_END_DECLS
#endif /* __GST_VIDEO_SPARROW_H__ */
