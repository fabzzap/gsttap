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
 * SECTION:element-dmpenc
 *
 * Dumps a Commodore TAP stream into the DC2N DMP format.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! tapenc ! dmpenc ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <string.h>

#include "gstdmpenc.h"

/* #defines don't like whitespacey bits */
#define GST_TYPE_DMPENC \
  (gst_dmpenc_get_type())
#define GST_DMPENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DMPENC,GstDmpEnc))
#define GST_DMPENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DMPENC,GstDmpEncClass))
#define GST_IS_DMPENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DMPENC))
#define GST_IS_DMPENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DMPENC))

typedef struct _GstDmpEnc      GstDmpEnc;
typedef struct _GstDmpEncClass GstDmpEncClass;

struct _GstDmpEnc
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  guint rate;

  gboolean sent_header;

  guchar machine_byte;
  guchar video_byte;
  guchar bits_per_sample;
};

struct _GstDmpEncClass 
{
  GstElementClass parent_class;
};

/*GType gst_dmpenc_get_type (void);*/

GST_DEBUG_CATEGORY_STATIC (gst_dmpenc_debug);
#define GST_CAT_DEFAULT gst_dmpenc_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_MACHINE_BYTE,
  PROP_VIDEO_BYTE,
  PROP_BITS_PER_SAMPLE
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
    GST_STATIC_CAPS ("audio/x-tap-dmp")
    );

GST_BOILERPLATE (GstDmpEnc, gst_dmpenc, GstElement,
    GST_TYPE_ELEMENT);

/* GObject vmethod implementations */

static void
gst_dmpenc_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
    "Commodore 64 DMP file writer",
    "Encoder/Audio",
    "Writes TAP data as DMP files",
    "Fabrizio Gennari <fabrizio.ge@tiscali.it>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

static void
gst_dmpenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDmpEnc *filter = GST_DMPENC (object);

  switch (prop_id) {
    case PROP_MACHINE_BYTE:
      filter->machine_byte = (guchar) g_value_get_uint (value);
      break;
    case PROP_VIDEO_BYTE:
      filter->video_byte = (guchar) g_value_get_uint (value);
      break;
    case PROP_BITS_PER_SAMPLE:
      filter->bits_per_sample = (guchar) g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dmpenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDmpEnc *filter = GST_DMPENC (object);

  switch (prop_id) {
    case PROP_MACHINE_BYTE:
      g_value_set_uint (value, filter->machine_byte);
      break;
    case PROP_VIDEO_BYTE:
      g_value_set_uint (value, filter->video_byte);
      break;
    case PROP_BITS_PER_SAMPLE:
      g_value_set_uint (value, filter->bits_per_sample);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* initialize the dmpenc's class */
static void
gst_dmpenc_class_init (GstDmpEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_dmpenc_set_property;
  gobject_class->get_property = gst_dmpenc_get_property;

  g_object_class_install_property (gobject_class, PROP_MACHINE_BYTE,
      g_param_spec_uint ("machine", "Machine", "Tag representing machine for which this dump is intended. 0=C64, 1=VIC20, 2=C16/+4. No effect on conversion, only affects 1 byte in the header", 0, 2,
          0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_VIDEO_BYTE,
      g_param_spec_uint ("videotype", "Video type", "Tag representing video type of machine for which this dump is intended. 0=PAL, 1=NTSC. No effect on conversion, only affects 1 byte in the header", 0, 100,
          0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_BITS_PER_SAMPLE,
      g_param_spec_uint ("bits_per_sample", "Bits per sample", "How many bits represent one sample. If more than 8, first bytes will carry least significant bits. If not multiple of 8, most significant bits of last byte are ignored", 0, 32,
          16, G_PARAM_READWRITE));
}

/* GstElement vmethod implementations */

/* this function handles the link with other elements */
static gboolean
gst_dmpenc_sinkpad_set_caps (GstPad * pad, GstCaps * caps)
{
  GstDmpEnc *filter = GST_DMPENC (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gint samplerate;
  gboolean ret = gst_structure_get_int (structure, "rate", &samplerate);
  int i;

  for(i = 0; i < gst_structure_n_fields (structure); i++) {
    char x[512];
    sprintf(x, "fiels %u is %s", i, gst_structure_nth_field_name (structure, i));
    GST_ERROR_OBJECT (filter, x);
  }
  {
    char x[512];
    sprintf(x, "fiels %u", gst_structure_n_fields (structure));
    GST_ERROR_OBJECT (filter, x);
  }
  if (!ret)
    GST_ERROR_OBJECT (filter, "input caps have no sample rate field");
  else
    filter->rate = samplerate;

  gst_object_unref (filter);

  return ret;
}

/* chain function
 * this function does the actual processing
 */
#define DMP_OUTPUT_SIZE 128

static GstFlowReturn
add_pulse_to_outbuf (GstPad * pad, GstBuffer ** buf, guint pulse, guint bytes_per_sample)
{
  guchar *data = GST_BUFFER_DATA(*buf) + GST_BUFFER_SIZE(*buf);
  GstFlowReturn ret = GST_FLOW_OK;

  switch(bytes_per_sample) {
  case 1:
    *data = (guchar)pulse;
    break;
  case 2:
    GST_WRITE_UINT16_LE(data, pulse);
    break;
  case 3:
    GST_WRITE_UINT24_LE(data, pulse);
    break;
  case 4:
    GST_WRITE_UINT32_LE(data, pulse);
    break;
  }

  GST_BUFFER_SIZE(*buf) += bytes_per_sample;
  if (GST_BUFFER_SIZE(*buf) + bytes_per_sample > DMP_OUTPUT_SIZE) {
    GstBuffer * newbuf = gst_buffer_new_and_alloc (DMP_OUTPUT_SIZE * bytes_per_sample);
    gst_buffer_copy_metadata (newbuf, *buf, GST_BUFFER_COPY_CAPS);
    GST_BUFFER_SIZE(newbuf) = 0;
    ret = gst_pad_push (pad, *buf);
    *buf = newbuf;
  }
  return ret;
}

static GstFlowReturn
gst_dmpenc_chain (GstPad * pad, GstBuffer * buf)
{
  GstDmpEnc *filter = GST_DMPENC (GST_OBJECT_PARENT (pad));
  guint *data = (guint*) GST_BUFFER_DATA(buf);
  guint buflen = GST_BUFFER_SIZE(buf) / sizeof(int32_t);
  guint bufsofar;
  GstFlowReturn ret = GST_FLOW_OK, ret2;
  guint bytes_per_sample = (filter->bits_per_sample + 7) / 8;
  guint overflow = (1 << filter->bits_per_sample) - 1;
  GstBuffer * newbuf = gst_buffer_new_and_alloc (DMP_OUTPUT_SIZE * bytes_per_sample);

  if (!filter->sent_header) {
    GstBuffer *buf = gst_buffer_new_and_alloc (20);
    guint8 *header = GST_BUFFER_DATA (buf);
    const char signature[] = "DC2N-TAP-RAW";
    GstFlowReturn ret;

    memcpy (header, signature, strlen(signature));
    header += strlen(signature);
    *header++ = 0; /* format version */
    *header++ = filter->machine_byte; /* not really useful, however the spec says 
                                       0 = Commodore 64
                                       1 = VIC 20
                                       2 = Commodore 16, Plus/4 */
    *header++ = filter->video_byte;   /* not really useful, however the spec says 
                                       0 = PAL
                                       1 = NTSC */
    *header++ = filter->bits_per_sample;
    GST_WRITE_UINT32_LE (header, filter->rate);
    ret = gst_pad_push (filter->srcpad, buf);

    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (filter, "push header failed: flow = %s",
          gst_flow_get_name (ret));
    }
    filter->sent_header = TRUE;
  }

  GST_BUFFER_SIZE(newbuf) = 0;
  for (bufsofar = 0; bufsofar < buflen; bufsofar++) {
    guint pulse = data[bufsofar];
    while (pulse >= overflow) {
      ret2 = add_pulse_to_outbuf(filter->srcpad, &newbuf, overflow, bytes_per_sample);
      if (ret2 != GST_FLOW_OK)
        ret = ret2;
      pulse -= overflow;
    }
    ret2 = add_pulse_to_outbuf(filter->srcpad, &newbuf, pulse, bytes_per_sample);
    if (ret2 != GST_FLOW_OK)
      ret = ret2;
  }
  if (GST_BUFFER_SIZE (newbuf) > 0) {
    ret2 = gst_pad_push (filter->srcpad, newbuf);
    if (ret2 != GST_FLOW_OK)
      ret = ret2;
  }
  else
    gst_buffer_unref (newbuf);

  return ret;
}


/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */

static void
gst_dmpenc_init (GstDmpEnc * filter,
    GstDmpEncClass * gclass)
{
  guint i, nproperties;
  GParamSpec ** properties = g_object_class_list_properties (G_OBJECT_GET_CLASS(filter), &nproperties);

  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_dmpenc_sinkpad_set_caps));
  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_dmpenc_chain));

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

  filter->sent_header = FALSE;
}

gboolean
gst_dmpenc_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_dmpenc_debug, "dmpenc",
      0, "Commodore 64 DMP encoder");

  return gst_element_register (plugin, "dmpenc", GST_RANK_NONE, GST_TYPE_DMPENC);
}

