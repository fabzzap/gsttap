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
 * SECTION:element-tapfiledec
 *
 * Reads a Commodore TAP file and extract its Commodore TAP stream.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! tapfiledec ! tapdec ! wavenc ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstbytewriter.h>
#include <string.h>

#include "gsttapfiledec.h"

/* #defines don't like whitespacey bits */
#define GST_TYPE_TAPFILEDEC \
  (gst_tapfiledec_get_type())
#define GST_TAPFILEDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TAPFILEDEC,GstTapFileDec))
#define GST_TAPFILEDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TAPFILEDEC,GstTapFileDecClass))
#define GST_IS_TAPFILEDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TAPFILEDEC))
#define GST_IS_TAPFILEDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TAPFILEDEC))

typedef struct _GstTapFileDec GstTapFileDec;
typedef struct _GstTapFileDecClass GstTapFileDecClass;

struct _GstTapFileDec
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean read_header;

  guchar version;

  gboolean invalid_header;

  guint in_offset;
  GstClockTime timestamp;
  gboolean last_was_0;
  guint rate;

  // push mode
  GstAdapter *adapter;
  const guint8 *bytes_from_adapter;
  guint numbytes_from_adapter;
  gsize to_flush;

  // pull mode
  GstBuffer *pulled_bytes;
  GstMapInfo pulled_bytes_info;
  guint initial_offset;
};

struct _GstTapFileDecClass
{
  GstElementClass parent_class;
};

GST_DEBUG_CATEGORY_STATIC (gst_tapfiledec_debug);
#define GST_CAT_DEFAULT gst_tapfiledec_debug

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-tap-tap")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-tap, "
        "rate = (int) { 110840, 111860, 123156, 127840, 138550 }")
    );

/* Who cares about having 8x these resolutions for pauses anyway? */
static const guint tap_clocks[][2] = {
  {123156, 127840},             /* C64 */
  {138550, 127840},             /* VIC */
  {110840, 111860}              /* C16 */
};

G_DEFINE_TYPE (GstTapFileDec, gst_tapfiledec, GST_TYPE_ELEMENT);

/* GObject vmethod implementations */
static void
gst_tapfiledec_finalize (GObject * object)
{
  GstTapFileDec *dec = GST_TAPFILEDEC (object);
  g_object_unref (dec->adapter);
}

/* GstElement vmethod implementations */
static GstStateChangeReturn
gst_tapfiledec_change_state (GstElement * element, GstStateChange transition)
{
  GstTapFileDec *dec = GST_TAPFILEDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      dec->read_header = FALSE;
      dec->last_was_0 = FALSE;
      dec->timestamp = 0;
      gst_adapter_clear (dec->adapter);
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (gst_tapfiledec_parent_class)->change_state (element,
      transition);
}

/* initialize the tapfiledec's class */
static void
gst_tapfiledec_class_init (GstTapFileDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "Commodore 64 TAP file reader",
      "Codec/Parser/Audio",
      "Reads TAP data from TAP files",
      "Fabrizio Gennari <fabrizio.ge@tiscali.it>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  element_class->change_state = gst_tapfiledec_change_state;
  object_class->finalize = gst_tapfiledec_finalize;
}

typedef const guint8 *(*read_func) (GstTapFileDec * filter, guint numbytes);

static const guint8 *
read_from_adapter (GstTapFileDec * filter, guint numbytes)
{
  const guint8 *retval;

  if (filter->numbytes_from_adapter < filter->in_offset + numbytes)
    return NULL;
  retval = filter->bytes_from_adapter + filter->in_offset;
  filter->in_offset += numbytes;
  return retval;
}

static const guint8 *
read_from_peer (GstTapFileDec * filter, guint numbytes)
{
  const guint8 *ret = NULL;
  if (filter->pulled_bytes) {
    gst_buffer_unmap (filter->pulled_bytes, &filter->pulled_bytes_info);
    gst_buffer_unref (filter->pulled_bytes);
    filter->pulled_bytes = NULL;
  }

  if (gst_pad_pull_range (filter->sinkpad, filter->in_offset, numbytes,
          &filter->pulled_bytes) == GST_FLOW_OK) {
    gsize actual_numbytes = gst_buffer_get_size (filter->pulled_bytes);
    if (actual_numbytes == numbytes) {
      filter->in_offset += actual_numbytes;
      gst_buffer_map (filter->pulled_bytes,
          &filter->pulled_bytes_info, GST_MAP_READ);
      ret = filter->pulled_bytes_info.data;
    } else {
      gst_buffer_unref (filter->pulled_bytes);
      filter->pulled_bytes = NULL;
    }
  }

  return ret;
}


#define TAPFILEDEC_HEADER_SIZE 20
#define VALUE_OF_0_IN_TAP_V0 25000
#define THREE_BYTE_OVERFLOW 0xFFFFFF

static gboolean
get_pulse_from_tap (GstTapFileDec * filter, read_func read_data,
    GstByteWriter * writer, GstClockTime * duration)
{
  gboolean overflow_occurred;
  guint pulse = 0;

  if (!filter->read_header && !filter->invalid_header) {
    const char expected_signature1[] = "C64-TAPE-RAW";
    const char expected_signature2[] = "C16-TAPE-RAW";
    const guint8 *header_data = read_data (filter, TAPFILEDEC_HEADER_SIZE);
    GstCaps *srccaps;
    GstEvent *new_caps_event;
    guchar machine, video_standard;
    GstEvent *new_segment_event;
    GstSegment new_segment;

    if (header_data == NULL)
      return FALSE;

    filter->invalid_header =
        memcmp (header_data, expected_signature1,
        strlen (expected_signature1)) != 0
        && memcmp (header_data, expected_signature2,
        strlen (expected_signature2)) != 0;
    header_data += strlen (expected_signature1);
    filter->version = *header_data++;
    filter->invalid_header = filter->invalid_header ||
        (filter->version != 0 && filter->version != 1 && filter->version != 2);
    machine = *header_data++;
    filter->invalid_header = filter->invalid_header || (machine != 0    /* C64 */
        && machine != 1         /* VIC20 */
        && machine != 2         /* C16 */
        );
    video_standard = *header_data++;
    filter->invalid_header = filter->invalid_header || (video_standard != 0     /* PAL */
        && video_standard != 1  /* NTSC */
        );

    if (filter->invalid_header)
      return FALSE;

    srccaps = gst_pad_query_caps (filter->srcpad, NULL);
    srccaps = gst_caps_make_writable (srccaps);

    filter->read_header = TRUE;
    filter->rate = tap_clocks[machine][video_standard];

    gst_caps_set_simple (srccaps,
        "rate", G_TYPE_INT, filter->rate,
        "halfwaves", G_TYPE_BOOLEAN, filter->version == 2, NULL);

    new_caps_event = gst_event_new_caps (srccaps);
    gst_pad_push_event (filter->srcpad, new_caps_event);
    gst_segment_init (&new_segment, GST_FORMAT_TIME);
    new_segment_event = gst_event_new_segment (&new_segment);
    gst_pad_push_event (filter->srcpad, new_segment_event);
    filter->to_flush = filter->in_offset;
  }

  /* Don't go past this point before finishing with the header */
  if (!filter->read_header)
    return FALSE;
  guint inpulse;
  filter->initial_offset = filter->in_offset;
  do {
    const guint8 *inbytes = read_data (filter, 1);

    overflow_occurred = FALSE;
    if (inbytes == NULL)
      return FALSE;
    inpulse = inbytes[0];
    if (inpulse == 0) {
      if (filter->version == 0) {
        if (!filter->last_was_0) {
          filter->last_was_0 = TRUE;
          inpulse = VALUE_OF_0_IN_TAP_V0;
        } else
          overflow_occurred = TRUE;
      } else {
        inbytes = read_data (filter, 3);
        if (inbytes == NULL)
          return FALSE;
        inpulse = GST_READ_UINT24_LE (inbytes);
        if (inpulse == THREE_BYTE_OVERFLOW)
          overflow_occurred = TRUE;
        inpulse /= 8;
      }
    } else
      filter->last_was_0 = FALSE;
    pulse += inpulse;
  } while (overflow_occurred);
  gst_byte_writer_put_data (writer, (const guint8 *) &pulse, sizeof (pulse));
  filter->to_flush = filter->in_offset;
  if (duration)
    *duration += pulse;
  return TRUE;
}

/* chain function
 * this function does the actual processing
 */

static GstFlowReturn
gst_tapfiledec_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstTapFileDec *filter = GST_TAPFILEDEC (parent);
  GstByteWriter *writer = gst_byte_writer_new ();
  GstClockTime duration = 0;
  guint size = 0;
  GstFlowReturn ret = GST_FLOW_OK;

  gst_adapter_push (filter->adapter, buf);
  filter->numbytes_from_adapter = gst_adapter_available (filter->adapter);
  filter->bytes_from_adapter =
      gst_adapter_map (filter->adapter, filter->numbytes_from_adapter);
  filter->in_offset = 0;
  filter->to_flush = 0;
  while (get_pulse_from_tap (filter, read_from_adapter, writer, &duration));
  gst_adapter_unmap (filter->adapter);
  gst_adapter_flush (filter->adapter, filter->to_flush);
  if (filter->invalid_header)
    ret = GST_FLOW_ERROR;
  else
    size = gst_byte_writer_get_size (writer);
  if (size > 0) {
    GstBuffer *newbuf = gst_byte_writer_free_and_get_buffer (writer);
    GST_BUFFER_DTS (newbuf) = filter->timestamp;
    duration = gst_util_uint64_scale (duration, GST_SECOND, filter->rate);
    GST_BUFFER_DURATION (newbuf) = duration;
    filter->timestamp += duration;
    ret = gst_pad_push (filter->srcpad, newbuf);
  } else
    gst_byte_writer_free (writer);

  return ret;
}

static GstFlowReturn
gst_tapfiledec_get_range (GstPad * pad,
    GstObject * parent, guint64 offset, guint length, GstBuffer ** buf)
{
  GstTapFileDec *filter = GST_TAPFILEDEC (parent);
  GstByteWriter *writer = gst_byte_writer_new ();
  while (gst_byte_writer_get_size (writer) + sizeof (guint32) <= length) {
    if (!get_pulse_from_tap (filter, read_from_peer, writer, NULL)) {
      if (filter->invalid_header) {
        gst_buffer_unref (*buf);
        return GST_FLOW_ERROR;
      }
      break;
    }
  }
  *buf = gst_byte_writer_free_and_get_buffer (writer);
  return GST_FLOW_OK;
}

static gboolean
gst_tapfiledec_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GST_LOG ("handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    case GST_EVENT_SEGMENT:
      GST_DEBUG ("eating event");
      gst_event_unref (event);
      return TRUE;
    default:
      GST_DEBUG ("forwarding event");
      return gst_pad_event_default (pad, parent, event);
  }
}

static gboolean
gst_tapfiledec_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  GstTapFileDec *filter = GST_TAPFILEDEC (parent);
  return gst_pad_activate_mode (filter->sinkpad, mode, active);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */

static void
gst_tapfiledec_init (GstTapFileDec * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tapfiledec_chain));
  gst_pad_set_event_function (filter->sinkpad, gst_tapfiledec_event);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_getrange_function (filter->srcpad, gst_tapfiledec_get_range);
  GST_PAD_SET_PROXY_SCHEDULING (filter->srcpad);
  gst_pad_set_activatemode_function (filter->srcpad,
      gst_tapfiledec_activate_mode);

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->adapter = gst_adapter_new ();
  filter->read_header = FALSE;
  filter->invalid_header = FALSE;
  filter->last_was_0 = FALSE;
}

gboolean
gst_tapfiledec_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_tapfiledec_debug, "tapfiledec",
      0, "Commodore TAP file decoder");

  return gst_element_register (plugin, "tapfiledec", GST_RANK_MARGINAL,
      GST_TYPE_TAPFILEDEC);
}
