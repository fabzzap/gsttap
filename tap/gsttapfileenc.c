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
#include <gst/base/gstbytewriter.h>
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

typedef struct _GstTapFileEnc GstTapFileEnc;
typedef struct _GstTapFileEncClass GstTapFileEncClass;

struct _GstTapFileEnc
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean sent_header;
  gboolean force_version_0;
  guchar machine_byte;
  guchar video_byte;
  guchar version;
  gboolean last_was_overflow;

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

static const guint tap_clocks[][2] = {
  {985248, 1022727},            /* C64 */
  {1108405, 1022727},           /* VIC */
  {886724, 894886}              /* C16 */
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

G_DEFINE_TYPE (GstTapFileEnc, gst_tapfileenc, GST_TYPE_ELEMENT);

/* GObject vmethod implementations */

static void
gst_tapfileenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTapFileEnc *filter = GST_TAPFILEENC (object);

  switch (prop_id) {
    case PROP_MACHINE_BYTE:
      filter->machine_byte = (guchar) g_value_get_enum (value);
      break;
    case PROP_VIDEO_BYTE:
      filter->video_byte = (guchar) g_value_get_enum (value);
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
      g_value_set_enum (value, filter->machine_byte);
      break;
    case PROP_VIDEO_BYTE:
      g_value_set_enum (value, filter->video_byte);
      break;
    case PROP_FORCE_VERSION_0:
      g_value_set_boolean (value, filter->force_version_0);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Machine entry used in extended TAP header */
#define TAP_MACHINE_C64 0
#define TAP_MACHINE_VIC 1
#define TAP_MACHINE_C16 2
#define TAP_MACHINE_MAX 2

/* Video standards */
#define TAP_VIDEOTYPE_PAL  0
#define TAP_VIDEOTYPE_NTSC 1
#define TAP_VIDEOTYPE_MAX  1

static GType
gst_machines_get_type (void)
{
  static GType machines_type = 0;

  if (machines_type == 0) {
    static const GEnumValue machines_profiles[] = {
      {TAP_MACHINE_C64, "C64", "Commodore 64"},
      {TAP_MACHINE_C16, "C16", "Commodore 16/Plus-4"},
      {TAP_MACHINE_VIC, "VIC20", "Commodore VIC-20"},
      {0, NULL, NULL},
    };
    machines_type =
        g_enum_register_static ("GstTapFileEncMachines", machines_profiles);
  }

  return machines_type;
}

static GType
gst_videotypes_get_type (void)
{
  static GType videotypes_type = 0;

  if (videotypes_type == 0) {
    static const GEnumValue videotypes_profiles[] = {
      {TAP_VIDEOTYPE_PAL, "PAL", "PAL"},
      {TAP_VIDEOTYPE_NTSC, "NTSC", "NTSC"},
      {0, NULL, NULL},
    };
    videotypes_type =
        g_enum_register_static ("GstTapFileEncVideotypes", videotypes_profiles);
  }

  return videotypes_type;
}

/* initialize the tapfileenc's class */
static void
gst_tapfileenc_class_init (GstTapFileEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_tapfileenc_set_property;
  gobject_class->get_property = gst_tapfileenc_get_property;

  g_object_class_install_property (gobject_class, PROP_MACHINE_BYTE,
      g_param_spec_enum ("machine", "Machine",
          "Machine for which this dump is intended", gst_machines_get_type (),
          TAP_MACHINE_C64,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, PROP_VIDEO_BYTE,
      g_param_spec_enum ("videotype", "Video type",
          "Video type of machine for which this dump is intended",
          gst_videotypes_get_type (), TAP_VIDEOTYPE_PAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, PROP_FORCE_VERSION_0,
      g_param_spec_boolean ("version-0", "Force version 0",
          "If true, and incoming stream is not halfwaves, a version 0 TAP file will be created. Otherwise the version will be 1 for full waves and 2 for halfwaves.",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  gst_element_class_set_details_simple (element_class,
      "Commodore 64 TAP file writer",
      "Codec/Encoder/Audio",
      "Writes TAP data as TAP files",
      "Fabrizio Gennari <fabrizio.ge@tiscali.it>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* GstElement vmethod implementations */

#define OVERFLOW_HI 0xFFFFFF
#define OVERFLOW_LO 0x800

static GstFlowReturn
write_header (GstPad * pad, guchar version, guchar machine_byte,
    guchar video_byte, guint len)
{
  GstBuffer *buf = gst_buffer_new_and_alloc (20);
  guint8 *header;
  const char signature_c64[] = "C64-TAPE-RAW";
  const char signature_c16[] = "C16-TAPE-RAW";
  GstMapInfo map;

  gst_buffer_map (buf, &map, GST_MAP_WRITE);
  header = map.data;
  memcpy (header, machine_byte != 2 ? signature_c64 : signature_c16,
      strlen (signature_c64));
  header += strlen (signature_c64);
  *header++ = version;
  *header++ = machine_byte;
  *header++ = video_byte;
  *header++ = 0;
  GST_WRITE_UINT32_LE (header, len);
  gst_buffer_unmap (buf, &map);
  return gst_pad_push (pad, buf);
}

/* chain function
 * this function does the actual processing
 */

static GstFlowReturn
gst_tapfileenc_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstTapFileEnc *filter = GST_TAPFILEENC (parent);
  GstMapInfo map;
  guint *data;
  guint buflen = gst_buffer_get_size (buf) / sizeof (guint32);
  guint bufsofar;
  GstFlowReturn ret = GST_FLOW_OK;
  GstByteWriter *writer = gst_byte_writer_new ();
  guint size;

  if (!filter->sent_header) {
    ret = write_header (filter->srcpad, filter->version, filter->machine_byte, filter->video_byte, 0    /* real length will be written later */
        );

    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (filter, "push header failed: flow = %s",
          gst_flow_get_name (ret));
    }
    filter->sent_header = TRUE;
  }

  gst_buffer_map (buf, &map, GST_MAP_READ);
  data = (guint *) map.data;
  for (bufsofar = 0; bufsofar < buflen; bufsofar++) {
    guint pulse = data[bufsofar];
    if (filter->version == 0) {
      if (pulse >= OVERFLOW_LO && !filter->last_was_overflow) {
        gst_byte_writer_put_uint8 (writer, 0);
        filter->last_was_overflow = TRUE;
      } else {
        guint8 pulse8 = pulse / 8;
        gst_byte_writer_put_uint8 (writer, pulse8);
        filter->last_was_overflow = FALSE;
      }
    } else {
      while (pulse >= OVERFLOW_HI) {
        gst_byte_writer_put_uint8 (writer, 0);
        gst_byte_writer_put_uint24_le (writer, OVERFLOW_HI);
        pulse -= OVERFLOW_HI;
      }
      if (pulse >= OVERFLOW_LO) {
        gst_byte_writer_put_uint8 (writer, 0);
        gst_byte_writer_put_uint24_le (writer, pulse);
      } else {
        guint8 pulse8 = pulse / 8;
        gst_byte_writer_put_uint8 (writer, pulse8);
      }
    }
  }
  gst_buffer_unmap (buf, &map);

  size = gst_byte_writer_get_size (writer);
  if (size > 0) {
    GstBuffer *newbuf = gst_byte_writer_free_and_get_buffer (writer);
    ret = gst_pad_push (filter->srcpad, newbuf);
    filter->length += size;
  } else
    gst_byte_writer_free (writer);

  return ret;
}

static gboolean
gst_tapfileenc_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstTapFileEnc *filter = GST_TAPFILEENC (parent);
  GstSegment segment;
  GstStructure *structure;
  gint samplerate;
  GstCaps *caps;
  gboolean halfwaves;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      /* seek to beginning of file */
      gst_segment_init (&segment, GST_FORMAT_BYTES);
      if (!gst_pad_push_event (filter->srcpad,
              gst_event_new_segment (&segment)))
        return FALSE;
      write_header (filter->srcpad, filter->version, filter->machine_byte,
          filter->video_byte, filter->length);
      break;
    case GST_EVENT_CAPS:
      gst_event_parse_caps (event, &caps);
      structure = gst_caps_get_structure (caps, 0);
      if (!gst_structure_get_int (structure, "rate", &samplerate)) {
        GST_ERROR_OBJECT (filter, "input caps have no sample rate field");
        return FALSE;
      } else if (samplerate !=
          tap_clocks[filter->machine_byte][filter->video_byte]) {
        GST_ERROR_OBJECT (filter, "wrong sample rate");
        return FALSE;
      } else if (!gst_structure_get_boolean (structure, "halfwaves",
              &halfwaves)) {
        GST_ERROR_OBJECT (filter, "input caps have no halfwaves field");
        return FALSE;
      } else if (halfwaves)
        filter->version = 2;
      else
        filter->version = filter->force_version_0 ? 0 : 1;

      break;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gsttapfileenc_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstTapFileEnc *filter = GST_TAPFILEENC (parent);

      GstCaps *ret = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
      GstStructure *structure = gst_caps_get_structure (ret, 0);
      GValue value = { 0 };

      g_value_init (&value, G_TYPE_INT);
      g_value_set_int (&value,
          tap_clocks[filter->machine_byte][filter->video_byte]);
      gst_structure_set_value (structure, "rate", &value);
      gst_query_set_caps_result (query, ret);
      res = TRUE;

      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }
  return res;
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */

static void
gst_tapfileenc_init (GstTapFileEnc * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tapfileenc_chain));
  gst_pad_set_event_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tapfileenc_sink_event));
  gst_pad_set_query_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gsttapfileenc_query));
  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  filter->sent_header = FALSE;
  filter->length = 0;
}

gboolean
gst_tapfileenc_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_tapfileenc_debug, "tapfileenc",
      0, "Commodore 64 DMP encoder");

  return gst_element_register (plugin, "tapfileenc", GST_RANK_NONE,
      GST_TYPE_TAPFILEENC);
}
