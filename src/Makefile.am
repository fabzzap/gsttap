# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgsttap.la

# sources used to compile this plug-in
libgsttap_la_SOURCES = gsttapenc.c gsttapenc.h \
gsttapdec.c gsttapdec.h \
gstdmpenc.c gstdmpenc.h \
gstdmpdec.c gstdmpdec.h \
gsttapfileenc.c gsttapfileenc.h \
gsttapconvert.c gsttapconvert.h \
plugin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgsttap_la_CFLAGS = $(GST_CFLAGS)
libgsttap_la_LIBADD = $(GST_LIBS)
libgsttap_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttap_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gsttapenc.h gsttapdec.h gstdmpenc.h gstdmpdec.h gsttapfileenc.h gsttapconvert.h

