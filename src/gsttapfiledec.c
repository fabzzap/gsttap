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
 * Reads a DC2N DMP file and extract its Commodore TAP stream.
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

  guchar version;

  GstAdapter * adapter;
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

/* chain function
 * this function does the actual processing
 */
#define DMP_OUTPUT_SIZE 128

static GstFlowReturn
add_pulse_to_outbuf (GstPad * pad, GstBuffer ** buf, guint pulse)
{
  guint *data;
  GstFlowReturn ret = GST_FLOW_OK;

  if(*buf != NULL &&
     GST_BUFFER_SIZE(*buf) + sizeof(guint) > DMP_OUTPUT_SIZE) {
    ret = gst_pad_push (pad, *buf);
    *buf = NULL;
  }

  if(*buf == NULL) {
    *buf = gst_buffer_new_and_alloc (DMP_OUTPUT_SIZE);
    GST_BUFFER_SIZE(*buf) = 0;
    gst_buffer_set_caps (*buf, GST_PAD_CAPS (pad));
  }

  data = (guint*)(GST_BUFFER_DATA(*buf) + GST_BUFFER_SIZE(*buf));
  *data = pulse;
  GST_BUFFER_SIZE(*buf) += sizeof(guint);

  return ret;
}

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


#define TAPFILEDEC_HEADER_SIZE 20
#define ONE_BYTE_OVERFLOW 0x100
#define THREE_BYTE_OVERFLOW 0xFFFFFF

static GstFlowReturn
gst_tapfiledec_chain (GstPad * pad, GstBuffer * buf)
{
  GstTapFileDec *filter = GST_TAPFILEDEC (GST_OBJECT_PARENT (pad));
  GstFlowReturn ret = GST_FLOW_OK, ret2;
  GstBuffer * newbuf = NULL;
  guint pulse = 0;
  guint peeked_bytes = 0;

  gst_adapter_push (filter->adapter, buf);
  if (!filter->read_header && gst_adapter_available (filter->adapter) >= TAPFILEDEC_HEADER_SIZE) {
    const char expected_signature1[] = "C64-FILE-RAW";
    const char expected_signature2[] = "C16-FILE-RAW";
    GstBuffer *header_buf = gst_adapter_take_buffer (filter->adapter, TAPFILEDEC_HEADER_SIZE);
    guint8 *header_data;
    gboolean header_valid;
    GstCaps * srccaps;
    GstStructure * srcstructure;
    GValue rate = {0}, semiwaves = {0};
    guchar machine, video_standard;

    if (!header_buf)
      return GST_FLOW_ERROR;

    header_data = GST_BUFFER_DATA (header_buf);
    header_valid = !memcmp (header_data, expected_signature1, strlen(expected_signature1))
                || !memcmp (header_data, expected_signature2, strlen(expected_signature2));
    header_data += strlen(expected_signature1);
    filter->version = *header_data++;
    header_valid = header_valid && 
     (filter->version == 0
   || filter->version == 1
   || filter->version == 2
     );
    machine = *header_data++;
    header_valid = header_valid && 
     (machine == 0 /* C64 */
   || machine == 1 /* VIC20 */
   || machine == 2 /* C16 */
     );
    video_standard = *header_data++;
    header_valid = header_valid && 
     (video_standard == 0 /* PAL */
   || video_standard == 1 /* NTSC */
     );

    gst_buffer_unref (header_buf);

    if (!header_valid)
      return GST_FLOW_ERROR;

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
    return GST_FLOW_OK;

  while (gst_adapter_available (filter->adapter) > peeked_bytes) {
    const guint8 * inbytes;
    guint inpulse;
    gboolean overflow_occurred = FALSE;

    inbytes = gst_adapter_peek (filter->adapter, peeked_bytes + 1);
    inpulse = inbytes[peeked_bytes];
    peeked_bytes++;
    if (inpulse == 0) {
      if (filter->version == 0) {
        inpulse = ONE_BYTE_OVERFLOW;
        overflow_occurred = TRUE;
      }
      else {
        inbytes = gst_adapter_peek (filter->adapter, peeked_bytes + 3);
        if (!inbytes)
          break;
        inpulse = GST_READ_UINT24_LE (inbytes + peeked_bytes);
        peeked_bytes += 3;
        if (inpulse == THREE_BYTE_OVERFLOW)
          overflow_occurred = TRUE;
        inpulse /= 8;
      }
    }
    pulse += inpulse;
    if (!overflow_occurred) {
      gst_adapter_flush (filter->adapter, peeked_bytes);
      peeked_bytes = 0;
      ret2 = add_pulse_to_outbuf (filter->srcpad, &newbuf, pulse);
      pulse = 0;
      if (ret2 != GST_FLOW_OK)
        ret = ret2;
    }
  }
  if (newbuf != NULL) {
    ret2 = gst_pad_push (filter->srcpad, newbuf);
    if (ret2 != GST_FLOW_OK)
      ret = ret2;
  }

  return ret;
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

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->adapter = gst_adapter_new ();
  filter->read_header = FALSE;
}

gboolean
gst_tapfiledec_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_tapfiledec_debug, "tapfiledec",
      0, "Commodore 64 DMP encoder");

  return gst_element_register (plugin, "tapfiledec", GST_RANK_MARGINAL, GST_TYPE_TAPFILEDEC);
}

