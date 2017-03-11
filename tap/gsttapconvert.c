/*
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) 2011 Fabrizio Gennari <fabrizio.ge@tiscali.it>
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
 * SECTION:element-tapconvert
 *
 * Change frequency of an audio stream in the Commodore TAP format.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! tapenc ! tapconvert ! tapefileenc ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include "gsttapconvert.h"

GST_DEBUG_CATEGORY_STATIC (gst_tapconvert_debug);
#define GST_CAT_DEFAULT gst_tapconvert_debug

#define GST_TYPE_TAP_CONVERT \
  (gst_tapconvert_get_type())
#define GST_TAP_CONVERT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TAP_CONVERT,GstTapConvert))
#define GST_TAP_CONVERT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TAP_CONVERT,GstTapConvertClass))
#define GST_IS_TAP_CONVERT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TAP_CONVERT))
#define GST_IS_TAP_CONVERT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TAP_CONVERT))

typedef struct _GstTapConvert GstTapConvert;
typedef struct _GstTapConvertClass GstTapConvertClass;

struct _GstTapConvert
{
  GstBaseTransform element;

  gint inrate;
  gint outrate;
  enum
  {
    wave_unchanged,
    wave_half_to_full,
    wave_full_to_half
  } waves;
  GstPadGetRangeFunction base_getrange;
};

struct _GstTapConvertClass
{
  GstBaseTransformClass parent_class;
};

/* the capabilities of the inputs and outputs.
 *
 */

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-tap")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-tap")
    );

/* debug category for fltering log messages
 *
 */

G_DEFINE_TYPE (GstTapConvert, gst_tapconvert, GST_TYPE_BASE_TRANSFORM);

static gboolean gst_tapconvert_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_tapconvert_transform_ip (GstBaseTransform * base,
    GstBuffer * outbuf);
static GstFlowReturn gst_tapconvert_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static GstCaps *gst_tapconvert_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_tapconvert_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);
static GstFlowReturn gst_tapconvert_getrange (GstPad * pad, GstObject * parent,
    guint64 offset, guint length, GstBuffer ** buffer);
/* GObject vmethod implementations */

/* initialize the plugin's class */
static void
gst_tapconvert_class_init (GstTapConvertClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_BASE_TRANSFORM_CLASS (klass)->transform_ip =
      GST_DEBUG_FUNCPTR (gst_tapconvert_transform_ip);
  GST_BASE_TRANSFORM_CLASS (klass)->transform =
      GST_DEBUG_FUNCPTR (gst_tapconvert_transform);
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps =
      GST_DEBUG_FUNCPTR (gst_tapconvert_set_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      GST_DEBUG_FUNCPTR (gst_tapconvert_transform_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_tapconvert_get_unit_size);

  gst_element_class_set_details_simple (element_class,
      "Commodore 64 TAP rate converter",
      "Filter/Converter/Audio",
      "Adapts rate of a Commodore TAP stream",
      "Fabrizio Gennari <fabrizio.ge@tiscali.it>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  GST_DEBUG_CATEGORY_INIT (gst_tapconvert_debug, "tapconvert", 0,
      "TAP rate convert");
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_tapconvert_init (GstTapConvert * filter)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (filter);
  filter->base_getrange = trans->srcpad->getrangefunc;
  gst_pad_set_getrange_function (trans->srcpad, gst_tapconvert_getrange);
}

/* GstBaseTransform vmethod implementations */

static GstFlowReturn
gst_tapconvert_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstTapConvert *filter = GST_TAP_CONVERT (base);
  guint buflen;
  guint bufsofar;
  guint *data;
  GstMapInfo map;

  if (!gst_buffer_map (outbuf, &map, GST_MAP_READWRITE))
    return GST_FLOW_ERROR;
  data = (guint32 *) map.data;
  buflen = map.size / sizeof (guint32);
  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (outbuf)))
    gst_object_sync_values (GST_OBJECT (filter), GST_BUFFER_TIMESTAMP (outbuf));

  for (bufsofar = 0; bufsofar < buflen; bufsofar++) {
    guint64 pulse = (guint64) data[bufsofar] * filter->outrate;
    data[bufsofar] = (guint32) (pulse / filter->inrate);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_tapconvert_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstTapConvert *filter = GST_TAP_CONVERT (trans);
  guint buflen;
  guint inbufsofar, outbufsofar;
  guint *indata, *outdata;
  GstMapInfo inmap, outmap;
  GstFlowReturn ret;

  if (!gst_buffer_map (inbuf, &inmap, GST_MAP_READ))
    return GST_FLOW_ERROR;
  indata = (guint32 *) inmap.data;
  ret = GST_FLOW_ERROR;
  if (gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE)) {
    outdata = (guint32 *) outmap.data;
    if (filter->waves == wave_full_to_half) {
      buflen = inmap.size / sizeof (guint32);
      outbufsofar = 0;

      for (inbufsofar = 0; inbufsofar < buflen; inbufsofar++) {
        guint64 pulse = (guint64) indata[inbufsofar] * filter->outrate;
        guint32 converted_pulse = pulse / filter->inrate;
        outdata[outbufsofar] = (guint32) converted_pulse / 2;
        outdata[outbufsofar + 1] = converted_pulse - outdata[outbufsofar];
        outbufsofar += 2;
      }
      ret = GST_FLOW_OK;
    } else if (filter->waves == wave_half_to_full) {
      buflen = outmap.size / sizeof (guint32);
      inbufsofar = 0;

      for (outbufsofar = 0; outbufsofar < buflen; outbufsofar++) {
        guint64 pulse = (guint64) indata[inbufsofar++] * filter->outrate;
        pulse += indata[inbufsofar++] * filter->outrate;
        outdata[outbufsofar] = pulse / filter->inrate;
      }
      ret = GST_FLOW_OK;
    }
    gst_buffer_unmap (outbuf, &outmap);
  }
  gst_buffer_unmap (inbuf, &inmap);

  return ret;
}

static gboolean
gst_tapconvert_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstTapConvert *filter = GST_TAP_CONVERT (trans);
  GstStructure *instructure = gst_caps_get_structure (incaps, 0);
  GstStructure *outstructure = gst_caps_get_structure (outcaps, 0);
  gboolean inhalfwaves, outhalfwaves;
  gboolean ret1 = gst_structure_get_int (instructure, "rate", &filter->inrate);
  if (!ret1)
    GST_WARNING_OBJECT (filter, "input caps have no rate");
  gboolean ret2 =
      gst_structure_get_int (outstructure, "rate", &filter->outrate);
  if (!ret2)
    GST_WARNING_OBJECT (filter, "output caps have no rate");
  gboolean ret3 =
      gst_structure_get_boolean (instructure, "halfwaves", &inhalfwaves);
  if (!ret3)
    GST_WARNING_OBJECT (filter, "input caps have no indication about halfwaves");
  gboolean ret4 =
      gst_structure_get_boolean (outstructure, "halfwaves", &outhalfwaves);
  if (!ret4)
    GST_WARNING_OBJECT (filter, "output caps have no rate");
  gboolean ret = ret1 && ret2 && ret3 && ret4;

  GST_DEBUG_OBJECT (trans, "from: %" GST_PTR_FORMAT, instructure);
  GST_DEBUG_OBJECT (trans, "to: %" GST_PTR_FORMAT, outstructure);

  if (inhalfwaves == outhalfwaves) {
    gst_base_transform_set_in_place (trans, TRUE);
    filter->waves = wave_unchanged;
  } else {
    gst_base_transform_set_in_place (trans, FALSE);
    filter->waves = inhalfwaves ? wave_half_to_full : wave_full_to_half;
  }

  if (!ret)
    GST_WARNING_OBJECT (filter, "incomplete caps");

  return ret;
}

static gboolean
gst_tapconvert_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gboolean halfwaves;
  GstTapConvert *filter = GST_TAP_CONVERT (trans);

  if (!gst_structure_get_boolean (structure, "halfwaves", &halfwaves))
    return FALSE;
  *size = sizeof (guint32);
  if (halfwaves && (filter->waves == wave_full_to_half
          || filter->waves == wave_half_to_full))
    *size *= 2;

  return TRUE;
}

static GstCaps *
gst_tapconvert_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GST_DEBUG_OBJECT (trans, "direction %s from: %" GST_PTR_FORMAT,
      direction == GST_PAD_SRC ? "src" : "sink", caps);
  GstStaticCaps static_caps = GST_STATIC_CAPS ("audio/x-tap");
  GstCaps *newcaps = gst_static_caps_get (&static_caps);
  if (direction == GST_PAD_SINK) {
    GstStructure *newstructure = gst_caps_get_structure (newcaps, 0);
    GstCaps *othercaps = gst_pad_peer_query_caps (trans->srcpad, NULL);

    if (othercaps && gst_caps_get_size (othercaps) > 0) {
      GstStructure *structure = gst_caps_get_structure (othercaps, 0);
      const GValue *rate = gst_structure_get_value (structure, "rate");
      const GValue *halfwaves = gst_structure_get_value (structure, "halfwaves");
      gboolean has_rate = G_VALUE_HOLDS (rate, G_TYPE_INT);
      gboolean has_halfwaves = G_VALUE_HOLDS (halfwaves, G_TYPE_BOOLEAN);
      if (has_rate || has_halfwaves) {
        GstStructure *src_structure;
        gst_caps_make_writable(newcaps);
        if (!has_rate || !has_halfwaves)
          src_structure = gst_caps_get_structure (caps, 0);
        if (has_rate)
          gst_structure_set_value (newstructure, "rate", rate);
        else {
          const GValue *src_rate = gst_structure_get_value (src_structure, "rate");
          if (G_VALUE_HOLDS (src_rate, G_TYPE_INT))
            gst_structure_set_value (newstructure, "rate", src_rate);
        }
        if (has_halfwaves)
          gst_structure_set_value (newstructure, "halfwaves", halfwaves);
        else {
          const GValue *src_halfwaves = gst_structure_get_value (src_structure, "halfwaves");
          if (G_VALUE_HOLDS (src_halfwaves, G_TYPE_BOOLEAN))
            gst_structure_set_value (newstructure, "halfwaves", src_halfwaves);
        }
      }
    }
  }

  GST_DEBUG_OBJECT (trans, "to: %" GST_PTR_FORMAT, newcaps);

  return newcaps;
}

/* work around the brokenness of gst_base_transform_getrange */
static GstFlowReturn
gst_tapconvert_getrange (GstPad * pad, GstObject * parent, guint64 offset,
    guint length, GstBuffer ** buffer)
{
  GstBaseTransformClass *klass = GST_BASE_TRANSFORM_GET_CLASS (parent);
  GstTapConvert *filter = GST_TAP_CONVERT (parent);
  GstBaseTransform *trans = GST_BASE_TRANSFORM (parent);
  GstFlowReturn ret;
  GstBuffer *inbuf = NULL;
  GstBuffer *outbuf = NULL;
  gsize other_length;
  GstCaps *incaps, *outcaps;

  ret = klass->submit_input_buffer (trans, FALSE, gst_buffer_new ());
  if (ret != GST_FLOW_OK) {
    if (ret != GST_FLOW_NOT_NEGOTIATED)
      goto pull_error;
  } else {
    gst_buffer_unref (trans->queued_buf);
    trans->queued_buf = NULL;
  }

  if (filter->waves == wave_unchanged)
    return filter->base_getrange (pad, parent, offset, length, buffer);

  incaps = gst_pad_get_current_caps (trans->sinkpad);
  outcaps = gst_pad_get_current_caps (pad);

  if (!klass->transform_size (trans, GST_PAD_SRC, outcaps, length, incaps,
          &other_length)) {
    ret = GST_FLOW_ERROR;
    goto pull_error;
  }

  ret = gst_pad_pull_range (trans->sinkpad, offset, other_length, &inbuf);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto pull_error;

  if (klass->before_transform)
    klass->before_transform (trans, inbuf);

  ret = klass->submit_input_buffer (trans, FALSE, inbuf);
  if (ret != GST_FLOW_OK) {
    if (ret == GST_BASE_TRANSFORM_FLOW_DROPPED)
      ret = GST_FLOW_OK;
    goto done;
  }
  ret = klass->generate_output (trans, &outbuf);

  *buffer = outbuf;
done:
  return ret;

  /* ERRORS */
pull_error:
  {
    GST_DEBUG_OBJECT (trans, "failed to pull a buffer: %s",
        gst_flow_get_name (ret));
    goto done;
  }
}

gboolean
gst_tapconvert_register (GstPlugin * plugin)
{
  return gst_element_register (plugin, "tapconvert", GST_RANK_NONE,
      GST_TYPE_TAP_CONVERT);
}
