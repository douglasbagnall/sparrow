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
#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>

static const char *JPEG_BLOB_NAME = "/home/douglas/sparrow/content/jpeg.blob";
static const char *JPEG_INDEX_NAME = "/home/douglas/sparrow/content/jpeg.index";

INVISIBLE sparrow_shared_t *
sparrow_get_shared(void){
  static sparrow_shared_t shared;
  return &shared;
}

static gpointer
load_images(gpointer p){
  GST_DEBUG("load_images with pointer %p", p);
  GstSparrow *sparrow = (GstSparrow *)p;
  int fd = open(JPEG_BLOB_NAME, O_RDONLY);
  off_t length = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);
  GST_DEBUG("about to mmap");
  void *mem = mmap(NULL, length, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
  GST_DEBUG("mmap returned %p", mem);
  madvise(mem, length, POSIX_MADV_WILLNEED);
  GST_DEBUG("done madvise. sparrow->shared is %p", sparrow->shared);
  sparrow->shared->jpeg_blob = mem;
  sparrow->shared->blob_size = length;

  return mem;
}

static gpointer
unload_images(gpointer p){
  GstSparrow *sparrow = (GstSparrow *)p;
  sparrow_shared_t *shared = sparrow->shared;
  munmap(shared->jpeg_blob, shared->blob_size);
  return NULL;
}

static gpointer
load_index(gpointer p){
  GstSparrow *sparrow = (GstSparrow *)p;
  int fd = open(JPEG_INDEX_NAME, O_RDONLY);
  off_t length = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);
  void *mem = mmap(NULL, length, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
  GST_DEBUG("mmap returned %p", mem);
  madvise(mem, length, POSIX_MADV_WILLNEED);
  sparrow->shared->index = mem;
  sparrow->shared->image_count = length / sizeof(sparrow_frame_t);
  GST_DEBUG("found %d frame info structures of size %d\n", sparrow->shared->image_count,
      sizeof(sparrow_frame_t));
  return mem;
}

static gpointer
unload_index(gpointer p){
  GstSparrow *sparrow = (GstSparrow *)p;
  munmap(sparrow->shared->index,
      sparrow->shared->image_count * sizeof(sparrow_frame_t));
  return NULL;
}


INVISIBLE void
maybe_load_images(GstSparrow *sparrow)
{
  GST_DEBUG("maybe load images");
  static GOnce image_once = G_ONCE_INIT;
  g_once(&image_once, load_images, sparrow);
}

INVISIBLE void
maybe_unload_images(GstSparrow *sparrow){
  GST_DEBUG("maybe unload images");
  static GOnce once = G_ONCE_INIT;
  g_once(&once, unload_images, sparrow);
}

INVISIBLE void
maybe_load_index(GstSparrow *sparrow)
{
  GST_DEBUG("maybe load index");
  static GOnce index_once = G_ONCE_INIT;
  g_once(&index_once, load_index, sparrow);
}

INVISIBLE void
maybe_unload_index(GstSparrow *sparrow){
  static GOnce once = G_ONCE_INIT;
  g_once(&once, unload_index, sparrow);
}

