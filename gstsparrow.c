/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
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


/**
 * SECTION:element-sparrow
 *
 * Performs sparrow correction on a video stream.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch videotestsrc ! ffmpegcolorspace ! sparrow ! ximagesink
 * ]|
 * </refsect2>
 */

#include "gstsparrow.h"
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

#include "sparrow_gamma_lut.h"

/* static_functions */
static void gst_sparrow_base_init(gpointer g_class);
static void gst_sparrow_class_init(GstSparrowClass *g_class);
static void gst_sparrow_init(GstSparrow *sparrow, GstSparrowClass *g_class);
static void gst_sparrow_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_sparrow_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static gboolean gst_sparrow_set_caps(GstBaseTransform *base, GstCaps *incaps, GstCaps *outcaps);
static void rng_init(GstSparrow *sparrow, guint32 seed);
static void simple_negation(guint8 *bytes, guint size);
static void gamma_negation(guint8 *bytes, guint size);
static void calibrate_new_pattern(GstSparrow *sparrow);
static void calibrate_new_state(GstSparrow *sparrow);
static int cycle_pattern(GstSparrow *sparrow, int repeat);
static void sparrow_reset(GstSparrow *sparrow, guint8 *bytes);
static GstFlowReturn gst_sparrow_transform_ip(GstBaseTransform *base, GstBuffer *outbuf);
static gboolean plugin_init(GstPlugin *plugin);


/*
#ifdef HAVE_LIBOIL
#include <liboil/liboil.h>
#include <liboil/liboilcpu.h>
#include <liboil/liboilfunction.h>
#endif
*/

#include <string.h>
#include <math.h>


GST_DEBUG_CATEGORY_STATIC (sparrow_debug);
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
  PROP_DEBUG
};

#define DEFAULT_PROP_CALIBRATE FALSE

/* the capabilities of the inputs and outputs.
 *
 * Use RGB, not YUV, because inverting video is trivial in RGB, not so in YUV
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      GST_VIDEO_CAPS_xBGR "; " GST_VIDEO_CAPS_xRGB "; "
      GST_VIDEO_CAPS_BGRx "; " GST_VIDEO_CAPS_RGBx)
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      GST_VIDEO_CAPS_xBGR "; " GST_VIDEO_CAPS_xRGB "; "
      GST_VIDEO_CAPS_BGRx "; " GST_VIDEO_CAPS_RGBx)
    );


static void gst_sparrow_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_sparrow_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_sparrow_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps);
static GstFlowReturn gst_sparrow_transform_ip (GstBaseTransform * transform,
    GstBuffer * buf);


GST_BOILERPLATE (GstSparrow, gst_sparrow, GstVideoFilter, GST_TYPE_VIDEO_FILTER);

/* plugin_init    - registers plugin (once)
   XXX_base_init  - for the gobject class (once)
   XXX_class_init - for global state (once)
   XXX_init       - for each plugin instance
*/

static void
gst_sparrow_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Video sparrow correction",
      "Filter/Effect/Video",
      "Adds sparrows to a video stream",
      "Douglas Bagnall <douglas@halo.gen.nz>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  GST_INFO("gst base init\n");


/* Clean up */
static void
gst_sparrow_finalize (GObject * obj){
  //GstSparrow *sparrow = GST_SPARROW(obj);
  //free everything
  GST_DEBUG("in gst_sparrow_finalize!\n");
}

static void
gst_sparrow_class_init (GstSparrowClass * g_class)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  trans_class = GST_BASE_TRANSFORM_CLASS (g_class);

  gobject_class->set_property = gst_sparrow_set_property;
  gobject_class->get_property = gst_sparrow_get_property;
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_sparrow_finalize);

  g_object_class_install_property (gobject_class, PROP_CALIBRATE,
      g_param_spec_boolean ("calibrate", "Calibrate", "calibrate against projection",
          DEFAULT_PROP_CALIBRATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEBUG,
      g_param_spec_boolean ("debug", "Debug", "save debug images",
          DEFAULT_PROP_CALIBRATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_sparrow_set_caps);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_sparrow_transform_ip);
  GST_INFO("gst class init\n");
}

static void
gst_sparrow_init (GstSparrow * sparrow, GstSparrowClass * g_class)
{
  GST_INFO("gst sparrow init\n");
  void *mem;
  memalign_or_die(&mem, 16, sizeof(dsfmt_t));
  sparrow->dsfmt = mem;
  rng_init(sparrow, -1);


  sparrow->lag_table = NULL;
  sparrow->prev_frame = NULL;
  sparrow->work_frame = NULL;

  sparrow->state = SPARROW_INIT;
  sparrow->next_state = SPARROW_FIND_SELF; // can be overridden
  calibrate_new_pattern(sparrow);

  /*disallow resizing */
  gst_pad_use_fixed_caps(GST_BASE_TRANSFORM_SRC_PAD(sparrow));
  gst_pad_use_fixed_caps(GST_BASE_TRANSFORM_SRC_PAD(sparrow));

  GST_DEBUG_OBJECT(sparrow, "gst_sparrow_init. RNG:%p", sparrow->dsfmt);
}

static void
gst_sparrow_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstSparrow *sparrow;

  g_return_if_fail (GST_IS_SPARROW (object));
  sparrow = GST_SPARROW (object);

  GST_DEBUG("gst_sparrow_set_property\n");
  switch (prop_id) {
  case PROP_CALIBRATE:
    if (! value){
      sparrow->next_state = SPARROW_PLAY;
      GST_DEBUG("Calibrate argument is %d\n", sparrow->calibrate);
      break;
  default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}

static void
gst_sparrow_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSparrow *sparrow;

  g_return_if_fail (GST_IS_SPARROW (object));
  sparrow = GST_SPARROW (object);

  switch (prop_id) {
    case PROP_CALIBRATE:
      g_value_set_boolean (value, sparrow->calibrate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_sparrow_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstSparrow *this;
  GstStructure *structure;
  gboolean res;

  this = GST_SPARROW (base);

  GST_DEBUG_OBJECT (this,
      "set_caps: in %" GST_PTR_FORMAT " out %" GST_PTR_FORMAT, incaps, outcaps);

  structure = gst_caps_get_structure (incaps, 0);

  res = gst_structure_get_int (structure, "width", &this->width);
  res &= gst_structure_get_int (structure, "height", &this->height);
  if (!res)
    goto done;

  this->size = this->width * this->height * PIXSIZE;

  GST_INFO("Calibrate is %d\n", this->calibrate);


done:
  return res;
}

/*RNG code */

/*seed with -1 for automatic seed choice */
static void rng_init(GstSparrow *sparrow, guint32 seed){
    GST_DEBUG("in RNG init\n");
    if (seed == -1){
      //seed = 0x33;
      seed = rand();
      GST_DEBUG("Real seed %u\n", seed);
    }
    if (seed == 0)
	seed = 12345;
    GST_DEBUG("in RNG init dfsmt is %p\n", sparrow->dsfmt);
    GST_DEBUG("dfsmt->status is %p\n", sparrow->dsfmt->status);
    dsfmt_init_gen_rand(sparrow->dsfmt, seed);
    /*run the generator here before the threads get going */
    dsfmt_gen_rand_all(sparrow->dsfmt);
    GST_DEBUG("RNG seeded with %u\n", seed);
}

static inline UNUSED guint32
rng_uniform_int(GstSparrow *sparrow, guint32 limit){
  double d = dsfmt_genrand_close_open(sparrow->dsfmt);
  double d2 = d * limit;
  guint32 i = (guint32)d2;
  //GST_DEBUG("RNG int between 0 and %u: %u (from %f, %f)\n", limit, i, d2, d);
  return i;
}

static inline UNUSED double
rng_uniform_double(GstSparrow *sparrow, double limit){
    return dsfmt_genrand_close_open(sparrow->dsfmt) * limit;
}

#define rng_uniform() dsfmt_genrand_close_open(sparrow->dsfmt)



/* here we go */

UNUSED
static void
simple_negation(guint8 * bytes, guint size){
  guint i;
  guint32 * data = (guint32 *)bytes;
  //could use sse for superspeed
  for (i = 0; i < size / 4; i++){
    data[i] = ~data[i];
  }
}

static void
gamma_negation(guint8 * bytes, guint size){
  guint i;
  //XXX  could try oil_tablelookup_u8
  for (i = 0; i < size; i++){
    bytes[i] = sparrow_rgb_gamma_full_range_REVERSE[bytes[i]];
  }
}

#define RANDINT(sparrow, start, end)((start) + rng_uniform_int(sparrow, (end) - (start)))


static void calibrate_new_pattern(GstSparrow *sparrow){
  int i;
  sparrow->calibrate_index = CALIBRATE_PATTERN_L;
  sparrow->calibrate_wait = 0;
  for (i = 0; i < CALIBRATE_PATTERN_L; i++){
    sparrow->calibrate_pattern[i] = RANDINT(sparrow, CALIBRATE_MIN_T, CALIBRATE_MAX_T);
  }
}

static void calibrate_new_state(GstSparrow *sparrow){
  int edge_state = (sparrow->state == SPARROW_FIND_EDGES);

  if (edge_state){
    sparrow->calibrate_size = RANDINT(sparrow, 1, 8);
    sparrow->calibrate_x  = RANDINT(sparrow, 0, sparrow->width - sparrow->calibrate_size);
    sparrow->calibrate_y  = RANDINT(sparrow, 0, sparrow->height - sparrow->calibrate_size);
  }
  else {
    sparrow->calibrate_size = CALIBRATE_SELF_SIZE;
    sparrow->calibrate_x  = RANDINT(sparrow, sparrow->width / 4,
        sparrow->width * 3 / 4 - sparrow->calibrate_size);
    sparrow->calibrate_y  = RANDINT(sparrow, sparrow->height / 4,
        sparrow->height * 3 / 4 - sparrow->calibrate_size);
  }
}



/* in a noisy world, try to find the spot you control by stoping and watching
   for a while.
 */

static inline void
horizontal_line(GstSparrow *sparrow, guint8 *bytes, guint32 y){
  guint stride = sparrow->width * PIXSIZE;
  guint8 * line = bytes + y * stride;
  memset(line, 255, stride);
}

static inline void
vertical_line(GstSparrow *sparrow, guint8 *bytes, guint32 x){
  guint y;
  guint32 *p = (guint32 *)bytes;
  p += x;
  for(y = 0; y < sparrow->height; y++){
    *p = -1;
    p += sparrow->width;
  }
}

static inline void
draw_first_square(GstSparrow *sparrow, guint8 *bytes){
  guint y;
  guint stride = sparrow->width * PIXSIZE;
  guint8 * line = bytes + sparrow->calibrate_y * stride + sparrow->calibrate_x * PIXSIZE;
  for(y = 0; y < sparrow->calibrate_size; y++){
    memset(line, 255, sparrow->calibrate_size * PIXSIZE);
    line += stride;
  }
}


static inline void
record_calibration(GstSparrow *sparrow, gint32 offset, guint32 signal){
  guint16 *t = sparrow->lag_table[offset];
  guint32 r = sparrow->lag_record;
  //guint32 i = sparrow->lag_record;
  while(r){
    if(r & 1){
      *t += signal;
    }
    r >>= 1;
    t++;
  }
  GST_DEBUG("offset: %u signal: %u", offset, signal);
}

static inline IplImage*
ipl_wrap_frame(GstSparrow *sparrow, guint8 *data){
  /*XXX could keep a cache of IPL headers */
  CvSize size = {sparrow->width, sparrow->height};
  IplImage* ipl = cvCreateImageHeader(size, IPL_DEPTH_8U, PIXSIZE);
  int i;
  for (i = 0; i < IPL_IMAGE_COUNT; i++){
    ipl = sparrow->ipl_images + i;
    if (ipl->imageData == NULL){
      cvInitImageHeader(ipl, size, IPL_DEPTH_8U, PIXSIZE, 0, 8);
      ipl->imageData = (char*)data;
      return ipl;
    }
  }
  DISASTEROUS_CRASH("no more ipl images! leaking somewhere?\n");
  return NULL; //never reached, but shuts up warning.
}

static inline void
ipl_free(IplImage *ipl){
  ipl->imageData = NULL;
}

/*compare the frame to the new one. regions of change should indicate the
  square is about.
*/
static inline void
calibrate_find_square(GstSparrow *sparrow, guint8 *bytes){
  //GST_DEBUG("finding square\n");
  if(sparrow->prev_frame){
    IplImage* src1 = ipl_wrap_frame(sparrow, sparrow->prev_frame);
    IplImage* src2 = ipl_wrap_frame(sparrow, bytes);
    IplImage* dest = ipl_wrap_frame(sparrow, sparrow->work_frame);

    cvAbsDiff(src1, src2, dest);
    /*set up the calibration table if it does not exist.
     XXX not dealing with resizing!*/
    if (!sparrow->lag_table){
      sparrow->lag_table = malloc_aligned_or_die(
        sparrow->width * sparrow->height * sizeof(lag_times));
    }

    gint32 i;
    pix_t *changes = (pix_t *)sparrow->work_frame;
    for (i = 0; i < sparrow->height * sparrow->width; i++){
      pix_t p = changes[i];
      guint32 signal = (p >> 8) & 255; //possibly R, G, or B, but never A
      if (signal > CALIBRATE_SIGNAL_THRESHOLD){
        record_calibration(sparrow, i, signal);
      }
    }
    memcpy(sparrow->prev_frame, bytes, sparrow->prev_frame_size);
    ipl_free(src1);
    ipl_free(src2);
    ipl_free(dest);
  }
}

static int cycle_pattern(GstSparrow *sparrow, int repeat){
  if (sparrow->calibrate_wait == 0){
    if(sparrow->calibrate_index == 0){
      //pattern has run out
      if (repeat){
        sparrow->calibrate_index = CALIBRATE_PATTERN_L;
      }
      else{
        calibrate_new_pattern(sparrow);
      }
    }
    sparrow->calibrate_index--;
    sparrow->calibrate_wait = sparrow->calibrate_pattern[sparrow->calibrate_index];
    //GST_DEBUG("cycle_wait %u, cycle_index %u\n", sparrow->calibrate_wait, sparrow->calibrate_index);
  }
  //XXX record the pattern in sparrow->lag_record
  sparrow->lag_record = (sparrow->lag_record << 1) || (sparrow->calibrate_wait == 0);
  //GST_DEBUG("cycle_wait %u, cycle_index %u\n", sparrow->calibrate_wait, sparrow->calibrate_index);

  sparrow->calibrate_wait--;
  return sparrow->calibrate_index & 1;
}

static void
see_grid(GstSparrow *sparrow, guint8 *bytes){
}

static inline void
find_grid(GstSparrow *sparrow, guint8 *bytes){
  see_grid(sparrow, bytes);
  int on = cycle_pattern(sparrow, TRUE);
  memset(bytes, 0, sparrow->size);
  if (on){
    horizontal_line(sparrow, bytes, sparrow->calibrate_y);
  }
}

static inline void
find_edges(GstSparrow *sparrow, guint8 *bytes){
  calibrate_find_square(sparrow, bytes);
  int on = cycle_pattern(sparrow, TRUE);
  memset(bytes, 0, sparrow->size);
  if (on){
    draw_first_square(sparrow, bytes);
  }
}

static inline void
find_self(GstSparrow * sparrow, guint8 * bytes){
  calibrate_find_square(sparrow, bytes);
  int on = cycle_pattern(sparrow, TRUE);
  memset(bytes, 0, sparrow->size);
  if (on){
    //vertical_line(sparrow, bytes, sparrow->calibrate_x);
    //horizontal_line(sparrow, bytes, sparrow->calibrate_y);
    draw_first_square(sparrow, bytes);
  }
}


static void
sparrow_reset(GstSparrow *sparrow, guint8 *bytes){
  size_t size = sparrow->size;
  if (!sparrow->prev_frame){
    sparrow->prev_frame = malloc_aligned_or_die(size);
  }
  if (!sparrow->work_frame){
    sparrow->work_frame = malloc_aligned_or_die(size);
  }
  memcpy(sparrow->prev_frame, bytes, size);
  memset(bytes, 0xff0000, size);
  sparrow->state = SPARROW_FIND_SELF;
  calibrate_new_state(sparrow);
}





static GstFlowReturn
gst_sparrow_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstSparrow *sparrow;
  guint8 *data;
  guint size;
  sparrow = GST_SPARROW (base);
  if (base->passthrough)
    goto done;

  data = GST_BUFFER_DATA(outbuf);
  size = GST_BUFFER_SIZE(outbuf);

  if (size != sparrow->size)
    goto wrong_size;

  switch(sparrow->state){
  case SPARROW_INIT:
    sparrow_reset(sparrow, data);
    break;
  case SPARROW_FIND_SELF:
    find_self(sparrow, data);
    break;
  case SPARROW_FIND_EDGES:
    find_edges(sparrow, data);
    break;
  case SPARROW_FIND_GRID:
    find_grid(sparrow, data);
    break;
  default:
    gamma_negation(data, size);
  }
done:
  return GST_FLOW_OK;

  /* ERRORS */
wrong_size:
  {
    GST_ELEMENT_ERROR (sparrow, STREAM, FORMAT,
        (NULL), ("Invalid buffer size %d, expected %d", size, sparrow->size));
    return GST_FLOW_ERROR;
  }
}



static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (sparrow_debug, "sparrow", 0, "sparrow");
  GST_INFO("gst plugin init\n");

  return gst_element_register (plugin, "sparrow", GST_RANK_NONE, GST_TYPE_SPARROW);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "sparrow",
    "Changes sparrow on video images",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);

