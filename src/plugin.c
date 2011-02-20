/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gsttapenc.h"
#include "gstdmpenc.h"
#include "gsttapfileenc.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  return
    gst_tapenc_register (plugin)
 && gst_dmpenc_register (plugin)
 && gst_tapfileenc_register (plugin);
}

/* gstreamer looks for this structure to register tapencoders
 *
 * 
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "tap",
    "Commodore 64 tape support",
    plugin_init,
    VERSION,
    "LGPL",
    PACKAGE,
    "http://wav-prg.sourceforge.net/"
);

