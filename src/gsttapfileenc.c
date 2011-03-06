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
 * SECTION:element-tapfileenc
 *
 * Dumps a Commodore TAP stream into the TAP file format.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! tapenc ! tapfileenc ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <string.h>

#include "gsttapfileenc.h"

/* #defines don't like whitespacey bits */
#define GST_TYPE_TAPFILEENC \
  (gst_tapfileenc_get_type())
#define GST_TAPFILEENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TAPFILEENC,GstTapFileEnc))
#define GST_TAPFILEENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TAPFILEENC,GstTapFileEncClass))
#define GST_IS_TAPFILEENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TAPFILEENC))
#define GST_IS_TAPFILEENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TAPFILEENC))

typedef struct _GstTapFileEnc      GstTapFileEnc;
typedef struct _GstTapFileEncClass GstTapFileEncClass;

struct _GstTapFileEnc
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  guint rate;

  gboolean sent_header;
  gboolean force_version_0;
  guchar machine_byte;
  guchar video_byte;
  guchar version;

  guint length;
};

struct _GstTapFileEncClass
{
  GstElementClass parent_class;
};

GST_DEBUG_CATEGORY_STATIC (gst_tapfileenc_debug);
#define GST_CAT_DEFAULT gst_tapfileenc_debug

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
  PROP_FORCE_VERSION_0
};

static const guint tap_clocks[][2]={
  {985248,1022727}, /* C64 */
  {1108405,1022727}, /* VIC */
  {886724,894886}  /* C16 */
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-tap, "
    "rate = (int) { 886724 , 894886 , 985248 , 1022727 , 1108405 }")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-tap-tap")
    );

GST_BOILERPLATE (GstTapFileEnc, gst_tapfileenc, GstElement,
    GST_TYPE_ELEMENT);

/* GObject vmethod implementations */

static void
gst_tapfileenc_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
    "Commodore 64 TAP file writer",
    "Encoder/Audio",
    "Writes TAP data as TAP files",
    "Fabrizio Gennari <fabrizio.ge@tiscali.it>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

static void
gst_tapfileenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTapFileEnc *filter = GST_TAPFILEENC (object);

  switch (prop_id) {
    case PROP_MACHINE_BYTE:
      filter->machine_byte = (guchar) g_value_get_uint (value);
      break;
    case PROP_VIDEO_BYTE:
      filter->video_byte = (guchar) g_value_get_uint (value);
      break;
    case PROP_FORCE_VERSION_0:
      filter->force_version_0 = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tapfileenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTapFileEnc *filter = GST_TAPFILEENC (object);

  switch (prop_id) {
    case PROP_MACHINE_BYTE:
      g_value_set_uint (value, filter->machine_byte);
      break;
    case PROP_VIDEO_BYTE:
      g_value_set_uint (value, filter->video_byte);
      break;
    case PROP_FORCE_VERSION_0:
      g_value_set_boolean (value, filter->force_version_0);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* initialize the tapfileenc's class */
static void
gst_tapfileenc_class_init (GstTapFileEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_tapfileenc_set_property;
  gobject_class->get_property = gst_tapfileenc_get_property;

  g_object_class_install_property (gobject_class, PROP_MACHINE_BYTE,
      g_param_spec_uint ("machine", "Machine", "Tag representing machine for which this dump is intended. 0=C64, 1=VIC20, 2=C16/+4.", 0, 2,
          0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_VIDEO_BYTE,
      g_param_spec_uint ("videotype", "Video type", "Tag representing video type of machine for which this dump is intended. 0=PAL, 1=NTSC.", 0, 100,
          0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_FORCE_VERSION_0,
      g_param_spec_boolean ("version_0", "Force version 0", "If true, and incoming stream is not semiwaves, a version 0 TAP file will be created. Otherwise the version will be 1 for full waves and 2 for semiwaves.",
          FALSE, G_PARAM_READWRITE));
}

/* GstElement vmethod implementations */

/* this function handles the link with other elements */
static gboolean
gst_tapfileenc_sinkpad_set_caps (GstPad * pad, GstCaps * caps)
{
  GstTapFileEnc *filter = GST_TAPFILEENC (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gint samplerate;
  gboolean ret = gst_structure_get_int (structure, "rate", &samplerate);

  if (!ret)
    GST_ERROR_OBJECT (filter, "input caps have no sample rate field");
  else if (samplerate != tap_clocks[filter->machine_byte][filter->video_byte]) {
    GST_ERROR_OBJECT (filter, "wrong sample rate");
    ret = FALSE;
  }

  if (ret) {
    gboolean semiwaves;
    ret = gst_structure_get_boolean (structure, "semiwaves", &semiwaves);
    if (!ret)
      GST_ERROR_OBJECT (filter, "input caps have no semiwaves field");
    else if (semiwaves)
      filter->version = 2;
    else
      filter->version = filter->force_version_0 ? 0 : 1;
  }

  return ret;
}

static GstCaps *
gst_tapfileenc_sinkpad_get_caps (GstPad * pad)
{
  GstTapFileEnc *filter = GST_TAPFILEENC (gst_pad_get_parent (pad));
  GstCaps * ret = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  GstStructure *structure = gst_caps_get_structure (ret, 0);
  GValue value = {0};

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, tap_clocks[filter->machine_byte][filter->video_byte]);
  gst_structure_set_value (structure, "rate", &value);

  return ret;
}

#define TAP_OUTPUT_SIZE 128

static GstFlowReturn
push_if_needed (GstPad * pad, GstBuffer ** buf, guint numbytes)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstTapFileEnc *filter = GST_TAPFILEENC (GST_OBJECT_PARENT (pad));

  if (*buf == NULL || GST_BUFFER_SIZE(*buf) + numbytes > TAP_OUTPUT_SIZE) {
    GstBuffer * newbuf = gst_buffer_new_and_alloc (TAP_OUTPUT_SIZE);
    GstCaps *caps = GST_PAD_CAPS(pad);

    GST_BUFFER_SIZE(newbuf) = 0;
    gst_buffer_set_caps (newbuf, caps);
    if (*buf != NULL) {
      ret = gst_pad_push (pad, *buf);
      filter->length += GST_BUFFER_SIZE(*buf);
    }
    *buf = newbuf;
  }
  return ret;
}

static GstFlowReturn
add_four_bytes_to_outbuf (GstPad * pad, GstBuffer ** buf, guint pulse)
{
  guchar *data;
  GstFlowReturn ret = push_if_needed (pad, buf, 4);

  data = GST_BUFFER_DATA(*buf) + GST_BUFFER_SIZE(*buf);
  *data++ = 0;
  GST_WRITE_UINT24_LE(data, pulse);
  GST_BUFFER_SIZE(*buf) += 4;

  return ret;
}

static GstFlowReturn
add_byte_to_outbuf (GstPad * pad, GstBuffer ** buf, guchar pulse)
{
  GstFlowReturn ret = push_if_needed (pad, buf, 1);

  GST_BUFFER_DATA(*buf)[GST_BUFFER_SIZE(*buf)] = pulse;
  GST_BUFFER_SIZE(*buf) ++;

  return ret;
}

#define OVERFLOW_HI 0xFFFFFF
#define OVERFLOW_LO 0x800

static GstFlowReturn
add_pulse_to_outbuf (GstPad * pad, GstBuffer ** buf, guint pulse)
{
  if (pulse >= OVERFLOW_LO)
    return add_four_bytes_to_outbuf (pad, buf, pulse);
  return add_byte_to_outbuf (pad, buf, (guchar)(pulse / 8));
}

static GstFlowReturn
write_header(GstPad * pad
             , guchar version
             , guchar machine_byte
             , guchar video_byte
             , guint len
            )
{
  GstBuffer *buf = gst_buffer_new_and_alloc (20);
  guint8 *header = GST_BUFFER_DATA (buf);
  const char signature[] = "C64-FILE-RAW";

  memcpy (header, signature, strlen(signature));
  header += strlen(signature);
  *header++ = version;
  *header++ = machine_byte;
  *header++ = video_byte;
  *header++ = 0;
  GST_WRITE_UINT32_LE (header, len);
  return gst_pad_push (pad, buf);
}

/* chain function
 * this function does the actual processing
 */

static GstFlowReturn
gst_tapfileenc_chain (GstPad * pad, GstBuffer * buf)
{
  GstTapFileEnc *filter = GST_TAPFILEENC (GST_OBJECT_PARENT (pad));
  guint *data = (guint*) GST_BUFFER_DATA(buf);
  guint buflen = GST_BUFFER_SIZE(buf) / sizeof(int32_t);
  guint bufsofar;
  GstFlowReturn ret = GST_FLOW_OK, ret2 = GST_FLOW_OK;
  guint overflow = filter->version != 0 ? OVERFLOW_HI : OVERFLOW_LO;
  guint overflow_to_write = filter->version != 0 ? OVERFLOW_HI : 0;
  GstBuffer * newbuf = gst_buffer_new_and_alloc (GST_BUFFER_SIZE(buf));
  gst_buffer_set_caps (newbuf, gst_pad_get_caps(filter->srcpad));

  if (!filter->sent_header) {
    ret = write_header(filter->srcpad
                      ,filter->version
                      ,filter->machine_byte
                      ,filter->video_byte
                      , 0 /* real length will be written later */
                       );

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
      ret2 = add_pulse_to_outbuf(filter->srcpad, &newbuf, overflow_to_write);
      if (ret2 != GST_FLOW_OK)
        ret = ret2;
      pulse -= overflow;
    }
    if (pulse == 0) {
      if (filter->version != 0)
        ret2 = add_four_bytes_to_outbuf (filter->srcpad, &newbuf, 0);
    }
    else
      ret2 = add_pulse_to_outbuf(filter->srcpad, &newbuf, pulse);
    if (ret2 != GST_FLOW_OK)
        ret = ret2;
  }
  ret2 = gst_pad_push (filter->srcpad, newbuf);
  filter->length += GST_BUFFER_SIZE(newbuf);
  if (ret2 != GST_FLOW_OK)
    ret = ret2;

  return ret;
}

static gboolean
gst_tapfileenc_sink_event (GstPad * pad, GstEvent * event)
{
  GstTapFileEnc *filter = GST_TAPFILEENC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_EOS:
  /* seek to beginning of file */
    gst_pad_push_event (filter->srcpad,
      gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES, 0, -1, 0));
    write_header(filter->srcpad
                      ,filter->version
                      ,filter->machine_byte
                      ,filter->video_byte
                      ,filter->length
                       );
    break;
  default:
    break;
  }

  return gst_pad_event_default (pad, event);
}


/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */

static void
gst_tapfileenc_init (GstTapFileEnc * filter,
    GstTapFileEncClass * gclass)
{
  guint i, nproperties;
  GParamSpec ** properties = g_object_class_list_properties (G_OBJECT_GET_CLASS(filter), &nproperties);

  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_tapfileenc_sinkpad_set_caps));
  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_tapfileenc_chain));
  gst_pad_set_getcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_tapfileenc_sinkpad_get_caps));
  gst_pad_set_event_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tapfileenc_sink_event));

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
  filter->length = 0;
}

gboolean
gst_tapfileenc_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_tapfileenc_debug, "tapfileenc",
      0, "Commodore 64 DMP encoder");

  return gst_element_register (plugin, "tapfileenc", GST_RANK_NONE, GST_TYPE_TAPFILEENC);
}

