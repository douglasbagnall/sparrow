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

#include "sparrow.h"
#include "gstsparrow.h"
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>
GST_DEBUG_CATEGORY (sparrow_debug);

#include <string.h>
#include <math.h>

/* static_functions */
static void gst_sparrow_base_init(gpointer g_class);
static void gst_sparrow_class_init(GstSparrowClass *g_class);
static void gst_sparrow_init(GstSparrow *sparrow, GstSparrowClass *g_class);
static void gst_sparrow_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_sparrow_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static gboolean gst_sparrow_set_caps(GstBaseTransform *base, GstCaps *incaps, GstCaps *outcaps);
static GstFlowReturn gst_sparrow_transform(GstBaseTransform *base, GstBuffer *inbuf, GstBuffer *outbuf);
static gboolean plugin_init(GstPlugin *plugin);


//#include <sparrow.c>
#include <sparrow.h>


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


/* Clean up */
static void
gst_sparrow_finalize (GObject * obj){
  GST_DEBUG("in gst_sparrow_finalize!\n");
  GstSparrow *sparrow = GST_SPARROW(obj);
  sparrow_finalise(sparrow);
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

  g_object_class_install_property (gobject_class, PROP_DEBUG,
      g_param_spec_boolean ("debug", "Debug", "save PPM files of internal state [off]",
          DEFAULT_PROP_DEBUG,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TIMER,
      g_param_spec_boolean ("timer", "Timer", "Time each transform [off]",
          DEFAULT_PROP_TIMER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RNG_SEED,
      g_param_spec_uint ("rngseed", "RNGSeed", "Seed for the random number generator [-1, meaning auto]",
          0, (guint32)-1, (guint32)DEFAULT_PROP_RNG_SEED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_COLOUR,
      g_param_spec_uint ("colour", "Colour", "Colour for calibration (" QUOTE(SPARROW_GREEN)
          " for green, " QUOTE(SPARROW_MAGENTA) "for magenta) [" QUOTE(DEFAULT_PROP_COLOUR) "]",
          0, SPARROW_LAST_COLOUR - 1, (guint32)DEFAULT_PROP_COLOUR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RELOAD,
      g_param_spec_string ("reload", "Reload", "reload calibration from this file, don't calibrate [None]",
          DEFAULT_PROP_RELOAD, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SAVE,
      g_param_spec_string ("save", "Save", "save calibration details to this file [None]",
          DEFAULT_PROP_SAVE, G_PARAM_READWRITE));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_sparrow_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_sparrow_transform);
  GST_INFO("gst class init\n");
}

static void
gst_sparrow_init (GstSparrow * sparrow, GstSparrowClass * g_class)
{
  GST_INFO("gst sparrow init\n");
  /*disallow resizing */
  gst_pad_use_fixed_caps(GST_BASE_TRANSFORM_SRC_PAD(sparrow));
}

static inline void
set_string_prop(const GValue *value, const char **target){
  const char *s = g_value_dup_string(value);
  size_t len = strlen(s);
  if(len){
    *target = s;
  }
}

static void
gst_sparrow_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstSparrow *sparrow;

  g_return_if_fail (GST_IS_SPARROW (object));
  sparrow = GST_SPARROW (object);
  guint val;
  GST_DEBUG("gst_sparrow_set_property\n");
  if (value){
    switch (prop_id) {
    case PROP_DEBUG:
      sparrow->debug = g_value_get_boolean(value);
      GST_DEBUG("debug_value is %d\n", sparrow->debug);
      break;
    case PROP_TIMER:
      sparrow->use_timer = g_value_get_boolean(value);
      GST_DEBUG("timer_value is %d\n", sparrow->use_timer);
      break;
    case PROP_RNG_SEED:
      sparrow->rng_seed = g_value_get_uint(value);
      GST_DEBUG("rng seed is %d\n", sparrow->rng_seed);
      break;
    case PROP_COLOUR:
      val = g_value_get_uint(value);
      if (val < SPARROW_LAST_COLOUR){
        sparrow->colour = val;
      }
      GST_DEBUG("colour is %d\n", sparrow->rng_seed);
      break;
    case PROP_RELOAD:
      set_string_prop(value, &sparrow->reload);
      GST_DEBUG("reload is %s\n", sparrow->reload);
      break;
    case PROP_SAVE:
      set_string_prop(value, &sparrow->save);
      GST_DEBUG("save is %s\n", sparrow->save);
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
    case PROP_DEBUG:
      g_value_set_boolean(value, sparrow->debug);
      break;
    case PROP_TIMER:
      g_value_set_boolean(value, sparrow->use_timer);
      break;
    case PROP_RNG_SEED:
      g_value_set_uint(value, sparrow->rng_seed);
      break;
    case PROP_COLOUR:
      g_value_set_uint(value, sparrow->colour);
      break;
    case PROP_RELOAD:
      g_value_set_string(value, sparrow->reload);
      break;
    case PROP_SAVE:
      g_value_set_string(value, sparrow->save);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}



static gboolean
gst_sparrow_set_caps (GstBaseTransform * base, GstCaps * incaps, GstCaps * outcaps)
{
  GST_INFO("set_caps\n");
  GstSparrow *sparrow = GST_SPARROW(base);

  GST_DEBUG_OBJECT (sparrow,
      "set_caps: \nin %" GST_PTR_FORMAT " \nout %" GST_PTR_FORMAT, incaps, outcaps);

  /* set_caps gets called after set_property, so it is a good place for hooks
     that depend on properties and that need to be run before everything
     starts. */

  return sparrow_init(sparrow, incaps, outcaps);
}



static GstFlowReturn
gst_sparrow_transform (GstBaseTransform * base, GstBuffer *inbuf, GstBuffer *outbuf)
{
  GstSparrow *sparrow = GST_SPARROW(base);
  guint insize = GST_BUFFER_SIZE(inbuf);
  guint outsize = GST_BUFFER_SIZE(outbuf);

  if (insize != sparrow->in.size || outsize != sparrow->out.size)
    goto wrong_size;

  sparrow_transform(sparrow, inbuf, outbuf);
  return GST_FLOW_OK;

  /* ERRORS */
wrong_size:
  {
    GST_ELEMENT_ERROR (sparrow, STREAM, FORMAT,
        (NULL), ("Invalid buffer size(s)\nIN:  size %d, expected %d\nOUT: size %d, expected %d",
            insize, sparrow->in.size, outsize, sparrow->out.size));
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
    "Add sparrows to video streams",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);



