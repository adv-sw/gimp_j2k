## Process this file with automake to produce Makefile.in

bin_PROGRAMS = jp2

bindir = $(GIMP_LIBDIR)/plug-ins

jp2_SOURCES = \
	plugin-intl.h	\
	interface.c	\
	interface.h	\
	main.c		\
	main.h		\
	read_j2k.c	\
	write_j2k.c

AM_CPPFLAGS = \
	-DLOCALEDIR=\""$(LOCALEDIR)"\"		\
	-DDATADIR=\""$(DATADIR)"\"

INCLUDES =\
	-I$(top_srcdir)		\
	@GIMP_CFLAGS@		\
	-I$(includedir)

LDADD = $(GIMP_LIBS) -llibopenjpeg

