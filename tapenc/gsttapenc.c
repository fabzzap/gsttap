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
 * SECTION:element-tapenc
 *
 * Convert an audio stream to the Commodore TAP format.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! tapenc ! tapefileenc ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "tapencoder.h"

GST_DEBUG_CATEGORY_STATIC (gst_tapenc_debug);
#define GST_CAT_DEFAULT gst_tapenc_debug

#define GST_TYPE_TAPENC \
  (gst_tapenc_get_type())
#define GST_TAPENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TAPENC,GstTapEnc))
#define GST_TAPENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TAPENC,GstTapEncClass))
#define GST_IS_TAPENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TAPENC))
#define GST_IS_TAPENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TAPENC))

typedef struct _GstTapEnc      GstTapEnc;
typedef struct _GstTapEncClass GstTapEncClass;

struct _GstTapEnc
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean inverted;

  gboolean semiwaves;

  guchar sensitivity;

  guint min_duration;

  guchar initial_threshold;

  GstBuffer *outbuf;

  struct tap_enc_t *tap;
};

struct _GstTapEncClass 
{
  GstElementClass parent_class;
};

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_MIN_DURATION,
  PROP_SENSITIVITY,
  PROP_INVERTED,
  PROP_SEMIWAVES,
  PROP_INITIAL_THRESHOLD
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
      "width = (int) 32, "
      "depth = (int) 32, "
      "signed = (boolean) TRUE, "
      "endianness = (int) BYTE_ORDER, "
      "channels = (int) 1")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-tap")
    );

GST_BOILERPLATE (GstTapEnc, gst_tapenc, GstElement,
    GST_TYPE_ELEMENT);

#define TAPENC_OUTBUF_SIZE 128

static GstFlowReturn
add_pulse_to_outbuf (GstTapEnc *filter, uint32_t pulse)
{
  GstFlowReturn ret = GST_FLOW_OK;

  if(filter->outbuf != NULL 
      && GST_BUFFER_SIZE(filter->outbuf) + sizeof(uint32_t) > TAPENC_OUTBUF_SIZE) {
    ret = gst_pad_push (filter->srcpad, filter->outbuf);
    filter->outbuf = NULL;
  }
  if (filter->outbuf == NULL) {
    GstCaps *caps = GST_PAD_CAPS(filter->srcpad);

    filter->outbuf = gst_buffer_new_and_alloc (TAPENC_OUTBUF_SIZE);
    GST_BUFFER_SIZE(filter->outbuf) = 0;
    gst_buffer_set_caps(filter->outbuf, caps);
  }
  *((uint32_t*)(GST_BUFFER_DATA(filter->outbuf) + GST_BUFFER_SIZE(filter->outbuf))) = pulse;
  GST_BUFFER_SIZE(filter->outbuf) += sizeof(uint32_t);

  return ret;
}


/* GObject vmethod implementations */

static void
gst_tapenc_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
    "Commodore 64 TAP format encoder",
    "Encoder/Audio",
    "Encodes audio as Commodore 64 TAP data",
    "Fabrizio Gennari <fabrizio.ge@tiscali.it>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

static void
gst_tapenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTapEnc *filter = GST_TAPENC (object);

  switch (prop_id) {
    case PROP_MIN_DURATION:
      filter->min_duration = g_value_get_uint (value);
      break;
    case PROP_SENSITIVITY:
      filter->sensitivity = (guchar) g_value_get_uint (value);
      break;
    case PROP_INITIAL_THRESHOLD:
      filter->initial_threshold = (guchar) g_value_get_uint (value);
      break;
    case PROP_INVERTED:
      filter->inverted = g_value_get_boolean (value);
      break;
    case PROP_SEMIWAVES:
      filter->semiwaves = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tapenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTapEnc *filter = GST_TAPENC (object);

  switch (prop_id) {
    case PROP_MIN_DURATION:
      g_value_set_uint (value, filter->min_duration);
      break;
    case PROP_SENSITIVITY:
      g_value_set_uint (value, filter->sensitivity);
      break;
    case PROP_INITIAL_THRESHOLD:
      g_value_set_uint (value, filter->initial_threshold);
      break;
    case PROP_INVERTED:
      g_value_set_boolean (value, filter->inverted);
      break;
    case PROP_SEMIWAVES:
      g_value_set_boolean (value, filter->semiwaves);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_tapenc_change_state (GstElement * object, GstStateChange transition)
{
  GstTapEnc *filter = GST_TAPENC (object);
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

static gboolean
gst_tapenc_sink_event (GstPad * pad, GstEvent * event)
{
  GstTapEnc *filter = GST_TAPENC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_EOS:
    if (filter->tap != NULL) {
      uint32_t flushed_pulses = tapenc_flush(filter->tap);
      add_pulse_to_outbuf(filter, flushed_pulses);
      gst_pad_push (filter->srcpad, filter->outbuf);
    }
    break;
  case GST_EVENT_FLUSH_STOP:
    tapenc_flush(filter->tap);
    break;
  default:
    break;
  }

  return gst_pad_event_default (pad, event);
}

/* initialize the tapencoder's class */
static void
gst_tapenc_class_init (GstTapEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_tapenc_set_property;
  gobject_class->get_property = gst_tapenc_get_property;
  gstelement_class->change_state = gst_tapenc_change_state;

  g_object_class_install_property (gobject_class, PROP_MIN_DURATION,
      g_param_spec_uint ("min_duration", "Minimum duration", "Minimum duration of a pulse, in samples", 0, UINT_MAX,
          0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_SENSITIVITY,
      g_param_spec_uint ("sensitivity", "Sensitivity", "How much the detector should be sensitive to a wave much smaller than the previous one. 100 = detect all waves. 0 = all waves less than 1/2 high than the previous one are ignored", 0, 100,
          12, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_INVERTED,
      g_param_spec_boolean ("inverted", "Inverted waveform", "If true, the input waveform will be treated as inverted (upside down). A positive signal will be interpreted as negative and vice versa",
          FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_SEMIWAVES,
      g_param_spec_boolean ("semiwaves", "Use semiwaves", "If true, both rising edges and falling edges are boundaries between pulses. Some C16/+4 tapes need it",
          FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_INITIAL_THRESHOLD,
      g_param_spec_uint ("initial_threshold", "Initial threshold", "Level the signal needs to reach to overcome initial noise", 0, 127,
          20, G_PARAM_READWRITE));
}

/* GstElement vmethod implementations */

/* this function handles the link with other elements */
static gboolean
gst_tapenc_sinkpad_set_caps (GstPad * pad, GstCaps * caps)
{
  GstTapEnc *filter = GST_TAPENC (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstCaps *othercaps;
  gint samplerate;

  if (!gst_structure_get_int (structure, "rate", &samplerate))
  {
    GST_ERROR_OBJECT (filter, "input caps have no sample rate field");
    return FALSE;
  }

  othercaps =
      gst_caps_new_simple ("audio/x-tap",
      "rate", G_TYPE_INT, samplerate, 
      "semiwaves", G_TYPE_BOOLEAN, filter->semiwaves, 
      NULL);

  filter->tap = tapencoder_init(filter->min_duration,
                            filter->sensitivity,
                            filter->initial_threshold,
                            filter->inverted,
                            filter->semiwaves);

  gst_pad_set_caps (filter->srcpad, othercaps);
  gst_caps_unref (othercaps);

  return TRUE;
}

static GstCaps *
gst_tapenc_sinkpad_get_caps (GstPad * pad)
{
  GstTapEnc *filter = GST_TAPENC (gst_pad_get_parent (pad));
  GstCaps * ret = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  GstStructure *structure = gst_caps_get_structure (ret, 0);
  GstPad * peer = gst_pad_get_peer (filter->srcpad);

  GST_DEBUG_OBJECT (filter, "initial: %" GST_PTR_FORMAT, structure);
  if (peer) {
    GstCaps *othercaps = gst_pad_get_caps (peer);

    if (othercaps && gst_caps_get_size (othercaps) > 0) {
      GstStructure *otherstructure = gst_caps_get_structure (othercaps, 0);
      const GValue * rate = gst_structure_get_value (otherstructure, "rate");

      GST_DEBUG_OBJECT (filter, "peer: %" GST_PTR_FORMAT, otherstructure);
      if (G_VALUE_HOLDS(rate, G_TYPE_INT)) {
        gst_structure_set_value (structure, "rate", rate);
      }
    }
  }
  GST_DEBUG_OBJECT (filter, "returning: %" GST_PTR_FORMAT, structure);

  return ret;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_tapenc_chain (GstPad * pad, GstBuffer * buf)
{
  GstTapEnc *filter = GST_TAPENC (GST_OBJECT_PARENT (pad));
  int32_t *data = (int32_t*) GST_BUFFER_DATA(buf);
  uint32_t buflen = GST_BUFFER_SIZE(buf) / sizeof(int32_t);
  uint32_t bufsofar = 0;
  GstFlowReturn ret = GST_FLOW_OK;

  while(bufsofar < buflen) {
    uint32_t pulse;
    bufsofar += tapenc_get_pulse(filter->tap, data + bufsofar, buflen - bufsofar, &pulse);
    if (pulse > 0)
      ret = add_pulse_to_outbuf(filter, pulse);
  }

  return ret;
}


/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_tapenc_init (GstTapEnc * filter,
    GstTapEncClass * gclass)
{
  guint i, nproperties;
  GParamSpec ** properties = g_object_class_list_properties (G_OBJECT_GET_CLASS(filter), &nproperties);

  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_tapenc_sinkpad_set_caps));
  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_tapenc_chain));
  gst_pad_set_event_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_tapenc_sink_event));
  gst_pad_set_getcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_tapenc_sinkpad_get_caps));

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

  filter->outbuf = NULL;
}

static gboolean
gst_tapenc_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_tapenc_debug, "tapenc",
      0, "Commodore 64 TAP format encoder");
  return gst_element_register (plugin, "tapenc", GST_RANK_NONE, GST_TYPE_TAPENC);
}

/* gstreamer looks for this structure to register tapencoders
 *
 *
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "tapenc",
    "Commodore tape encoder support",
    gst_tapenc_register,
    VERSION,
    "LGPL",
    PACKAGE,
    "http://wav-prg.sourceforge.net/"
);
