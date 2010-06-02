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
#ifndef __SPARROW_SPARROW_H__
#define __SPARROW_SPARROW_H__

#include <stdlib.h>
#include "gstsparrow.h"
#include "sparrow_false_colour_lut.h"
#include "sparrow_gamma_lut.h"

/* calibrate.c */
INVISIBLE void init_find_self(GstSparrow *sparrow);
INVISIBLE sparrow_state mode_find_self(GstSparrow *sparrow, guint8 *in, guint8 *out);
INVISIBLE void finalise_find_self(GstSparrow *sparrow);

/* edges.c */
INVISIBLE void init_find_edges(GstSparrow *sparrow);
INVISIBLE sparrow_state mode_find_edges(GstSparrow *sparrow, guint8 *in, guint8 *out);
INVISIBLE void finalise_find_edges(GstSparrow *sparrow);

/* floodfill.c */
INVISIBLE void init_find_screen(GstSparrow *sparrow);
INVISIBLE sparrow_state mode_find_screen(GstSparrow *sparrow, guint8 *in, guint8 *out);
INVISIBLE void finalise_find_screen(GstSparrow *sparrow);

/* play.c */
INVISIBLE void init_play(GstSparrow *sparrow);
INVISIBLE sparrow_state mode_play(GstSparrow *sparrow, guint8 *in, guint8 *out);
INVISIBLE void finalise_play(GstSparrow *sparrow);

/* sparrow.c */
INVISIBLE void debug_frame(GstSparrow *sparrow, guint8 *data, guint32 width, guint32 height, int pixsize);
INVISIBLE void sparrow_pre_init(GstSparrow *sparrow);
INVISIBLE gboolean sparrow_init(GstSparrow *sparrow, GstCaps *incaps, GstCaps *outcaps);
INVISIBLE void sparrow_transform(GstSparrow *sparrow, guint8 *in, guint8 *out);
INVISIBLE void sparrow_finalise(GstSparrow *sparrow);


#define SPARROW_CALIBRATE_ON  1

#define MAYBE_DEBUG_IPL(ipl)((sparrow->debug) ?                         \
      debug_frame(sparrow, (guint8*)(ipl)->imageData, (ipl)->width,     \
          (ipl)->height, (ipl)->nChannels):(void)0)


#define CALIBRATE_WAIT_SIGNAL_THRESHOLD 32



/*memory allocation */
#define ALIGNMENT 16

static inline __attribute__((malloc)) UNUSED void *
malloc_or_die(size_t size){
  void *p = malloc(size);
  if (!p){
    GST_ERROR("malloc would not allocate %u bytes! seriously!\n", size);
    exit(EXIT_FAILURE);
  }
  return p;
}

static inline __attribute__((malloc)) UNUSED void *
malloc_aligned_or_die(size_t size){
  void *mem;
  int err = posix_memalign(&mem, ALIGNMENT, size);
  if (err){
    GST_ERROR("posix_memalign returned %d trying to allocate %u bytes aligned on %u byte boundaries\n",
        err, size, ALIGNMENT);
    exit(EXIT_FAILURE);
  }
  return mem;
}

static inline __attribute__((malloc)) UNUSED void *
zalloc_aligned_or_die(size_t size){
  void *mem = malloc_aligned_or_die(size);
  memset(mem, 0, size);
  return mem;
}

static inline __attribute__((malloc)) UNUSED void *
zalloc_or_die(size_t size){
  void *mem = calloc(size, 1);
  if (!mem){
    GST_ERROR("calloc would not allocate %u bytes!\n", size);
    exit(EXIT_FAILURE);
  }
  return mem;
}

/*RNG macros */

static inline UNUSED guint32
rng_uniform_int(GstSparrow *sparrow, guint32 limit){
  double d = dsfmt_genrand_close_open(sparrow->dsfmt);
  double d2 = d * limit;
  guint32 i = (guint32)d2;
  return i;
}

static inline UNUSED double
rng_uniform_double(GstSparrow *sparrow, double limit){
    return dsfmt_genrand_close_open(sparrow->dsfmt) * limit;
}

#define rng_uniform(sparrow) dsfmt_genrand_close_open((sparrow)->dsfmt)

#define RANDINT(sparrow, start, end)((start) + rng_uniform_int(sparrow, (end) - (start)))


#define DISASTEROUS_CRASH(msg) GST_ERROR("DISASTER: %s\n%-25s  line %4d \n", (msg), __func__, __LINE__);

static inline guint32
popcount32(guint32 x)
{
  x = x - ((x >> 1) & 0x55555555);
  x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
  x = (x + (x >> 4)) & 0x0F0F0F0F;
  x = x + (x >> 8);
  x = x + (x >> 16);
  return x & 0x000000FF;
}


/*XXX optimised for 32 bit!*/
static inline guint32
popcount64(guint64 x64)
{
  guint32 x = x64 & (guint32)-1;
  guint32 y = x64 >> 32;
  x = x - ((x >> 1) & 0x55555555);
  y = y - ((y >> 1) & 0x55555555);
  x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
  y = (y & 0x33333333) + ((y >> 2) & 0x33333333);
  x = (x + (x >> 4)) & 0x0F0F0F0F;
  y = (y + (y >> 4)) & 0x0F0F0F0F;
  x = x + (x >> 8);
  y = y + (y >> 8);
  x = x + (x >> 16);
  y = y + (y >> 16);
  return (x + y) & 0x000000FF;
}

static inline guint32
hamming_distance64(guint64 a, guint64 b, guint64 mask){
  a &= mask;
  b &= mask;
  /* count where the two differ */
  return popcount64(a ^ b);
}

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

static inline IplImage *
init_ipl_image(sparrow_format *dim, int channels){
  CvSize size = {dim->width, dim->height};
  IplImage* im = cvCreateImageHeader(size, IPL_DEPTH_8U, channels);
  return cvInitImageHeader(im, size, IPL_DEPTH_8U, channels, 0, 8);
}


#endif /* __SPARROW_SPARROW_H__ */
