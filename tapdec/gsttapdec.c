/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2011 Fabrizio Gennari <fabrizio.ge@tiscali.it>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:element-tapdec
 *
 * Convert a Commodore TAP stream to audio format.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! tapefiledec ! tapdec ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "tapdecoder.h"

GST_DEBUG_CATEGORY_STATIC (gst_tapdec_debug);
#define GST_CAT_DEFAULT gst_tapdec_debug

#define GST_TYPE_TAPDEC \
  (gst_tapdec_get_type())
#define gst_tapdec(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TAPDEC,GstTapDec))
#define gst_tapdec_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TAPDEC,GstTapDecClass))
#define GST_IS_TAPDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TAPDEC))
#define GST_IS_TAPDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TAPDEC))

typedef struct _GstTapDec      GstTapDec;
typedef struct _GstTapDecClass GstTapDecClass;

struct _GstTapDec
{
  GstElement element;
  GstPad *sinkpad, *srcpad;

  gboolean inverted;
  guint volume;
  guint waveform;

  struct tap_dec_t *tap;
};

struct _GstTapDecClass 
{
  GstElementClass parent_class;
};

enum
{
  PROP_0,
  PROP_VOLUME,
  PROP_TRIGGER_ON_RISING_EDGE,
  PROP_WAVEFORM
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-tap")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
      "width = (int) 32, "
      "depth = (int) 32, "
      "signed = (boolean) TRUE, "
      "endianness = (int) BYTE_ORDER, "
      "channels = (int) 1")
    );

GST_BOILERPLATE (GstTapDec, gst_tapdec, GstElement,
    GST_TYPE_ELEMENT);

/* GObject vmethod implementations */

static void
gst_tapdec_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
    "Commodore 64 TAP format decoder",
    "Decoder/Audio",
    "Decodes Commodore 64 TAP data to audio",
    "Fabrizio Gennari <fabrizio.ge@tiscali.it>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

static void
gst_tapdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTapDec *filter = gst_tapdec (object);

  switch (prop_id) {
    case PROP_VOLUME:
      filter->volume = g_value_get_uint (value);
      break;
    case PROP_TRIGGER_ON_RISING_EDGE:
      filter->inverted = g_value_get_boolean (value);
      break;
    case PROP_WAVEFORM:
      filter->waveform = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tapdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTapDec *filter = gst_tapdec (object);

  switch (prop_id) {
    case PROP_VOLUME:
      g_value_set_uint (value, filter->volume);
      break;
    case PROP_TRIGGER_ON_RISING_EDGE:
      g_value_set_boolean (value, filter->inverted);
      break;
    case PROP_WAVEFORM:
      g_value_set_uint (value, filter->waveform);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_tapdec_change_state (GstElement * object, GstStateChange transition)
{
  GstTapDec *filter = gst_tapdec (object);
  GstStateChangeReturn result = parent_class->change_state (object, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      free (filter->tap);
      filter->tap = NULL;
      break;
    default:
      break;
  }

  return result;
}

/* initialize the tapdecoder's class */
static void
gst_tapdec_class_init (GstTapDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_tapdec_set_property;
  gobject_class->get_property = gst_tapdec_get_property;
  gstelement_class->change_state = gst_tapdec_change_state;

  g_object_class_install_property (gobject_class, PROP_VOLUME,
      g_param_spec_uint ("volume", "Volume", "Volume", 0, 255,
          254, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_TRIGGER_ON_RISING_EDGE,
      g_param_spec_boolean ("inverted", "Inverted waveform", "If true, the output waveform will be  inverted: a positive signal will become negative and vice versa",
          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_WAVEFORM,
      g_param_spec_uint ("waveform", "Waveform", "0=square, 1=triangle, 2=sine (if tapdecoder does not support sine, will fall back to square)", 0, 2,
          0, G_PARAM_READWRITE));
}

/* GstElement vmethod implementations */

/* this function handles the link with other elements */
static gboolean
gst_tapdec_sinkpad_set_caps (GstPad * pad, GstCaps * caps)
{
  GstTapDec *filter = gst_tapdec (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstCaps *othercaps;
  gint samplerate;
  gboolean semiwaves;

  if (!gst_structure_get_int (structure, "rate", &samplerate))
  {
    GST_ERROR_OBJECT (filter, "input caps have no sample rate field");
    return FALSE;
  }
  if (!gst_structure_get_boolean (structure, "semiwaves", &semiwaves))
  {
    GST_ERROR_OBJECT (filter, "input caps have no semiwaves field");
    return FALSE;
  }

  filter->tap = tapdecoder_init(filter->volume,
                            filter->inverted,
                            semiwaves,
                            filter->waveform==2 ? TAPDEC_SINE :
                            filter->waveform==1 ? TAPDEC_TRIANGLE :
                            TAPDEC_SQUARE
                            );
  othercaps =
      gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, samplerate,
      "width", G_TYPE_INT, 32,
      "depth", G_TYPE_INT, 32,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "endianness", G_TYPE_INT, BYTE_ORDER,
      "channels", G_TYPE_INT, 1, 
      NULL);

  gst_pad_set_caps (filter->srcpad, othercaps);
  gst_caps_unref (othercaps);

  return TRUE;
}

/* chain function
 * this function does the actual processing
 */

#define TAPDEC_OUTBUF_SIZE 128

static GstFlowReturn
gst_tapdec_chain (GstPad * pad, GstBuffer * buf)
{
  GstTapDec *filter = gst_tapdec (GST_OBJECT_PARENT (pad));
  int32_t *data = (int32_t*) GST_BUFFER_DATA(buf);
  uint32_t buflen = GST_BUFFER_SIZE(buf) / sizeof(int32_t);
  uint32_t bufsofar;
  GstFlowReturn ret = GST_FLOW_OK;

  if (filter->tap == NULL) {
    GST_ERROR_OBJECT (filter, "not initialised: input not a tape?");
    return GST_FLOW_ERROR;
  }

  for (bufsofar = 0; bufsofar < buflen; bufsofar++) {
    uint32_t npulses;
    GstBuffer *outbuf;

    tapdec_set_pulse (filter->tap, data[bufsofar]);
    do {
      outbuf = gst_buffer_new_and_alloc(TAPDEC_OUTBUF_SIZE * sizeof(int32_t));
      npulses = tapdec_get_buffer(filter->tap, (int32_t*)GST_BUFFER_DATA (outbuf), TAPDEC_OUTBUF_SIZE);
      if (npulses > 0) {
        GstFlowReturn ret2;

        GST_BUFFER_SIZE (outbuf) = npulses * sizeof(int32_t);
        gst_buffer_set_caps(outbuf, GST_PAD_CAPS(filter->srcpad));
        ret2 = gst_pad_push (filter->srcpad, outbuf);
        if (ret2 != GST_FLOW_OK)
          ret = ret2;
      }
    } while (npulses > 0);
    gst_buffer_unref (outbuf);
  }

  return ret;
}


/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_tapdec_init (GstTapDec * filter,
    GstTapDecClass * gclass)
{
  guint i, nproperties;
  GParamSpec ** properties = g_object_class_list_properties (G_OBJECT_GET_CLASS(filter), &nproperties);

  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_tapdec_sinkpad_set_caps));
  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_tapdec_chain));

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  /* set defaults for properties */
  for (i = 0;i < nproperties; i++) {
    GValue default_value = {0,};
    g_value_init (&default_value, G_PARAM_SPEC_VALUE_TYPE(properties[i]));
    g_param_value_set_default (properties[i], &default_value);
    g_object_set_property (G_OBJECT(filter), g_param_spec_get_name (properties[i]), &default_value);
  }
  g_free(properties);
}

static gboolean
gst_tapdec_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_tapdec_debug, "tapdec",
      0, "Commodore 64 TAP format decoder");
  return gst_element_register (plugin, "tapdec", GST_RANK_SECONDARY, GST_TYPE_TAPDEC);
}

/* gstreamer looks for this structure to register tapencoders
 *
 *
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "tapdec",
    "Commodore tape decoder support",
    gst_tapdec_register,
    VERSION,
    "LGPL",
    PACKAGE,
    "http://wav-prg.sourceforge.net/"
);

