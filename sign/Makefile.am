# Makefile for rpm library.

include $(top_srcdir)/rpm.am

AM_CPPFLAGS = -I$(top_builddir) -I$(top_srcdir) -I$(top_builddir)/include/
AM_CPPFLAGS += @WITH_NSS_INCLUDE@
AM_CPPFLAGS += @WITH_POPT_INCLUDE@
AM_CPPFLAGS += -I$(top_srcdir)/misc

usrlibdir = $(libdir)

usrlib_LTLIBRARIES = librpmsign.la
librpmsign_la_SOURCES = rpmgensig.c
librpmsign_la_LDFLAGS = -version-info 0:0:0
librpmsign_la_LIBADD = \
	$(top_builddir)/lib/librpm.la \
	$(top_builddir)/rpmio/librpmio.la \
	@WITH_POPT_LIB@ \
	@LIBINTL@
