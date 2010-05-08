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


void INVISIBLE sparrow_rotate_history(GstSparrow *sparrow, GstBuffer *inbuf);
void INVISIBLE  sparrow_pre_init(GstSparrow *sparrow);
gboolean INVISIBLE sparrow_init(GstSparrow *sparrow, GstCaps *incaps, GstCaps *outcaps);
void INVISIBLE sparrow_transform(GstSparrow *sparrow, guint8 *in, guint8 *out);
void INVISIBLE sparrow_finalise(GstSparrow *sparrow);


#define SPARROW_CALIBRATE_ON  1


#define CALIBRATE_WAIT_SIGNAL_THRESHOLD 32
#define ALIGNMENT 16


/*memory allocation */

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

#endif /* __SPARROW_SPARROW_H__ */
