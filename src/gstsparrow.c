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
  PROP_SILENT
      /* FILL ME */
};


/* the capabilities of the inputs and outputs.
 *
 * Use RGB, not YUV, because inverting video is trivial in RGB, not so in YUV
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRx "; " GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_xBGR "; " GST_VIDEO_CAPS_xRGB)
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRx "; " GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_xBGR "; " GST_VIDEO_CAPS_xRGB)
    );


static void gst_sparrow_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_sparrow_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_sparrow_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps);
static GstFlowReturn gst_sparrow_transform_ip (GstBaseTransform * transform,
    GstBuffer * buf);

static void gst_sparrow_RGB_ip (GstSparrow * sparrow, guint8 * data, gint size);

GST_BOILERPLATE (GstSparrow, gst_sparrow, GstVideoFilter, GST_TYPE_VIDEO_FILTER);


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

  /*
  g_object_class_install_property (gobject_class, PROP_SPARROW,
				   g_param_spec_double ("sparrow", "Sparrow", "sparrow",
							0.01, 10, DEFAULT_PROP_SPARROW,
							G_PARAM_READWRITE));
  */
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_sparrow_set_caps);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_sparrow_transform_ip);
}

static void
gst_sparrow_init (GstSparrow * sparrow, GstSparrowClass * g_class)
{
  GST_DEBUG_OBJECT (sparrow, "gst_sparrow_init");
}

static void
gst_sparrow_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstSparrow *sparrow;

  g_return_if_fail (GST_IS_SPARROW (object));
  sparrow = GST_SPARROW (object);

  GST_DEBUG ("gst_sparrow_set_property");
  switch (prop_id) {
  case PROP_SILENT:
      //do something;
      break;
  default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
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
    /*case PROP_SPARROW:
      g_value_set_double (value, sparrow->sparrow);
      break;
    */
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

  this->size = this->width * this->height * 4;

done:
  return res;
}


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
  for (i = 0; i < size; i++){
    bytes[i] = sparrow_rgb_gamma_full_range_REVERSE[bytes[i]];
  }
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

  data = GST_BUFFER_DATA (outbuf);
  size = GST_BUFFER_SIZE (outbuf);

  if (size != sparrow->size)
    goto wrong_size;

  gamma_negation(data, size);

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

  return gst_element_register (plugin, "sparrow", GST_RANK_NONE, GST_TYPE_SPARROW);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "sparrow",
    "Changes sparrow on video images",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
