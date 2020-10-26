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

#include "gstbasetapcontainerdec.h"

#include <gst/base/gstbytewriter.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_basetapcontainerdec_debug);
#define GST_CAT_DEFAULT gst_basetapcontainerdec_debug

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

G_DEFINE_ABSTRACT_TYPE (GstBaseTapContainerDec, gst_basetapcontainerdec,
    GST_TYPE_ELEMENT);

/* GObject vmethod implementations */
static void
gst_basetapcontainerdec_finalize (GObject * object)
{
  GstBaseTapContainerDec *dec = GST_BASETAPCONTAINERDEC (object);
  g_object_unref (dec->adapter);
}

/* GstElement vmethod implementations */
static GstStateChangeReturn
gst_basetapcontainerdec_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseTapContainerDec *dec = GST_BASETAPCONTAINERDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      dec->header_status = GST_BASE_TAP_CONVERT_NO_HEADER_YET;
      dec->timestamp = 0;
      gst_adapter_clear (dec->adapter);
      break;
    default:
      break;
  }

  return
      GST_ELEMENT_CLASS (gst_basetapcontainerdec_parent_class)->change_state
      (element, transition);
}

/* initialize the tapfiledec's class */
static void
gst_basetapcontainerdec_class_init (GstBaseTapContainerDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gst_element_class_set_metadata (element_class,
      "Commodore tape file reader",
      "Codec/Parser/Audio",
      "Base class to read tape data from tape dump files",
      "Fabrizio Gennari <fabrizio.ge@tiscali.it>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  element_class->change_state = gst_basetapcontainerdec_change_state;
  object_class->finalize = gst_basetapcontainerdec_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_basetapcontainerdec_debug, "basetapcontainerdec", 0,
      "Base class to open file containers for tapes");
}

static const guint8 *
read_from_adapter (GstBaseTapContainerDec * filter, guint numbytes)
{
  const guint8 *retval;

  if (filter->numbytes_from_adapter < filter->in_offset + numbytes)
    return NULL;
  retval = filter->bytes_from_adapter + filter->in_offset;
  filter->in_offset += numbytes;
  return retval;
}

static const guint8 *
read_from_peer (GstBaseTapContainerDec * filter, guint numbytes)
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

static GstCaps *
get_src_caps (GstBaseTapContainerDec * filter) {
  GstCaps *srccaps = gst_pad_get_pad_template_caps (filter->srcpad);

  srccaps = gst_caps_make_writable (srccaps);

  gst_caps_set_simple (srccaps,
        "rate", G_TYPE_INT, filter->rate,
        "halfwaves", G_TYPE_BOOLEAN, filter->halfwaves, NULL);
  return srccaps;
}

static gboolean
read_header (GstBaseTapContainerDec * filter,
    GstBaseTapContainerReadData read_data)
{
  gsize header_size;
  const guint8 *header_data;
  GstEvent *new_segment_event;
  GstSegment new_segment;
  GstTagList *taglist;
  GstBaseTapContainerDecClass *bclass =
      GST_BASETAPCONTAINERDEC_GET_CLASS (filter);

  g_assert (bclass->get_header_size != NULL);
  header_size = bclass->get_header_size (filter);
  header_data = read_data (filter, header_size);
  if (header_data == NULL)
    return FALSE;

  filter->header_status = bclass->read_header (filter, header_data);
  if (filter->header_status == GST_BASE_TAP_CONVERT_VALID_HEADER) {
    GstEvent *new_caps_event;

    GstCaps *srccaps = get_src_caps(filter);
    new_caps_event = gst_event_new_caps (srccaps);
    gst_pad_push_event (filter->srcpad, new_caps_event);
    gst_segment_init (&new_segment, GST_FORMAT_TIME);
    new_segment_event = gst_event_new_segment (&new_segment);
    gst_pad_push_event (filter->srcpad, new_segment_event);
    taglist =
        gst_tag_list_new (GST_TAG_CONTAINER_FORMAT,
        "TAP Commodore tape image file", NULL);
    gst_pad_push_event (filter->srcpad, gst_event_new_tag (taglist));
    filter->to_flush = filter->in_offset;
  }
  return TRUE;
}

static gboolean
get_pulse_from_tap (GstBaseTapContainerDec * filter,
    GstBaseTapContainerReadData read_data, GstByteWriter * writer,
    GstClockTime * duration)
{
  guint pulse = 0;
  GstBaseTapContainerDecClass *bclass =
      GST_BASETAPCONTAINERDEC_GET_CLASS (filter);

  filter->initial_offset = filter->in_offset;
  g_assert (bclass->read_pulse != NULL);
  if (!bclass->read_pulse (filter, read_data, &pulse))
    return FALSE;
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
gst_basetapcontainerdec_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstBaseTapContainerDec *filter = GST_BASETAPCONTAINERDEC (parent);
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
  if (filter->header_status == GST_BASE_TAP_CONVERT_NO_HEADER_YET) {
    gboolean read_header_ret = read_header (filter, read_from_adapter);
    /* Don't go past this point before finishing with the header */
    if (!read_header_ret
        || filter->header_status != GST_BASE_TAP_CONVERT_VALID_HEADER)
      return GST_FLOW_ERROR;
  }
  while (get_pulse_from_tap (filter, read_from_adapter, writer, &duration));
  gst_adapter_unmap (filter->adapter);
  gst_adapter_flush (filter->adapter, filter->to_flush);
  if (filter->header_status == GST_BASE_TAP_CONVERT_NO_VALID_HEADER)
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
gst_basetapcontainerdec_get_range (GstPad * pad,
    GstObject * parent, guint64 offset, guint length, GstBuffer ** buf)
{
  GstBaseTapContainerDec *filter = GST_BASETAPCONTAINERDEC (parent);
  GstByteWriter *writer = gst_byte_writer_new ();
  GstFlowReturn ret = GST_FLOW_ERROR;

  if (filter->header_status == GST_BASE_TAP_CONVERT_NO_HEADER_YET)
    read_header (filter, read_from_peer);

  if (filter->header_status == GST_BASE_TAP_CONVERT_VALID_HEADER) {
    while (gst_byte_writer_get_size (writer) + sizeof (guint32) <= length) {
      if (!get_pulse_from_tap (filter, read_from_peer, writer, NULL))
        break;
    }
    ret = GST_FLOW_OK;
  }

  *buf = gst_byte_writer_free_and_get_buffer (writer);
  return ret;
}

static gboolean
gst_basetapcontainerdec_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GST_LOG ("handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      GST_DEBUG ("eating event");
      gst_event_unref (event);
      return TRUE;
    default:
      GST_DEBUG ("forwarding event");
      return gst_pad_event_default (pad, parent, event);
  }
}

#define BASETAPCONTAINERDEC_PULL_SIZE 256

static void
gst_basetapcontainerdec_loop (GstPad * pad)
{
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstBaseTapContainerDec *filter = GST_BASETAPCONTAINERDEC (GST_PAD_PARENT (pad));
  GstBuffer *buf;

  GST_LOG_OBJECT (filter, "process data");

  switch (filter->header_status) {
    case GST_BASE_TAP_CONVERT_NO_HEADER_YET:
      GST_DEBUG_OBJECT (filter, "no header yet");
      if (read_header (filter, read_from_peer))
        ret = GST_FLOW_OK;
      break;

    case GST_BASE_TAP_CONVERT_VALID_HEADER:
      GST_DEBUG_OBJECT (filter, "getting data");
      ret = gst_basetapcontainerdec_get_range (pad, GST_OBJECT(filter), 0, BASETAPCONTAINERDEC_PULL_SIZE, &buf);
      if (ret == GST_FLOW_OK) {
        ret = gst_basetapcontainerdec_chain (pad, GST_OBJECT(filter), buf);
      }
      break;
    default:
      break;
  }
  if (ret == GST_FLOW_OK)
    return;

  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_DEBUG_OBJECT (filter, "pausing task, reason %s", reason);
    gst_pad_pause_task (pad);

    if (ret == GST_FLOW_EOS) {
      gst_pad_push_event (filter->srcpad, gst_event_new_eos ());
    } else if (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_EOS) {
      /* for fatal errors we post an error message, post the error
       * first so the app knows about the error first. */
      GST_ELEMENT_FLOW_ERROR (filter, ret);
      gst_pad_push_event (filter->srcpad, gst_event_new_eos ());
    }
    return;
  }
}

static gboolean
gst_basetapcontainerdec_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  GstBaseTapContainerDec *filter = GST_BASETAPCONTAINERDEC (parent);
  if (mode == GST_PAD_MODE_PULL) {
    gst_pad_pause_task (filter->sinkpad);
    return gst_pad_activate_mode (filter->sinkpad, mode, active);
  }
  return TRUE;
}

static gboolean
gst_basetapcontainerdec_sink_activate (GstPad * sinkpad, GstObject * parent)
{
  GstQuery *query;
  gboolean pull_mode = FALSE;

  query = gst_query_new_scheduling ();

  if (gst_pad_peer_query (sinkpad, query)) {
    pull_mode = gst_query_has_scheduling_mode_with_flags (query,
      GST_PAD_MODE_PULL, GST_SCHEDULING_FLAG_SEEKABLE);
  }

  gst_query_unref (query);

  if (!pull_mode) {
    GST_DEBUG_OBJECT (sinkpad, "activating push");
    return gst_pad_activate_mode (sinkpad, GST_PAD_MODE_PUSH, TRUE);
  }

  GST_DEBUG_OBJECT (sinkpad, "activating pull");
  return gst_pad_activate_mode (sinkpad, GST_PAD_MODE_PULL, TRUE);
}

static gboolean
gst_basetapcontainerdec_sink_activate_mode (GstPad * sinkpad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean res;

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      res = TRUE;
      break;
    case GST_PAD_MODE_PULL:
      //res = TRUE;
      if (active) {
        /* if we have a scheduler we can start the task */
        res = gst_pad_start_task (sinkpad, (GstTaskFunction) gst_basetapcontainerdec_loop,
            sinkpad, NULL);
      } else {
        res = gst_pad_stop_task (sinkpad);
      }
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

static gboolean
gst_basetapcontainerdec_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res;
  GstBaseTapContainerDec *filter = GST_BASETAPCONTAINERDEC (parent);
  GstCaps *caps, *filtercaps, *resultcaps;

  GST_LOG_OBJECT (pad, "%s query", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
      if (filter->header_status == GST_BASE_TAP_CONVERT_VALID_HEADER) {
        gst_query_parse_caps (query, &filtercaps);
        GST_DEBUG_OBJECT (parent, "query caps, matching with: %" GST_PTR_FORMAT, filtercaps);
        caps = get_src_caps(filter);
        GST_DEBUG_OBJECT (parent, "query caps, current: %" GST_PTR_FORMAT, caps);
        if (filtercaps) {
          resultcaps = gst_caps_intersect_full (filtercaps, caps, GST_CAPS_INTERSECT_FIRST);
          gst_caps_unref(caps);
        } else {
          resultcaps = caps;
        }
        gst_query_set_caps_result (query, resultcaps);
        res = TRUE;
        break;
      }
      // else fallthrough
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }
  return res;
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */

static void
gst_basetapcontainerdec_init (GstBaseTapContainerDec * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_basetapcontainerdec_chain));
  gst_pad_set_event_function (filter->sinkpad, gst_basetapcontainerdec_event);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_getrange_function (filter->srcpad,
      gst_basetapcontainerdec_get_range);
  GST_PAD_SET_PROXY_SCHEDULING (filter->srcpad);
  gst_pad_set_activatemode_function (filter->srcpad,
      gst_basetapcontainerdec_activate_mode);
  gst_pad_set_activatemode_function (filter->sinkpad,
      gst_basetapcontainerdec_sink_activate_mode);
  gst_pad_set_activate_function (filter->sinkpad,
      gst_basetapcontainerdec_sink_activate);
  gst_pad_set_query_function (filter->srcpad,
      gst_basetapcontainerdec_pad_query);

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->adapter = gst_adapter_new ();
  filter->header_status = GST_BASE_TAP_CONVERT_NO_HEADER_YET;
}
