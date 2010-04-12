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
static void rng_init(GstSparrow *sparrow, unsigned int seed);
static void simple_negation(guint8 *bytes, guint size);
static void gamma_negation(guint8 *bytes, guint size);
static void calibrate(guint8 *bytes, GstSparrow *sparrow);
static GstFlowReturn gst_sparrow_transform_ip(GstBaseTransform *base, GstBuffer *outbuf);
static gboolean plugin_init(GstPlugin *plugin);

static void calibrate_new_state(GstSparrow *sparrow);


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
  PROP_CALIBRATE
      /* FILL ME */
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

  g_object_class_install_property (gobject_class, PROP_CALIBRATE,
      g_param_spec_boolean ("calibrate", "Calibrate", "calibrate against projection",
          DEFAULT_PROP_CALIBRATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_sparrow_set_caps);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_sparrow_transform_ip);
  GST_INFO("gst class init\n");
}

static void
gst_sparrow_init (GstSparrow * sparrow, GstSparrowClass * g_class)
{
  GST_DEBUG_OBJECT(sparrow, "gst_sparrow_init. RNG:%" GST_PTR_FORMAT, &(sparrow->dsfmt));
  GST_INFO("gst sparrow init\n");

  rng_init(sparrow, -1);
  sparrow->state = SPARROW_INIT;
  sparrow->next_state = SPARROW_FIND_SELF; // can be overridden
  sparrow->calibrate_offset = 1;
  sparrow->calibrate_wait = 0;
  GST_DEBUG_OBJECT(sparrow, "gst_sparrow_init");
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
static void rng_init(GstSparrow *sparrow, unsigned int seed){
    if (seed == -1)
	seed = (unsigned int) time(0) + (unsigned int) clock();
    if (seed == 0)
	seed = 12345;
    dsfmt_init_gen_rand(&(sparrow->dsfmt), seed);
    sparrow->rng_has_init = TRUE;
    GST_DEBUG("RNG seeded with %u\n", seed);
}

static inline UNUSED guint32
rng_uniform_int(GstSparrow *sparrow, guint32 limit){
  double d = dsfmt_genrand_close_open(&(sparrow->dsfmt));
  double d2 = d * limit;
  guint32 i = (guint32)d2;
  //GST_DEBUG("RNG int between 0 and %u: %u (from %f, %f)\n", limit, i, d2, d);
  return i;
}

static inline UNUSED double
rng_uniform_double(GstSparrow *sparrow, double limit){
    return dsfmt_genrand_close_open(&(sparrow->dsfmt)) * limit;
}

#define rng_uniform() dsfmt_genrand_close_open(&(sparrow->dsfmt))



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



static void calibrate_new_state(GstSparrow *sparrow){
  int edge_state = (sparrow->state == SPARROW_FIND_EDGES);
  int i;
  int pattern_len = edge_state ? CALIBRATE_EDGE_PATTERN_L : CALIBRATE_SELF_PATTERN_L;
  sparrow->calibrate_index = pattern_len;

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

  for (i = 0; i < pattern_len; i++){
    sparrow->calibrate_pattern[i] = RANDINT(sparrow, CALIBRATE_MIN_T, CALIBRATE_MAX_T);
  }
}



/* in a noisy world, try to find the spot you control by stoping and watching
   for a while.
 */

static inline void
calibrate_draw_square(guint8 *bytes, GstSparrow *sparrow){
  guint y;
  guint stride = sparrow->width * PIXSIZE;
  guint8 * line = bytes + sparrow->calibrate_y * stride + sparrow->calibrate_x * PIXSIZE;
  for(y = 0; y < sparrow->calibrate_size; y++){
    memset(line, 255, sparrow->calibrate_size * PIXSIZE);
    line += stride;
  }
}

static void
calibrate(guint8 * bytes, GstSparrow * sparrow){
  if (sparrow->calibrate_wait == 0){
    if(sparrow->calibrate_index == 0){
      //pattern has run out. in the case of find_self state, repeat it
      if (sparrow->state == SPARROW_FIND_SELF){
        sparrow->calibrate_index = CALIBRATE_SELF_PATTERN_L;
      }
      else{
        calibrate_new_state(sparrow);
      }
    }
    sparrow->calibrate_index--;
    sparrow->calibrate_wait = sparrow->calibrate_pattern[sparrow->calibrate_index];
  }
  sparrow->calibrate_wait--;
  memset(bytes, 0, sparrow->size);

  if (sparrow->calibrate_index & 1){
    calibrate_draw_square(bytes, sparrow);
  }
}

static void
sparrow_reset(guint8 * bytes, GstSparrow * sparrow){
  memset(bytes, 0xff0000, sparrow->size);
  sparrow->state = SPARROW_FIND_SELF;
  calibrate_new_state(sparrow);
}





static GstFlowReturn
gst_sparrow_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstSparrow *sparrow;
  guint8 *data;
  guint size;
  //GST_DEBUG("doing transform\n");
  sparrow = GST_SPARROW (base);

  if (base->passthrough)
    goto done;

  data = GST_BUFFER_DATA (outbuf);
  size = GST_BUFFER_SIZE (outbuf);

  if (size != sparrow->size)
    goto wrong_size;

  switch(sparrow->state){
  case SPARROW_INIT:
    sparrow_reset(data, sparrow);
    break;
  case SPARROW_FIND_SELF:
  case SPARROW_FIND_EDGES:
    calibrate(data, sparrow);
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
