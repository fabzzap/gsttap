# Note: plugindir is set in configure

##############################################################################
# TODO: change libgstapenc.la to something else, e.g. libmysomething.la     #
##############################################################################
plugin_LTLIBRARIES = libgsttap.la

##############################################################################
# TODO: for the next set of variables, name the prefix if you named the .la, #
#  e.g. libmysomething.la => libmysomething_la_SOURCES                       #
#                            libmysomething_la_CFLAGS                        #
#                            libmysomething_la_LIBADD                        #
#                            libmysomething_la_LDFLAGS                       #
##############################################################################

# sources used to compile this plug-in
libgsttap_la_SOURCES = gsttapenc.c gsttapenc.h gstdmpenc.c gstdmpenc.h plugin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgsttap_la_CFLAGS = $(GST_CFLAGS)
libgsttap_la_LIBADD = $(GST_LIBS)
libgsttap_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttap_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gsttapenc.h gstdmpenc.h
