# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgsttapenc.la

# sources used to compile this plug-in
libgsttapenc_la_SOURCES = gsttapenc.c gsttapenc.h

# compiler and linker flags used to compile this plugin, set in configure.ac
libgsttapenc_la_CFLAGS = $(GST_CFLAGS)
libgsttapenc_la_LIBADD = $(GST_LIBS)
libgsttapenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttapenc_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gsttapenc.h

