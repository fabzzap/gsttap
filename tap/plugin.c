/*
 * GStreamer
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

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <string.h>

#include "gstdmpdec.h"
#include "gsttapfileenc.h"
#include "gsttapfiledec.h"
#include "gsttapconvert.h"

static void
tap_type_find (GstTypeFind * tf, gpointer caps_pointer)
{
  GstCaps *tap_caps = (GstCaps *) caps_pointer;
  const guint8 *data = gst_type_find_peek (tf, 0, 12);
  if (data) {
    if (memcmp (data, "C64-TAPE-RAW", 12) == 0
        || memcmp (data, "C16-TAPE-RAW", 12) == 0) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, tap_caps);
    }
  }
}

static void
dmp_type_find (GstTypeFind * tf, gpointer caps_pointer)
{
  GstCaps *dmp_caps = (GstCaps *) caps_pointer;
  const guint8 *data = gst_type_find_peek (tf, 0, 12);
  if (data && memcmp (data, "DC2N-TAP-RAW", 12) == 0) {
    gst_type_find_suggest (tf, GST_TYPE_FIND_LIKELY, dmp_caps);
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GstCaps *tap_caps = gst_caps_new_empty_simple ("audio/x-tap-tap");
  GstCaps *dmp_caps = gst_caps_new_empty_simple ("audio/x-tap-dmp");
  if (!gst_type_find_register (plugin, "audio/x-tap-tap", GST_RANK_PRIMARY,
          tap_type_find, "tap", tap_caps, tap_caps, NULL))
    return FALSE;
  if (!gst_type_find_register (plugin, "audio/x-tap-dmp", GST_RANK_SECONDARY,
          dmp_type_find, "dmp", dmp_caps, dmp_caps, NULL)) {
    return FALSE;
  }
  return
    gst_dmpdec_register (plugin)
 && gst_tapfileenc_register (plugin)
 && gst_tapfiledec_register (plugin)
 && gst_tapconvert_register (plugin)
;
}

/* gstreamer looks for this structure to register tap file manipulation
 *
 * 
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "tap",
    "Commodore tape file formats and frequency conversion support",
    plugin_init,
    VERSION,
    "LGPL",
    PACKAGE,
    "http://wav-prg.sourceforge.net/"
);

