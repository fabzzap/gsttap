# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgsttapdec.la

# sources used to compile this plug-in
libgsttapdec_la_SOURCES = gsttapdec.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgsttapdec_la_CFLAGS = $(GST_CFLAGS)
libgsttapdec_la_CPPFLAGS = $(TAPDEC_CPPFLAGS)
libgsttapdec_la_LIBADD = $(GST_LIBS) $(TAPDEC_LIBS)
libgsttapdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(TAPDEC_LDFLAGS)
libgsttapdec_la_LIBTOOLFLAGS = --tag=disable-static

