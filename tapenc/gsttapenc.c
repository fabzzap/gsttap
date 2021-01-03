/*
 * GStreamer
 * Copyright (C) 2011-2014 Fabrizio Gennari <fabrizio.ge@tiscali.it>
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

#include <gst/base/gstbytewriter.h>
#include <gst/audio/audio.h>

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

typedef struct _GstTapEnc GstTapEnc;
typedef struct _GstTapEncClass GstTapEncClass;

struct _GstTapEnc
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean inverted;

  gboolean halfwaves;

  guchar sensitivity;

  guint min_duration;

  guchar initial_threshold;

  GstBuffer *pull_buffer;

  GstMapInfo map;

  uint32_t buflen;
  int32_t *data;
  uint32_t buffer_consumed;
  struct tap_enc_t *tap;
  GCond cond;
  GMutex mutex;
  gboolean is_eos;
};

struct _GstTapEncClass
{
  GstElementClass parent_class;
};

enum
{
  PROP_0,
  PROP_MIN_DURATION,
  PROP_SENSITIVITY,
  PROP_INVERTED,
  PROP_HALFWAVES,
  PROP_INITIAL_THRESHOLD
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw,"
        "format=" GST_AUDIO_NE (S32) "," "channels=(int)1")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-tap")
    );

#define gst_tapenc_parent_class parent_class
G_DEFINE_TYPE (GstTapEnc, gst_tapenc, GST_TYPE_ELEMENT);

/* GObject vmethod implementations */

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
    {
      gboolean inverted = g_value_get_boolean (value);
      if (inverted != filter->inverted) {
        filter->inverted = inverted;
        if (filter->tap)
          tapenc_invert (filter->tap);
      }
      break;
    }
    case PROP_HALFWAVES:
      filter->halfwaves = g_value_get_boolean (value);
      if (filter->tap)
        tapenc_toggle_trigger_on_both_edges (filter->tap, filter->halfwaves);
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
    case PROP_HALFWAVES:
      g_value_set_boolean (value, filter->halfwaves);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tapenc_finalize (GObject * object)
{
  GstTapEnc *filter = GST_TAPENC (object);

  g_mutex_clear (&filter->mutex);
  g_cond_clear (&filter->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_tapenc_change_state (GstElement * object, GstStateChange transition)
{
  GstTapEnc *filter = GST_TAPENC (object);
  GstStateChangeReturn result =
      GST_ELEMENT_CLASS (parent_class)->change_state (object, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      tapenc_exit (filter->tap);
      filter->tap = NULL;
      break;
    default:
      break;
  }

  return result;
}

static gboolean
gst_tapenc_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstTapEnc *filter = GST_TAPENC (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (GST_PAD_MODE (filter->srcpad) == GST_PAD_MODE_PUSH) {
        if (filter->tap != NULL) {
          uint32_t flushed_pulses = tapenc_flush (filter->tap);
          if (flushed_pulses > 0) {
            GstByteWriter *writer = gst_byte_writer_new ();
            GstBuffer *buffer;
            gst_byte_writer_put_data (writer, (const guint8 *) &flushed_pulses,
                sizeof (flushed_pulses));
            buffer = gst_byte_writer_free_and_get_buffer (writer);
            gst_pad_push (filter->srcpad, buffer);
          }
        }
      } else {
        g_mutex_lock (&filter->mutex);
        filter->is_eos = TRUE;
        g_cond_signal (&filter->cond);
        g_mutex_unlock (&filter->mutex);
      }
      break;
    case GST_EVENT_FLUSH_STOP:
      tapenc_flush (filter->tap);
      break;
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      GstStructure *structure;
      GstCaps *srccaps;
      gint samplerate;
      GstEvent *new_caps_event;
      GstEvent *new_segment_event;
      GstSegment new_segment;

      gst_event_parse_caps (event, &caps);
      structure = gst_caps_get_structure (caps, 0);

      if (!gst_structure_get_int (structure, "rate", &samplerate)) {
        GST_ERROR_OBJECT (filter, "input caps have no sample rate field");
        return FALSE;
      }

      srccaps =
          gst_caps_make_writable (gst_pad_get_pad_template_caps
          (filter->srcpad));
      GST_DEBUG_OBJECT (srccaps, "caps before");
      structure = gst_caps_get_structure (srccaps, 0);
      gst_structure_set (structure,
          "rate", G_TYPE_INT, samplerate,
          "halfwaves", G_TYPE_BOOLEAN, filter->halfwaves, NULL);
      GST_DEBUG_OBJECT (srccaps, "caps after");

      filter->tap = tapenc_init2 (filter->min_duration,
          filter->sensitivity, filter->initial_threshold, filter->inverted);

      tapenc_toggle_trigger_on_both_edges (filter->tap, filter->halfwaves);

      new_caps_event = gst_event_new_caps (srccaps);
      gst_pad_push_event (filter->srcpad, new_caps_event);
      gst_segment_init (&new_segment, GST_FORMAT_TIME);
      new_segment_event = gst_event_new_segment (&new_segment);
      gst_pad_push_event (filter->srcpad, new_segment_event);
    }
      break;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

/* initialize the tapencoder's class */
static void
gst_tapenc_class_init (GstTapEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gst_element_class_set_details_simple (gstelement_class,
      "Commodore 64 TAP format encoder",
      "Encoder/Audio",
      "Encodes audio as Commodore 64 TAP data",
      "Fabrizio Gennari <fabrizio.ge@tiscali.it>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
  gobject_class->set_property = gst_tapenc_set_property;
  gobject_class->get_property = gst_tapenc_get_property;
  gobject_class->finalize = gst_tapenc_finalize;
  gstelement_class->change_state = gst_tapenc_change_state;

  g_object_class_install_property (gobject_class, PROP_MIN_DURATION,
      g_param_spec_uint ("min-duration", "Minimum duration",
          "Minimum duration of a pulse, in samples", 0, UINT_MAX, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, PROP_SENSITIVITY,
      g_param_spec_uint ("sensitivity", "Sensitivity",
          "How much the detector should be sensitive to a wave much smaller than the previous one. 100 = detect all waves. 0 = all waves less than 1/2 high than the previous one are ignored",
          0, 100, 12,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, PROP_INVERTED,
      g_param_spec_boolean ("inverted", "Inverted waveform",
          "If true, the input waveform will be treated as inverted (upside down). A positive signal will be interpreted as negative and vice versa",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, PROP_HALFWAVES,
      g_param_spec_boolean ("halfwaves", "Use halfwaves",
          "If true, both rising edges and falling edges are boundaries between pulses. Some C16/+4 tapes need it",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, PROP_INITIAL_THRESHOLD,
      g_param_spec_uint ("initial-threshold", "Initial threshold",
          "Level the signal needs to reach to overcome initial noise", 0, 127,
          20, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  GST_DEBUG_CATEGORY_INIT (gst_tapenc_debug, "tapenc",
      0, "Commodore TAP format encoder");
}

/* GstElement vmethod implementations */

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_tapenc_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstTapEnc *filter = GST_TAPENC (GST_OBJECT_PARENT (pad));
  GstFlowReturn ret = GST_FLOW_OK;
  GstByteWriter *writer = gst_byte_writer_new ();
  guint size;


  if (GST_PAD_MODE (filter->srcpad) == GST_PAD_MODE_PULL) {
    g_mutex_lock (&filter->mutex);

    while (filter->pull_buffer != NULL)
      g_cond_wait (&filter->cond, &filter->mutex);
    filter->pull_buffer = buf;
    gst_buffer_map (buf, &filter->map, GST_MAP_READ);
    filter->data = (int32_t *) filter->map.data;
    filter->buflen = filter->map.size / sizeof (int32_t);
    filter->buffer_consumed = 0;

    g_cond_signal (&filter->cond);
    g_mutex_unlock (&filter->mutex);
  } else {
    gst_buffer_map (buf, &filter->map, GST_MAP_READ);
    filter->data = (int32_t *) filter->map.data;
    filter->buflen = filter->map.size / sizeof (int32_t);
    filter->buffer_consumed = 0;
    while (filter->buffer_consumed < filter->buflen) {
      uint32_t pulse;
      filter->buffer_consumed +=
          tapenc_get_pulse (filter->tap, filter->data + filter->buffer_consumed,
          filter->buflen - filter->buffer_consumed, &pulse);
      if (pulse > 0)
        gst_byte_writer_put_data (writer, (const guint8 *) &pulse,
            sizeof (pulse));
    }

    size = gst_byte_writer_get_size (writer);
    if (size > 0) {
      GstBuffer *newbuf = gst_byte_writer_free_and_get_buffer (writer);
      ret = gst_pad_push (filter->srcpad, newbuf);
    } else
      gst_byte_writer_free (writer);
    gst_buffer_unmap (buf, &filter->map);
  }

  return ret;
}

static GstFlowReturn
gst_tapenc_get_range (GstPad * pad,
    GstObject * parent, guint64 offset, guint length, GstBuffer ** buf)
{
  GstTapEnc *filter = GST_TAPENC (GST_OBJECT_PARENT (pad));
  GstByteWriter *writer = gst_byte_writer_new ();

  g_mutex_lock (&filter->mutex);


  do {
    uint32_t pulse;

    while (filter->pull_buffer == NULL && !filter->is_eos)
      g_cond_wait (&filter->cond, &filter->mutex);
    if (filter->pull_buffer == NULL) {
      if (gst_byte_writer_get_size (writer) < length) {
        pulse = tapenc_flush (filter->tap);
        if (pulse > 0)
          gst_byte_writer_put_data (writer, (const guint8 *) &pulse,
              sizeof (pulse));
      }
      break;
    }

    if (filter->buflen <= filter->buffer_consumed) {
      gst_buffer_unmap (filter->pull_buffer, &filter->map);
      gst_buffer_unref (filter->pull_buffer);
      filter->pull_buffer = NULL;
      filter->buffer_consumed = 0;

      g_cond_signal (&filter->cond);
      continue;
    }
    if (gst_byte_writer_get_size (writer) >= length)
      break;

    filter->buffer_consumed +=
        tapenc_get_pulse (filter->tap, filter->data + filter->buffer_consumed,
        filter->buflen - filter->buffer_consumed, &pulse);
    if (pulse > 0)
      gst_byte_writer_put_data (writer, (const guint8 *) &pulse,
          sizeof (pulse));
  } while (1);

  g_mutex_unlock (&filter->mutex);
  *buf = gst_byte_writer_free_and_get_buffer (writer);
  return GST_FLOW_OK;
}

static gboolean
gst_tapenc_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_SCHEDULING:
      gst_query_set_scheduling (query, GST_SCHEDULING_FLAG_SEQUENTIAL, 4, -1,
          4);
      gst_query_add_scheduling_mode (query, GST_PAD_MODE_PULL);
      gst_query_add_scheduling_mode (query, GST_PAD_MODE_PUSH);

      return TRUE;
    case GST_QUERY_POSITION:
    {
      GstTapEnc *filter = GST_TAPENC (parent);
      gint64 pos = (gint64) tapenc_get_last_trigger (filter->tap);
      gst_query_set_position (query, GST_FORMAT_BYTES, pos);
    }
      return TRUE;
    default:
      return gst_pad_query_default (pad, parent, query);
  }
}

static gboolean
gst_tapenc_src_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean result = TRUE;

  GstTapEnc *trans = GST_TAPENC (parent);

  if (mode == GST_PAD_MODE_PULL) {
    GstQuery *query;
    gboolean pull_mode;

    /* first check what upstream scheduling is supported */
    query = gst_query_new_scheduling ();

    if (!gst_pad_peer_query (trans->sinkpad, query)) {
      gst_query_unref (query);
      result =
        gst_pad_activate_mode (trans->sinkpad, GST_PAD_MODE_PUSH, active);
    } else {
      /* see if pull-mode is supported */
      pull_mode = gst_query_has_scheduling_mode (query,
        GST_PAD_MODE_PULL);
      gst_query_unref (query);

      if (pull_mode)
        result =
          gst_pad_activate_mode (trans->sinkpad, GST_PAD_MODE_PULL, active);
    }
  }

  return result;
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_tapenc_init (GstTapEnc * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tapenc_chain));
  gst_pad_set_event_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tapenc_sink_event));

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_getrange_function (filter->srcpad, gst_tapenc_get_range);
  gst_pad_set_query_function (filter->srcpad, gst_tapenc_query);
  gst_pad_set_activatemode_function (filter->srcpad, gst_tapenc_src_activate_mode);

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  g_mutex_init (&filter->mutex);
  g_cond_init (&filter->cond);
}

static gboolean
gst_tapenc_register (GstPlugin * plugin)
{
  return gst_element_register (plugin, "tapenc", GST_RANK_NONE,
      GST_TYPE_TAPENC);
}

/* gstreamer looks for this structure to register tapencoders
 *
 *
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    tapenc,
    "Commodore tape encoder support",
    gst_tapenc_register,
    VERSION,
    "LGPL",
    PACKAGE,
    "http://wav-prg.sourceforge.net/"
);

