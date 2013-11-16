dnl required version of autoconf
AC_PREREQ([2.53])

dnl TODO: fill in your package name and package version here
AC_INIT([gst-tap],[1.0.0])

dnl required versions of gstreamer and plugins-base
GST_REQUIRED=1.0.0
GSTPB_REQUIRED=1.0.0

AC_CONFIG_SRCDIR([tap/plugin.c])
AC_CONFIG_HEADERS([config.h])

dnl required version of automake
AM_INIT_AUTOMAKE([1.10])

dnl enable mainainer mode by default
AM_MAINTAINER_MODE([enable])

dnl check for tools (compiler etc.)
AC_PROG_CC

dnl required version of libtool
LT_PREREQ([2.2.6])
LT_INIT

dnl give error and exit if we don't have pkgconfig
AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, [ ], [
  AC_MSG_ERROR([You need to have pkg-config installed!])
])

dnl Check for the required version of GStreamer core (and gst-plugins-base)
dnl This will export GST_CFLAGS and GST_LIBS variables for use in Makefile.am
dnl
dnl If you need libraries from gst-plugins-base here, also add:
dnl for libgstaudio-1.0: gstreamer-audio-1.0 >= $GST_REQUIRED
dnl for libgstvideo-1.0: gstreamer-video-1.0 >= $GST_REQUIRED
dnl for libgsttag-1.0: gstreamer-tag-1.0 >= $GST_REQUIRED
dnl for libgstpbutils-1.0: gstreamer-pbutils-1.0 >= $GST_REQUIRED
dnl for libgstfft-1.0: gstreamer-fft-1.0 >= $GST_REQUIRED
dnl for libgstinterfaces-1.0: gstreamer-interfaces-1.0 >= $GST_REQUIRED
dnl for libgstrtp-1.0: gstreamer-rtp-1.0 >= $GST_REQUIRED
dnl for libgstrtsp-1.0: gstreamer-rtsp-1.0 >= $GST_REQUIRED
dnl etc.
PKG_CHECK_MODULES(GST, [
  gstreamer-1.0 >= $GST_REQUIRED
  gstreamer-base-1.0 >= $GST_REQUIRED
  gstreamer-controller-1.0 >= $GST_REQUIRED
  gstreamer-audio-1.0 >= $GST_REQUIRED
], [
  AC_SUBST(GST_CFLAGS)
  AC_SUBST(GST_LIBS)
], [
  AC_MSG_ERROR([
      You need to install or upgrade the GStreamer development
      packages on your system. On debian-based systems these are
      libgstreamer1.0-dev and libgstreamer-plugins-base1.0-dev.
      on RPM-based systems gstreamer1.0-devel, libgstreamer1.0-devel
      or similar. The minimum version required is $GST_REQUIRED.
  ])
])

AC_ARG_WITH(libtap-includes, [Where the header files for libtap are located])
AC_ARG_WITH(libtap-libs, [Where the libtap library is located])

translit(dnm, m, l) AM_CONDITIONAL(USE_TAPENC, true)
AG_GST_CHECK_FEATURE(TAPENC, [TAP audio encoder], tapenc, [
    TAPENC_LIBS=-ltapencoder
    TAPENC_CPPFLAGS="-I$with_libtap_includes"
    TAPENC_LDFLAGS="-L$with_libtap_libs"
    CPPFLAGS="$CPPFLAGS $TAPENC_CPPFLAGS"
    LDFLAGS="$LDFLAGS $TAPENC_LDFLAGS"
    AC_CHECK_HEADER(tapencoder.h,
      [
        AC_CHECK_LIB(tapencoder,tapenc_get_pulse,HAVE_TAPENC=yes)
      ]
    )
    AC_SUBST(TAPENC_LIBS)
    AC_SUBST(TAPENC_CPPFLAGS)
    AC_SUBST(TAPENC_LDFLAGS)
  ]
)

translit(dnm, m, l) AM_CONDITIONAL(USE_TAPDEC, true)
AG_GST_CHECK_FEATURE(TAPDEC, [TAP audio decoder], tapdec, [
    TAPDEC_LIBS=-ltapdecoder
    TAPDEC_CPPFLAGS="-I$with_libtap_includes"
    TAPDEC_LDFLAGS="-L$with_libtap_libs"
    CPPFLAGS="$CPPFLAGS $TAPDEC_CPPFLAGS"
    LDFLAGS="$LDFLAGS $TAPDEC_LDFLAGS"
    AC_CHECK_HEADER(tapdecoder.h,
      [
        AC_CHECK_LIB(tapdecoder,tapdec_set_pulse,HAVE_TAPDEC=yes)
      ]
    )
    AC_SUBST(TAPDEC_LIBS)
    AC_SUBST(TAPDEC_CPPFLAGS)
    AC_SUBST(TAPDEC_LDFLAGS)
  ]
)

dnl check if compiler understands -Wall (if yes, add -Wall to GST_CFLAGS)
AC_MSG_CHECKING([to see if compiler understands -Wall])
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([ ], [ ])], [
  GST_CFLAGS="$GST_CFLAGS -Wall"
  AC_MSG_RESULT([yes])
], [
  AC_MSG_RESULT([no])
])

dnl set the plugindir where plugins should be installed (for src/Makefile.am)
if test "x${prefix}" = "x$HOME"; then
  plugindir="$HOME/.gstreamer-1.0/plugins"
else
  plugindir="\$(libdir)/gstreamer-1.0"
fi
AC_SUBST(plugindir)

dnl set proper LDFLAGS for plugins
GST_PLUGIN_LDFLAGS='-module -avoid-version -export-symbols-regex [_]*\(gst_\|Gst\|GST_\).* -L.'
AC_SUBST(GST_PLUGIN_LDFLAGS)

AC_CONFIG_FILES([
Makefile
tap/Makefile
tapenc/Makefile
tapdec/Makefile
])
AC_OUTPUT

