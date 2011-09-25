# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgsttapenc.la

# sources used to compile this plug-in
libgsttapenc_la_SOURCES = gsttapenc.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgsttapenc_la_CFLAGS = $(GST_CFLAGS)
libgsttapenc_la_CPPFLAGS = $(TAPENC_CPPFLAGS)
libgsttapenc_la_LIBADD = $(GST_LIBS) $(TAPENC_LIBS)
libgsttapenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(TAPENC_LDFLAGS)
libgsttapenc_la_LIBTOOLFLAGS = --tag=disable-static

