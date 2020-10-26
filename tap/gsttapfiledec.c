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
  GstBaseTapContainerDec element;
  guchar version;
  gboolean last_was_0;
};

struct _GstTapFileDecClass
{
  GstBaseTapContainerDecClass parent_class;
};

GST_DEBUG_CATEGORY_STATIC (gst_tapfiledec_debug);
#define GST_CAT_DEFAULT gst_tapfiledec_debug

/* Who cares about having 8x these resolutions for pauses anyway? */
static const guint tap_clocks[][2] = {
  {123156, 127840},             /* C64 */
  {138550, 127840},             /* VIC */
  {110840, 111860}              /* C16 */
};

/* Standard function returning type information. */
GType gst_tapfiledec_get_type (void);
G_DEFINE_TYPE (GstTapFileDec, gst_tapfiledec, GST_TYPE_BASETAPCONTAINERDEC);

static gsize gst_tapfiledec_get_header_size (GstBaseTapContainerDec * filter);
static GstBaseTapContainerHeaderStatus
gst_tapfiledec_read_header (GstBaseTapContainerDec * filter,
    const guint8 * header_data);
static const gchar *gst_tapfiledec_get_container_format (GstBaseTapContainerDec
    * filter);
static gboolean gst_tapfiledec_read_pulse (GstBaseTapContainerDec * filter,
    GstBaseTapContainerReadData read_data, guint * pulse);

static void
gst_tapfiledec_class_init (GstTapFileDecClass * bclass)
{
  GstBaseTapContainerDecClass *parent_class =
      GST_BASETAPCONTAINERDEC_CLASS (bclass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (bclass);
  gst_element_class_set_metadata (element_class,
      "Commodore 64 TAP file reader",
       "Codec/Parser/Audio",
      "Reads TAP data from TAP files",
       "Fabrizio Gennari <fabrizio.ge@tiscali.it>");

  parent_class->get_header_size = gst_tapfiledec_get_header_size;
  parent_class->get_container_format = gst_tapfiledec_get_container_format;
  parent_class->read_header = gst_tapfiledec_read_header;
  parent_class->read_pulse = gst_tapfiledec_read_pulse;
}

static void
gst_tapfiledec_init (GstTapFileDec * trans)
{
}

#define TAPFILEDEC_HEADER_SIZE 20
#define VALUE_OF_0_IN_TAP_V0 25000
#define THREE_BYTE_OVERFLOW 0xFFFFFF

static gsize
gst_tapfiledec_get_header_size (GstBaseTapContainerDec * filter)
{
  return TAPFILEDEC_HEADER_SIZE;
}

static GstBaseTapContainerHeaderStatus
gst_tapfiledec_read_header (GstBaseTapContainerDec * filter,
    const guint8 * header_data)
{
  const char expected_signature1[] = "C64-TAPE-RAW";
  const char expected_signature2[] = "C16-TAPE-RAW";
  guchar machine, video_standard;
  GstTapFileDec *decoder = GST_TAPFILEDEC (filter);

  if (memcmp (header_data, expected_signature1,
          strlen (expected_signature1)) != 0
      && memcmp (header_data, expected_signature2,
          strlen (expected_signature2)) != 0)
    return GST_BASE_TAP_CONVERT_NO_VALID_HEADER;
  header_data += strlen (expected_signature1);
  decoder->version = *header_data++;
  if (decoder->version != 0 && decoder->version != 1 && decoder->version != 2)
    return GST_BASE_TAP_CONVERT_NO_VALID_HEADER;
  machine = *header_data++;
  if (machine != 0              /* C64 */
      && machine != 1           /* VIC20 */
      && machine != 2           /* C16 */
      )
    return GST_BASE_TAP_CONVERT_NO_VALID_HEADER;
  video_standard = *header_data++;
  if (video_standard != 0       /* PAL */
      && video_standard != 1    /* NTSC */
      )
    return GST_BASE_TAP_CONVERT_NO_VALID_HEADER;
  filter->rate = tap_clocks[machine][video_standard];
  filter->halfwaves = decoder->version == 2;

  return GST_BASE_TAP_CONVERT_VALID_HEADER;
}

static const gchar *
gst_tapfiledec_get_container_format (GstBaseTapContainerDec * filter)
{
  return "TAP Commodore tape image file";
}

static gboolean
gst_tapfiledec_read_pulse (GstBaseTapContainerDec * filter,
    GstBaseTapContainerReadData read_data, guint * pulse)
{
  gboolean overflow_occurred;
  GstTapFileDec *decoder = GST_TAPFILEDEC (filter);
  do {
    const guint8 *inbytes = read_data (filter, 1);
    guint inpulse;

    *pulse = 0;
    overflow_occurred = FALSE;
    if (inbytes == NULL)
      return FALSE;
    inpulse = inbytes[0];
    if (inpulse == 0) {
      if (decoder->version == 0) {
        if (!decoder->last_was_0) {
          decoder->last_was_0 = TRUE;
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
      decoder->last_was_0 = FALSE;
    *pulse += inpulse;
  } while (overflow_occurred);
  return TRUE;
}

gboolean
gst_tapfiledec_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_tapfiledec_debug, "tapfiledec",
      0, "Commodore TAP file decoder");

  return gst_element_register (plugin, "tapfiledec", GST_RANK_MARGINAL,
      GST_TYPE_TAPFILEDEC);
}
