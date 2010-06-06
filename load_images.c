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

#include <sys/mman.h>


static void
load_images(GstSparrow *sparrow){
  int fd = open(sparrow->jpeg_blob, O_RDONLY);
  off_t length = lseek(fd, 0, SEEK_END);
  off_t start = lseek(fd, 0, SEEK_SET);

  void *mem = mmap(NULL, length, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
  madvise(mem, length, POSIX_MADV_WILLNEED);

  sparrow->shared->jpeg_blob = mem;
  sparrow->shared->blob_size = length;
}

static void
unload_images(GstSparrow *sparrow){
  int munmap(sparrow->shared->jpeg_blob, sparrow->shared->blob_size);
}

GStaticRWLock blob_lock = G_STATIC_RW_LOCK_INIT;
GStaticRWLock index_lock = G_STATIC_RW_LOCK_INIT;

/* not using the reader locks (?)
  g_static_rw_lock_reader_lock ();
  g_static_rw_lock_reader_unlock ();
*/

void
maybe_load_images(GstSparrow *sparrow)
{
  if (g_static_rw_lock_writer_trylock(&blob_lock)){
    if (sparrow->shared->jpeg_blob == NULL){
      load_images(sparrow);
    }
    else {
      GST_WARNING("trying to load images that are already loaded !!\n");
    }
    g_static_rw_lock_writer_unlock(&blob_lock);
  }
}

INVISIBLE void
maybe_unload_images(GstSparrow *sparrow){
  if (g_static_rw_lock_writer_trylock(&blob_lock)){
    if (sparrow->shared->jpeg_blob){
      unload_images(sparrow);
      sparrow->shared->jpeg_blob = NULL;
    }
    g_static_rw_lock_writer_unlock(&blob_lock);
  }
}


