/*
 * GStreamer
 * Copyright (C) 2011-2013 Fabrizio Gennari <fabrizio.ge@tiscali.it>
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
#include <gst/audio/gstaudiodecoder.h>

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

typedef struct _GstTapDec GstTapDec;
typedef struct _GstTapDecClass GstTapDecClass;

struct _GstTapDec
{
  GstAudioDecoder element;

  gboolean inverted;
  guint volume;
  gint waveform;

  struct tap_dec_t *tap;
};

struct _GstTapDecClass
{
  GstAudioDecoderClass parent_class;
};

enum
{
  PROP_0,
  PROP_VOLUME,
  PROP_TRIGGER_ON_RISING_EDGE,
  PROP_WAVEFORM,
  N_PROPERTIES
};

static GType
gst_waveforms_get_type (void)
{
  static GType waveforms_type = 0;

  if (waveforms_type == 0) {
    static const GEnumValue waveforms_profiles[] = {
      {TAPDEC_SQUARE, "square", "Square wave"},
      {TAPDEC_TRIANGLE, "triangle", "Triangular wave"},
      {TAPDEC_SINE, "sine", "Sinusoidal wave"},
      {0, NULL, NULL},
    };
    waveforms_type =
        g_enum_register_static ("GstTapEncWaveforms", waveforms_profiles);
  }

  return waveforms_type;
}

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
    GST_STATIC_CAPS ("audio/x-raw, "
        "format=" GST_AUDIO_NE (S32) ", "
        "channels = (int) 1," "layout = (string) interleaved")
    );

G_DEFINE_TYPE (GstTapDec, gst_tapdec, GST_TYPE_AUDIO_DECODER);

/* GObject vmethod implementations */

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
      filter->waveform = g_value_get_enum (value);
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
      g_value_set_enum (value, filter->waveform);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstAudioDecoder vmethod implementations */

static gboolean
gst_tapdec_start (GstAudioDecoder * parent)
{
  GstTapDec *filter = gst_tapdec (parent);
  filter->tap = tapdec_init2 (filter->volume,
      filter->inverted, filter->waveform);
  gst_audio_decoder_set_estimate_rate (parent, TRUE);
  return TRUE;
}

static gboolean
gst_tapdec_set_format (GstAudioDecoder * parent, GstCaps * caps)
{
  GstTapDec *filter = gst_tapdec (parent);
  GstCaps *out_caps;
  GstAudioInfo info;
  gboolean ret;
  gboolean halfwaves;
  GstStructure *structure;
  const GValue *rate;

  GST_DEBUG_OBJECT (filter, "Getting input caps");

  structure = gst_caps_get_structure (caps, 0);
  if (!(rate = gst_structure_get_value (structure, "rate"))) {
    GST_ERROR_OBJECT (filter, "input caps have no sample rate field");
    return FALSE;
  }
  if (!gst_structure_get_boolean (structure, "halfwaves", &halfwaves)) {
    GST_ERROR_OBJECT (filter, "input caps have no halfwaves field");
    return FALSE;
  }
  GST_DEBUG_OBJECT (filter, "Setting output caps, rate %d, %s waves",
      g_value_get_int (rate), halfwaves ? "half" : "full");

  tapdec_enable_halfwaves (filter->tap, halfwaves);
  out_caps =
      gst_caps_make_writable (gst_static_pad_template_get_caps (&src_factory));
  gst_caps_set_value (out_caps, "rate", rate);
  ret = gst_audio_info_from_caps (&info, out_caps);
  gst_caps_unref (out_caps);
  if (!ret)
    return FALSE;
  gst_audio_decoder_set_output_format (parent, &info);
  return TRUE;
}

static GstFlowReturn
gst_tapdec_parse (GstAudioDecoder * dec,
    GstAdapter * adapter, gint * offset, gint * length)
{
  guint size = gst_adapter_available (adapter);
  g_return_val_if_fail (size > 0, GST_FLOW_ERROR);
  *offset = 0;
  *length = size - (size % 4);

  return GST_FLOW_OK;
}

static gboolean
gst_tapdec_stop (GstAudioDecoder * dec)
{
  GstTapDec *filter = gst_tapdec (dec);
  tapdec_exit (filter->tap);
  filter->tap = NULL;
  return TRUE;
}

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

/* GstElement vmethod implementations */


/* chain function
 * this function does the actual processing
 */

#define TAPDEC_OUTBUF_SIZE 1024

static GstFlowReturn
gst_tapdec_handle_frame (GstAudioDecoder * parent, GstBuffer * buf)
{
  GstTapDec *filter = gst_tapdec (parent);
  int32_t *data;
  int32_t *outdata;
  uint32_t buflen;
  uint32_t bufsofar;
  uint32_t total_pulses;
  GstMapInfo map;
  GstBuffer *outbuf;
  GstMapInfo outmap;
  GstMemory *outmemory = NULL;

  if (filter->tap == NULL) {
    GST_ERROR_OBJECT (filter, "not initialised: input not a tape?");
    return GST_FLOW_ERROR;
  }

  if (buf == NULL) {
    GST_LOG_OBJECT (filter, "No input data");
    return GST_FLOW_OK;
  }

  gst_buffer_map (buf, &map, GST_MAP_READ);
  data = (int32_t *) map.data;
  buflen = map.size / sizeof (int32_t);

  outbuf = gst_buffer_new ();

  for (bufsofar = 0; bufsofar < buflen; bufsofar++) {
    uint32_t npulses;
    tapdec_set_pulse (filter->tap, data[bufsofar]);
    do {
      if (outmemory == NULL) {
        outmemory =
            gst_allocator_alloc (NULL,
            TAPDEC_OUTBUF_SIZE * sizeof (int32_t), NULL);
        total_pulses = 0;
        gst_memory_map (outmemory, &outmap, GST_MAP_WRITE);
        outdata = (int32_t *) outmap.data;
      }
      npulses =
          tapdec_get_buffer (filter->tap, outdata + total_pulses,
          TAPDEC_OUTBUF_SIZE - total_pulses);
      total_pulses += npulses;
      if (total_pulses >= TAPDEC_OUTBUF_SIZE) {
        gst_memory_unmap (outmemory, &outmap);
        gst_memory_resize (outmemory, 0, total_pulses * sizeof (int32_t));
        gst_buffer_append_memory (outbuf, outmemory);
        outmemory = NULL;
      }
    } while (npulses > 0);
  }

  if (outmemory != NULL) {
    gst_memory_unmap (outmemory, &outmap);
    gst_memory_resize (outmemory, 0, total_pulses * sizeof (int32_t));
    gst_buffer_append_memory (outbuf, outmemory);
  }
  return gst_audio_decoder_finish_frame (parent, outbuf, 1);
}


/* initialize the tapdecoder's class */
static void
gst_tapdec_class_init (GstTapDecClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstAudioDecoderClass *gstaudiodecoder_class = (GstAudioDecoderClass *) klass;

  gst_element_class_set_details_simple (gstelement_class,
      "Commodore 64 TAP format decoder",
      "Decoder/Audio",
      "Decodes Commodore TAP data to audio",
      "Fabrizio Gennari <fabrizio.ge@tiscali.it>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
  gobject_class->set_property = gst_tapdec_set_property;
  gobject_class->get_property = gst_tapdec_get_property;

  obj_properties[PROP_VOLUME] =
      g_param_spec_uint ("volume", "Volume", "Volume", 0, 255,
      254, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
  obj_properties[PROP_TRIGGER_ON_RISING_EDGE] =
      g_param_spec_boolean ("inverted", "Inverted waveform",
      "If true, the output waveform will be inverted: a positive signal will become negative and vice versa",
      TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
  obj_properties[PROP_WAVEFORM] =
      g_param_spec_enum ("waveform", "Waveform",
      "Waveform to be used in output", gst_waveforms_get_type (), TAPDEC_SQUARE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
  g_object_class_install_properties (gobject_class, N_PROPERTIES,
      obj_properties);

  gstaudiodecoder_class->start = gst_tapdec_start;
  gstaudiodecoder_class->stop = gst_tapdec_stop;
  gstaudiodecoder_class->set_format = gst_tapdec_set_format;
  gstaudiodecoder_class->parse = gst_tapdec_parse;
  gstaudiodecoder_class->handle_frame = gst_tapdec_handle_frame;
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_tapdec_init (GstTapDec * filter)
{
}

static gboolean
gst_tapdec_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_tapdec_debug, "tapdec",
      0, "Commodore TAP format decoder");
  return gst_element_register (plugin, "tapdec", GST_RANK_SECONDARY,
      GST_TYPE_TAPDEC);
}

/* gstreamer looks for this structure to register tapencoders
 *
 *
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    tapdec,
    "Commodore tape decoder support",
    gst_tapdec_register,
    VERSION,
    "LGPL",
    PACKAGE,
    "http://wav-prg.sourceforge.net/"
);

