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

typedef struct _GstTapFileDec      GstTapFileDec;
typedef struct _GstTapFileDecClass GstTapFileDecClass;

struct _GstTapFileDec
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean read_header;
  gboolean invalid_header;

  guchar version;

  guint64 in_offset;

  GstAdapter * adapter;
  GstAdapter * out_adapter;
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
static const guint tap_clocks[][2]={
  {123156,127840}, /* C64 */
  {138550,127840}, /* VIC */
  {110840,111860}  /* C16 */
};

GST_BOILERPLATE (GstTapFileDec, gst_tapfiledec, GstElement,
    GST_TYPE_ELEMENT);

/* GObject vmethod implementations */

static void
gst_tapfiledec_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
    "Commodore 64 TAP file reader",
    "Decoder/Audio",
    "Reads TAP data from TAP files",
    "Fabrizio Gennari <fabrizio.ge@tiscali.it>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the tapfiledec's class */
static void
gst_tapfiledec_class_init (GstTapFileDecClass * klass)
{
}

/* GstElement vmethod implementations */

#define DMP_OUTPUT_SIZE 128

static GstCaps *
gst_tapfiledec_srcpad_get_caps (GstPad * pad)
{
  GstTapFileDec *filter = GST_TAPFILEDEC (gst_pad_get_parent (pad));
  GstCaps * result = filter->read_header ?
    GST_PAD_CAPS (pad) :
    GST_PAD_TEMPLATE_CAPS (GST_PAD_PAD_TEMPLATE (pad));

  GST_DEBUG_OBJECT (filter, "Requested caps from source pad %" GST_PTR_FORMAT, result);
  return gst_caps_copy (result);
}

typedef GstBuffer* (*read_func)(GstTapFileDec * filter, guint numbytes);
typedef void (*flush_func)(GstTapFileDec * filter);

static GstBuffer* read_from_adapter(GstTapFileDec * filter, guint numbytes)
{
  GstBuffer* retval;
  if (gst_adapter_available(filter->adapter) < filter->in_offset + numbytes)
    return NULL;
  retval = gst_buffer_new_and_alloc(numbytes);
  gst_adapter_copy (filter->adapter, GST_BUFFER_DATA(retval), filter->in_offset, numbytes);
  filter->in_offset += numbytes;
  return retval;
}

static GstBuffer* read_from_peer(GstTapFileDec * filter, guint numbytes)
{
  GstBuffer* retval;
  if (gst_pad_pull_range (filter->sinkpad, filter->in_offset, numbytes, &retval) == GST_FLOW_OK) {
    filter->in_offset += GST_BUFFER_SIZE(retval);
    return retval;
  }
  return NULL;
}

static void flush_adapter(GstTapFileDec * filter)
{
  gst_adapter_flush(filter->adapter, filter->in_offset);
  filter->in_offset = 0;
}

static void flush_peer(GstTapFileDec * filter)
{
}

#define TAPFILEDEC_HEADER_SIZE 20
#define ONE_BYTE_OVERFLOW 0x100
#define THREE_BYTE_OVERFLOW 0xFFFFFF

static gboolean
get_pulse_from_tap(GstTapFileDec * filter, read_func read_data, flush_func flush_data, guint *pulse)
{
  gboolean overflow_occurred;

  if (!filter->read_header && !filter->invalid_header) {
    const char expected_signature1[] = "C64-TAPE-RAW";
    const char expected_signature2[] = "C16-TAPE-RAW";
    GstBuffer *header_buf = read_data(filter, TAPFILEDEC_HEADER_SIZE);
    guint8 *header_data;
    GstCaps * srccaps;
    GstStructure * srcstructure;
    GValue rate = {0}, semiwaves = {0};
    guchar machine, video_standard;

    if (header_buf == NULL)
      return FALSE;
    header_data = GST_BUFFER_DATA(header_buf);

    filter->invalid_header =
       memcmp (header_data, expected_signature1, strlen(expected_signature1)) != 0
    && memcmp (header_data, expected_signature2, strlen(expected_signature2)) != 0;
    header_data += strlen(expected_signature1);
    filter->version = *header_data++;
    filter->invalid_header = filter->invalid_header || 
     (filter->version != 0
   && filter->version != 1
   && filter->version != 2
     );
    machine = *header_data++;
    filter->invalid_header = filter->invalid_header || 
     (machine != 0 /* C64 */
   && machine != 1 /* VIC20 */
   && machine != 2 /* C16 */
     );
    video_standard = *header_data++;
    filter->invalid_header = filter->invalid_header || 
     (video_standard != 0 /* PAL */
   && video_standard != 1 /* NTSC */
     );

    gst_buffer_unref(header_buf);
    flush_data (filter);

    if (filter->invalid_header)
      return FALSE;

    g_value_init (&rate, G_TYPE_INT);
    g_value_set_int (&rate, tap_clocks[machine][video_standard]);
    g_value_init (&semiwaves, G_TYPE_BOOLEAN);
    g_value_set_boolean (&semiwaves, filter->version == 2);
    srccaps = gst_pad_get_caps (filter->srcpad);

    filter->read_header = TRUE;

    srcstructure = gst_caps_get_structure (srccaps, 0);
    gst_structure_set_value (srcstructure, "rate", &rate);
    gst_structure_set_value (srcstructure, "semiwaves", &semiwaves);
    gst_pad_set_caps (filter->srcpad, srccaps);
    gst_caps_unref (srccaps);
  }

  /* Don't go past this point before finishing with the header */
  if (!filter->read_header)
    return FALSE;

  *pulse = 0;

  do {
    GstBuffer *inbuf = read_data(filter, 1);
    GstBuffer *inbuf2 = NULL;
    guint8 *inbytes;
    guint inpulse;
    gboolean need_to_read_3_bytes = FALSE;

    overflow_occurred = FALSE;
    if (inbuf == NULL)
      return FALSE;
    inbytes = GST_BUFFER_DATA(inbuf);
    if (inbytes[0] == 0) {
      if (filter->version == 0)
        inpulse = ONE_BYTE_OVERFLOW;
      else {
        need_to_read_3_bytes = TRUE;
        inbuf2 = read_data(filter, 3);
        if (inbuf2) {
          inbytes = GST_BUFFER_DATA(inbuf2);
          inpulse = GST_READ_UINT24_LE (inbytes);
          if (inpulse == THREE_BYTE_OVERFLOW)
            overflow_occurred = TRUE;
          inpulse /= 8;
        }
      }
    }
    else
      inpulse = inbytes[0];
    gst_buffer_unref(inbuf);
    if (need_to_read_3_bytes) {
      if(inbuf2 == NULL)
        return FALSE;
      gst_buffer_unref(inbuf2);
    }
    *pulse += inpulse;
    flush_data (filter);
  } while (overflow_occurred);
  return TRUE;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_tapfiledec_chain (GstPad * pad, GstBuffer * buf)
{
  GstTapFileDec *filter = GST_TAPFILEDEC (GST_OBJECT_PARENT (pad));
  guint32 pulse;

  gst_adapter_push(filter->adapter, buf);
  while(get_pulse_from_tap(filter, read_from_adapter, flush_adapter, &pulse)) {
    GstBuffer* buf = gst_buffer_new_and_alloc(sizeof(pulse));
    guint outbytes;

    *(guint32*)GST_BUFFER_DATA(buf) = pulse;
    gst_adapter_push(filter->out_adapter, buf);
    outbytes = gst_adapter_available(filter->out_adapter);
    if (outbytes >= DMP_OUTPUT_SIZE) {
      GstBuffer *outbuf = gst_adapter_take_buffer(filter->out_adapter, outbytes);
      GstFlowReturn retval;

      gst_buffer_set_caps (outbuf, GST_PAD_CAPS (filter->srcpad));
      retval = gst_pad_push(filter->srcpad, outbuf);
      if (retval != GST_FLOW_OK)
        return retval;
    }
  }
  return filter->invalid_header ? GST_FLOW_ERROR : GST_FLOW_OK;
}

static gboolean
gst_tapfiledec_get_range (GstPad     * pad,
			 guint64      offset,
			 guint        length,
			 GstBuffer ** buf)
{
  GstTapFileDec *filter = GST_TAPFILEDEC (GST_OBJECT_PARENT (pad));
  *buf = gst_buffer_new_and_alloc (length);
  GST_BUFFER_SIZE(*buf) = 0;
  while(GST_BUFFER_SIZE(*buf) + sizeof(guint32) <= length) {
    if (!get_pulse_from_tap(filter, read_from_peer, flush_peer, (guint32*)(GST_BUFFER_DATA(*buf) + GST_BUFFER_SIZE(*buf)))) {
      if (filter->invalid_header) {
        gst_buffer_unref(*buf);
        return GST_FLOW_ERROR;
      }
      break;
    }
    GST_BUFFER_SIZE(*buf) += sizeof(guint32);
  }
  return GST_FLOW_OK;
}

static gboolean
gst_tapfiledec_activate (GstPad * pad)
{
  GstTapFileDec *filter = GST_TAPFILEDEC (GST_OBJECT_PARENT (pad));
  if (GST_PAD_ACTIVATE_MODE (filter->srcpad) == GST_ACTIVATE_PULL) {
    return gst_pad_activate_pull (pad, TRUE);
  }
  return gst_pad_activate_push (pad, TRUE);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */

static void
gst_tapfiledec_init (GstTapFileDec * filter,
    GstTapFileDecClass * gclass)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_tapfiledec_chain));

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_getcaps_function (filter->srcpad,
                                GST_DEBUG_FUNCPTR(gst_tapfiledec_srcpad_get_caps));
  gst_pad_set_getrange_function (filter->srcpad,
      gst_tapfiledec_get_range);
  gst_pad_set_activate_function (filter->sinkpad, gst_tapfiledec_activate);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->adapter = gst_adapter_new ();
  filter->out_adapter = gst_adapter_new ();
  filter->read_header = FALSE;
  filter->invalid_header = FALSE;
}

gboolean
gst_tapfiledec_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_tapfiledec_debug, "tapfiledec",
      0, "Commodore 64 DMP encoder");

  return gst_element_register (plugin, "tapfiledec", GST_RANK_MARGINAL, GST_TYPE_TAPFILEDEC);
}

