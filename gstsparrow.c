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
static GstFlowReturn gst_sparrow_transform_ip(GstBaseTransform *base, GstBuffer *outbuf);
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

  g_object_class_install_property (gobject_class, PROP_CALIBRATE,
      g_param_spec_boolean ("calibrate", "Calibrate", "calibrate against projection [on]",
          DEFAULT_PROP_CALIBRATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEBUG,
      g_param_spec_boolean ("debug", "Debug", "add a debug screen [off]",
          DEFAULT_PROP_DEBUG,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RNG_SEED,
      g_param_spec_uint ("rngseed", "RNGSeed", "Seed for the random number generator [-1, meaning auto]",
          0, (guint32)-1, (guint32)DEFAULT_PROP_RNG_SEED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_sparrow_set_caps);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_sparrow_transform_ip);
  GST_INFO("gst class init\n");
}

static void
gst_sparrow_init (GstSparrow * sparrow, GstSparrowClass * g_class)
{
  GST_INFO("gst sparrow init\n");
  /*disallow resizing */
  gst_pad_use_fixed_caps(GST_BASE_TRANSFORM_SRC_PAD(sparrow));
}

static void
gst_sparrow_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstSparrow *sparrow;

  g_return_if_fail (GST_IS_SPARROW (object));
  sparrow = GST_SPARROW (object);

  GST_DEBUG("gst_sparrow_set_property\n");
  if (value){
    switch (prop_id) {
    case PROP_CALIBRATE:
      sparrow->calibrate = g_value_get_boolean(value);
      GST_DEBUG("Calibrate argument is %d\n", sparrow->calibrate);
      break;
    case PROP_DEBUG:
      sparrow->debug = g_value_get_boolean(value);
      GST_DEBUG("debug_value is %d\n", sparrow->debug);
      break;
    case PROP_RNG_SEED:
      sparrow->rng_seed = g_value_get_uint(value);
      GST_DEBUG("rng seed is %d\n", sparrow->rng_seed);
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
    case PROP_DEBUG:
      g_value_set_boolean(value, sparrow->debug);
      break;
    case PROP_RNG_SEED:
      g_value_set_uint(value, sparrow->rng_seed);
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
  GST_INFO("set_caps\n");
  GstSparrow *sparrow = GST_SPARROW(base);
  GstStructure *structure;
  gboolean res;

  structure = gst_caps_get_structure (incaps, 0);

  res = gst_structure_get_int (structure, "width", &sparrow->width);
  res &= gst_structure_get_int (structure, "height", &sparrow->height);
  if (!res)
    goto done;

  GST_DEBUG_OBJECT (sparrow,
      "set_caps: \nin %" GST_PTR_FORMAT " \nout %" GST_PTR_FORMAT, incaps, outcaps);

  /* set_caps gets called after set_property, so it is a good place for hooks
     that depend on properties and that need to be run before everything
     starts. */

  sparrow_init(sparrow, incaps);
done:
  return res;
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
  sparrow_transform(sparrow, data);
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
    "Add sparrows to video streams",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);



