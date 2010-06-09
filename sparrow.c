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

/* static functions (via `make cproto`) */
static void change_state(GstSparrow *sparrow, sparrow_state state);


/* set up whatever debugging methods are enabled */
static void
init_debug(GstSparrow *sparrow){
  if (!sparrow->debug_frame){
    sparrow->debug_frame = malloc_aligned_or_die(MAX(sparrow->in.size, sparrow->out.size));
  }
}

/*RNG code */
/*seed with -1 for automatic seed choice */
static void rng_init(GstSparrow *sparrow, guint32 seed){
  GST_DEBUG("in RNG init\n");
  if (seed == (guint32)-1){
    /* XXX should really use /dev/urandom */
    seed = rand() ^ (size_t)&(sparrow);
    GST_DEBUG("Real seed %u\n", seed);
  }
  if (seed == 0)
    seed = 12345;

  dsfmt_init_gen_rand(sparrow->dsfmt, seed);
  dsfmt_gen_rand_all(sparrow->dsfmt);
  GST_DEBUG("RNG seeded with %u\n", seed);
}

/** debugging: write frames out somewhere. **/

/*spit out the frame as a ppm image */
void INVISIBLE
ppm_dump(sparrow_format *rgb, guint8 *data, guint32 width, guint32 height, const char *name)
{
  guint i;
  FILE *fh = fopen(name, "w");
  guint32 size = width * height;
  fprintf(fh, "P6\n%u %u\n255\n", width, height);
  /* 4 cases: xBGR xRGB BGRx RGBx
     need to convert to 24bit R G B
     XX maybe could optimise some cases?
  */
  guint32 *p = (guint32 *)data;
  for (i = 0; i < size; i++){
    putc((*p >> rgb->rshift) & 255, fh);
    putc((*p >> rgb->gshift) & 255, fh);
    putc((*p >> rgb->bshift) & 255, fh);
    p++;
  }
  fflush(fh);
  fclose(fh);
}

/*pgm for greyscale */
void INVISIBLE
pgm_dump(guint8 *data, guint32 width, guint32 height, const char *name)
{
  FILE *fh = fopen(name, "w");
  size_t size = width * height;
  fprintf(fh, "P5\n%u %u\n255\n", width, height);
  size_t wrote = fwrite(data, 1, size, fh);
  if (wrote != size){
    GST_DEBUG("wanted to write %u bytes; fwrite said %u\n", size, wrote);
  }
  fflush(fh);
  fclose(fh);
}


#define PPM_FILENAME_TEMPLATE "/tmp/sparrow_%05d.%s"
#define PPM_FILENAME_LENGTH (sizeof(PPM_FILENAME_TEMPLATE) + 15)

void INVISIBLE
debug_frame(GstSparrow *sparrow, guint8 *data, guint32 width, guint32 height, int pixsize){
  char name[PPM_FILENAME_LENGTH];
  int res;
  if (pixsize == 4){ /*rgb*/
    res = snprintf(name, PPM_FILENAME_LENGTH, PPM_FILENAME_TEMPLATE, sparrow->frame_count, "ppm");
    if (res > 0){
      ppm_dump(&(sparrow->in), data, width, height, name);
    }
  }
  else {/*greyscale*/
    res = snprintf(name, PPM_FILENAME_LENGTH, PPM_FILENAME_TEMPLATE, sparrow->frame_count, "pgm");
    if (res > 0){
      pgm_dump(data, width, height, name);
    }
  }
}

/** interpret gst attributes **/

/* Extract a colour (R,G,B) bitmask from gobject  */
static guint32 get_mask(GstStructure *s, char *mask_name){
  gint32 mask;
  int res = gst_structure_get_int(s, mask_name, &mask);
  if (!res){
    GST_WARNING("No mask for '%s' !\n", mask_name);
  }
  return (guint32)mask;
}

static void
extract_caps(sparrow_format *im, GstCaps *caps)
{
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gst_structure_get_int(s, "width", &(im->width));
  gst_structure_get_int(s, "height", &(im->height));
  im->rshift = mask_to_shift(get_mask(s, "red_mask"));
  im->gshift = mask_to_shift(get_mask(s, "green_mask"));
  im->bshift = mask_to_shift(get_mask(s, "blue_mask"));
  /* recalculate shifts as little-endian */
  im->rmask = 0xff << im->rshift;
  im->gmask = 0xff << im->gshift;
  im->bmask = 0xff << im->bshift;
  im->rbyte = im->rshift / 8;
  im->gbyte = im->gshift / 8;
  im->bbyte = im->bshift / 8;

  im->pixcount = im->width * im->height;
  im->size = im->pixcount * PIXSIZE;
  im->colours[SPARROW_WHITE] = im->rmask | im->gmask | im->bmask;
  im->colours[SPARROW_GREEN] = im->gmask;
  im->colours[SPARROW_MAGENTA] = im->rmask | im->bmask;

  GST_DEBUG("\ncaps:\n%" GST_PTR_FORMAT, caps);
  GST_DEBUG("shifts: r %u g %u b %u\n", im->rshift, im->gshift, im->bshift);
  GST_DEBUG("dimensions: w %u h %u pix %u size %u\n", im->width, im->height,
      im->pixcount, im->size);
}



/*Most functions below here are called from gstsparrow.c and are NOT static */

/* called by gst_sparrow_init(). The source/sink capabilities (and commandline
   arguments[?]) are unknown at this stage, so there isn't much useful to do
   here.*/
void INVISIBLE
sparrow_pre_init(GstSparrow *sparrow){
}

/* called by gst_sparrow_set_caps(). This sets up everything after gstreamer
   has worked out what the pipeline will look like.
 */
gboolean INVISIBLE
sparrow_init(GstSparrow *sparrow, GstCaps *incaps, GstCaps *outcaps){
  change_state(sparrow, SPARROW_INIT);
  extract_caps(&(sparrow->in), incaps);
  extract_caps(&(sparrow->out), outcaps);
  sparrow_format *in = &(sparrow->in);

  sparrow->shared = sparrow_get_shared();
  maybe_load_images(sparrow);
  maybe_load_index(sparrow);

  sparrow->dsfmt = zalloc_aligned_or_die(sizeof(dsfmt_t));
  sparrow->screenmask = malloc_aligned_or_die(in->pixcount);

#if ! USE_FULL_LUT
  size_t point_memsize = (sizeof(sparrow_map_path_t) * sparrow->out.pixcount / LINE_PERIOD) + 1;
  size_t row_memsize = sizeof(sparrow_map_row_t) * sparrow->out.height + 1;
  sparrow->map.point_mem = malloc_aligned_or_die(point_memsize);
  sparrow->map.rows = zalloc_aligned_or_die(row_memsize);
#else
  size_t lutsize = sizeof(sparrow_map_lut_t) * sparrow->out.pixcount;
  sparrow->map_lut = zalloc_aligned_or_die(lutsize);
#endif

  sparrow->timer_start.tv_sec = 0;
  sparrow->timer_stop.tv_sec = 0;

  rng_init(sparrow, sparrow->rng_seed);

  if (sparrow->debug){
    init_debug(sparrow);
  }

  if (sparrow->rng_seed & 2){/*XXX need better test */
    sparrow->colour = SPARROW_GREEN;
  }
  else {
    sparrow->colour = SPARROW_MAGENTA;
  }

  sparrow->timer_log = (sparrow->use_timer) ? fopen(TIMER_LOG_FILE, "w") : NULL;

  if(sparrow->reload){
    change_state(sparrow, SPARROW_FIND_EDGES);
  }
  else {
    change_state(sparrow, SPARROW_NEXT_STATE);
  }
  return TRUE;
}

void INVISIBLE
sparrow_finalise(GstSparrow *sparrow)
{
  free(sparrow->dsfmt);
  free(sparrow->screenmask);
#if ! USE_FULL_LUT
  free(sparrow->map.point_mem);
  free(sparrow->map.rows);
#else
  free(sparrow->map_lut);
#endif


  if (sparrow->timer_log){
    fclose(sparrow->timer_log);
  }
  //free everything
  //cvReleaseImageHeader(IplImage** image)
}


/* initialisation functions and sparrow_transform() use this to set up a new
   state. */
static void
change_state(GstSparrow *sparrow, sparrow_state state)
{
  GST_DEBUG("state is %d, sparrow->state is %d\n", state, sparrow->state);
  switch(sparrow->state){
  case SPARROW_FIND_SELF:
    finalise_find_self(sparrow);
    break;
  case SPARROW_FIND_SCREEN:
    finalise_find_screen(sparrow);
    break;
  case SPARROW_FIND_EDGES:
    finalise_find_edges(sparrow);
    break;
  case SPARROW_PLAY:
    finalise_play(sparrow);
    break;
  case SPARROW_INIT:
    break;
  default:
    GST_DEBUG("change_state got unknown state: %d\n", state);
  }
  if (state == SPARROW_NEXT_STATE){
    state = sparrow->state + 1;
  }
  switch(state){
  case SPARROW_FIND_SELF:
    init_find_self(sparrow);
    break;
  case SPARROW_FIND_SCREEN:
    init_find_screen(sparrow);
    break;
  case SPARROW_FIND_EDGES:
    init_find_edges(sparrow);
    break;
  case SPARROW_PLAY:
    init_play(sparrow);
    break;
  case SPARROW_INIT:
    //init_init(sparrow);
    break;
  default:
    GST_DEBUG("change_state got unknown state: %d\n", state);
  }
  sparrow->state = state;
}


/*called by gst_sparrow_transform_ip every frame.

  decide what to do based on sparrow->state. All the processing is done in a
  "mode_*" function, which returns a state or SPARROW_STATUS_QUO.  If a state
  is returned, then change_state() is called to initialise the state, even if
  it is the current state (so states can use this to reset).
*/
void INVISIBLE
sparrow_transform(GstSparrow *sparrow, guint8 *in, guint8 *out)
{
  sparrow_state new_state;
#if TIME_TRANSFORM
  TIMER_START(sparrow);
#endif
  //GST_DEBUG("in %p, out %p\n", in, out);
  switch(sparrow->state){
  case SPARROW_FIND_SELF:
    new_state = mode_find_self(sparrow, in, out);
    break;
  case SPARROW_FIND_SCREEN:
    new_state = mode_find_screen(sparrow, in, out);
    break;
  case SPARROW_FIND_EDGES:
    new_state = mode_find_edges(sparrow, in, out);
    break;
  case SPARROW_PLAY:
    new_state = mode_play(sparrow, in, out);
    break;
  default:
    GST_DEBUG("unknown state:%d\n", sparrow->state);
    new_state = SPARROW_STATUS_QUO;
  }
  sparrow->frame_count++;
  if (new_state != SPARROW_STATUS_QUO){
    change_state(sparrow, new_state);
  }
#if TIME_TRANSFORM
  TIMER_STOP(sparrow);
#endif
}

