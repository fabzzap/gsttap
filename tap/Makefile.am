# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgsttap.la

# sources used to compile this plug-in
libgsttap_la_SOURCES = \
gstdmpdec.c gstdmpdec.h \
gsttapfileenc.c gsttapfileenc.h \
gsttapfiledec.c gsttapfiledec.h \
gsttapconvert.c gsttapconvert.h \
gstbasetapcontainerdec.c gstbasetapcontainerdec.h \
plugin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgsttap_la_CFLAGS = $(GST_CFLAGS)
libgsttap_la_LIBADD = $(GST_LIBS)
libgsttap_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttap_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstdmpdec.h gsttapfileenc.h gsttapfiledec.h gsttapconvert.h

