/************************************************************************************
Partially based on TerraLib, from https://svn.dpi.inpe.br/terralib

TerraLib - a library for developing GIS applications.
Copyright © 2001-2007 INPE and Tecgraf/PUC-Rio.

This code is part of the TerraLib library.
This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

You should have received a copy of the GNU Lesser General Public
License along with this library.

The authors reassure the license terms regarding the warranties.
They specifically disclaim any warranties, including, but not limited to,
the implied warranties of merchantability and fitness for a particular purpose.
The library provided hereunder is on an "as is" basis, and the authors have no
obligation to provide maintenance, support, updates, enhancements, or modifications.
In no event shall INPE and Tecgraf / PUC-Rio be held liable to any party for direct,
indirect, special, incidental, or consequential damages arising out of the use
of this library and its documentation.
*************************************************************************************/

/* SYNOPSIS

GstSparrow *sparrow;
guint8 * src;        // pointer to jpeg in mem
int size;            // length of jpeg in mem
guint8 * dest;       // pointer to output image row

init_jpeg_src(sparrow);   //once only

FOR EACH jpeg {
  begin_reading_jpeg(sparrow, src, size);
  FOR EACH line {
    read_one_line(sparrow, dest);
  }
  finish_reading_jpeg(sparrow);
}

finalise_jpeg_src(sparrow); // once only (optional really)

*/

#include <stdio.h>
#include <stdlib.h>
#include "jpeglib.h"
#include "gstsparrow.h"
#include "sparrow.h"

// Expanded data source object for memory buffer input
typedef struct
{
  struct jpeg_source_mgr pub;
  unsigned char* buffer;
  unsigned int   bufsize;
} sparrow_src_mgr;

#define COLOURSPACE JCS_EXT_XBGR


/*
 Initialize source --- Nothing to do
 */
static void
init_source (j_decompress_ptr cinfo)
{}

/*
 Fill the input buffer --- called whenever buffer is emptied.
 */
static gboolean
fill_input_buffer (j_decompress_ptr cinfo)
{
  sparrow_src_mgr *src = (sparrow_src_mgr *) cinfo->src;
  src->pub.next_input_byte = src->buffer;
  src->pub.bytes_in_buffer = src->bufsize;

  return TRUE;
}

/*
 Skip data --- used to skip over a potentially large amount of
 uninteresting data.
 */
static void
skip_input_data (j_decompress_ptr cinfo, long num_bytes)
{
  sparrow_src_mgr * src = (sparrow_src_mgr *) cinfo->src;

  /* just move the ptr */
  src->pub.next_input_byte += num_bytes;
  src->pub.bytes_in_buffer -= num_bytes;
}

/*
  Terminate source --- called by jpeg_finish_decompress
 */
static void
term_source (j_decompress_ptr cinfo)
{}

/*
 Prepare for input from a memory buffer.
 */
static void
jpeg_mem_src (j_decompress_ptr cinfo, unsigned char* buffer, unsigned int bufsize)
{
  sparrow_src_mgr *src;

  if (cinfo->src == NULL) {
    cinfo->src = (struct jpeg_source_mgr *)
      (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT, sizeof(sparrow_src_mgr));
  }

  src = (sparrow_src_mgr *) cinfo->src;
  src->pub.init_source = init_source;
  src->pub.fill_input_buffer = fill_input_buffer;
  src->pub.skip_input_data = skip_input_data;
  src->pub.resync_to_restart = jpeg_resync_to_restart;
  src->pub.term_source = term_source;
  src->pub.bytes_in_buffer = 0;
  src->pub.next_input_byte = NULL;

  src->buffer = buffer;
  src->bufsize = bufsize;
}

#define ROWS_PER_CYCLE 1 /*I think this needs to be 1 in anycase*/

/* the complete decompress function, not actually used in sparrow */
INVISIBLE void
decompress_buffer(struct jpeg_decompress_struct *cinfo, guint8* src, int size, guint8* dest,
    int *width, int *height)
{
  jpeg_create_decompress(cinfo);
  jpeg_mem_src(cinfo, src, size);

  jpeg_read_header(cinfo, TRUE);
  jpeg_start_decompress(cinfo);
  cinfo->out_color_space = COLOURSPACE;

  *width = cinfo->output_width;
  *height = cinfo->output_height;

  /*use 4 here, not cinfo->num_channels, because libjpeg_turbo is doing the
    RGBx colour space conversion */
  int stride = 4 * cinfo->output_width;
  guint8* row = dest;
  while (cinfo->output_scanline < cinfo->output_height){
    int read = jpeg_read_scanlines(cinfo, &row, ROWS_PER_CYCLE);
    row += stride * read;
  }
  jpeg_finish_decompress(cinfo);
}




INVISIBLE void
begin_reading_jpeg(GstSparrow *sparrow, guint8* src, int size){
  struct jpeg_decompress_struct *cinfo = sparrow->cinfo;
  GST_DEBUG("cinfo is %p, src %p, size %d\n", cinfo, src, size);

  jpeg_create_decompress(cinfo);
  jpeg_mem_src(cinfo, src, size);

  jpeg_read_header(cinfo, TRUE);
  jpeg_start_decompress(cinfo);
  cinfo->out_color_space = COLOURSPACE;
  if (cinfo->output_width != (guint)sparrow->out.width ||
      cinfo->output_height != (guint)sparrow->out.width){
    GST_ERROR("jpeg sizes are wrong! %dx%d, should be %dx%d.\n"
        "Not doing anything: this is probably goodbye.\n",
        cinfo->output_width, cinfo->output_height,
        sparrow->out.width, sparrow->out.width);
  }
}


INVISIBLE void
read_one_line(GstSparrow *sparrow, guint8* dest){
  struct jpeg_decompress_struct *cinfo = sparrow->cinfo;
  if (cinfo->output_scanline < cinfo->output_height){
    int read = jpeg_read_scanlines(cinfo, &dest, 1);
  }
  else {
    GST_WARNING("wanted to read line %d of jpeg that thinks it is %d lines high!",
        cinfo->output_scanline, cinfo->output_height);
  }
}

INVISIBLE void
finish_reading_jpeg(GstSparrow *sparrow){
  jpeg_finish_decompress(sparrow->cinfo);
}


INVISIBLE void
init_jpeg_src(GstSparrow *sparrow){
  sparrow->cinfo = zalloc_or_die(sizeof(struct jpeg_decompress_struct));
  struct jpeg_error_mgr *jerr = zalloc_or_die(sizeof(struct jpeg_error_mgr));
  sparrow->cinfo->err = jpeg_std_error(jerr);
  sparrow->cinfo->out_color_space = COLOURSPACE;
}

INVISIBLE void
finalise_jpeg_src(GstSparrow *sparrow){
  jpeg_destroy_decompress(sparrow->cinfo);
  free(sparrow->cinfo->err);
  free(sparrow->cinfo);
}
