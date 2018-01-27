pkglib_LIBRARIES = libremglk.a

libremglk_a_SOURCES = main.c rgevent.c rgfref.c rggestal.c rgdata.c rgmisc.c rgstream.c rgstyle.c rgwin_blank.c rgwin_buf.c rgwin_grid.c rgwin_pair.c rgwin_graph.c rgwindow.c rgschan.c rgblorb.c cgunicod.c cgdate.c gi_dispa.c gi_debug.c gi_blorb.c

libremglk_a_CFLAGS = -Wall -g -ansi

# cgunicod.c includes cgunigen.c...
cgunicod.o: cgunigen.c

pkginclude_HEADERS = glk.h glkstart.h

# We don't rely on AX_CREATE_PKGCONFIG_INFO to create the pkg-config .pc file.
pkgconfiglibdir = $(libdir)/pkgconfig
pkgconfiglib_DATA = remglk.pc

# See https://www.gnu.org/software/autoconf/manual/autoconf.html#Installation-Directory-Variables
# for why this is a sed rule, and not in configure.ac as AC_CONFIG_FILES.
remglk.pc: remglk.pc.in Makefile
	sed \
		-e 's|@prefix[@]|$(prefix)|g' \
		-e 's|@exec_prefix[@]|$(exec_prefix)|g' \
		-e 's|@includedir[@]|$(includedir)|g' \
		-e 's|@libdir[@]|$(libdir)|g' \
		-e 's|@PACKAGE_NAME[@]|$(PACKAGE_NAME)|g' \
		-e 's|@PACKAGE_VERSION[@]|$(PACKAGE_VERSION)|g' \
		-e 's|@PACKAGE_URL[@]|$(PACKAGE_URL)|g' \
		$@.in > $@
